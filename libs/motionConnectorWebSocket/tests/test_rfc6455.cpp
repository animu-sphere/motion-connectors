// SPDX-License-Identifier: Apache-2.0
//
// The RFC 6455 subset on bytes alone (WEBSOCKET_CONNECTOR.md §4, §5, §10): no
// socket and no clock.
//
// With no argument, the unit cases: the accept key and what it rests on, the
// frame writer's length encoding, the reader's bound, and two sessions talking
// to each other in memory. With a directory, the framing corpus: each stream
// is driven through a session as its reads arrived, and the messages, pongs,
// result, code, close status and byte must be what `expectations.txt` says --
// and every stream must have a line, so none is added and read by nobody.
#include "Rfc6455.h"
#include "Session.h"

#include "motionConnectorTransport/PacketCapture.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace ws = openstrata::connectors::websocket::internal;
namespace transport = openstrata::connectors::transport;

int failures = 0;

void
Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++failures;
    }
}

std::string
Hex(const std::array<std::uint8_t, 20>& digest)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t byte : digest)
    {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Unit cases
// ---------------------------------------------------------------------------

void
TestSha1()
{
    // FIPS 180 test vectors.
    Check(Hex(ws::Sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 empty");
    Check(Hex(ws::Sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
    Check(Hex(ws::Sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "sha1 two blocks");
    Check(Hex(ws::Sha1(std::string(1000000, 'a'))) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
          "sha1 a million a");
}

void
TestBase64()
{
    // RFC 4648 §10.
    const std::pair<std::string, std::string> vectors[] = {
        {"", ""},         {"f", "Zg=="},         {"fo", "Zm8="},        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    for (const auto& [plain, encoded] : vectors)
    {
        Check(ws::Base64Encode(plain) == encoded, "base64 encode " + plain);
        const auto decoded = ws::Base64Decode(encoded);
        Check(decoded && *decoded == plain, "base64 decode " + encoded);
    }
    for (const char* refused : {"Zg=", "Z!==", "Zg==Zg==", "=Zg=", "Zm9v\n"})
    {
        Check(!ws::Base64Decode(refused), std::string("base64 refuses ") + refused);
    }
}

void
TestAcceptKey()
{
    // RFC 6455 §1.3's worked example.
    Check(ws::AcceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "accept key");
}

void
TestUtf8()
{
    Check(ws::IsUtf8(""), "utf8 empty");
    Check(ws::IsUtf8("ascii"), "utf8 ascii");
    Check(ws::IsUtf8("\xc3\xa9\xe3\x83\xa2\xf0\x9f\x98\x80"), "utf8 two, three and four bytes");
    Check(!ws::IsUtf8("\xc3\x28"), "utf8 bad continuation");
    Check(!ws::IsUtf8("\xc0\xaf"), "utf8 overlong two");
    Check(!ws::IsUtf8("\xe0\x80\xaf"), "utf8 overlong three");
    Check(!ws::IsUtf8("\xed\xa0\x80"), "utf8 surrogate");
    Check(!ws::IsUtf8("\xf4\x90\x80\x80"), "utf8 above U+10FFFF");
    Check(!ws::IsUtf8("\xe3\x83"), "utf8 truncated");
}

void
TestFrameLengths()
{
    // The writer's length encoding is minimal, so the reader's refusal of a
    // non-minimal one can never fire on this repository's own frames.
    const std::pair<std::size_t, std::size_t> cases[] = {
        {0, 2}, {125, 2}, {126, 4}, {65535, 4}, {65536, 10}};
    for (const auto& [length, header] : cases)
    {
        std::string out;
        ws::AppendFrame(&out, ws::Opcode::Text, std::string(length, 'a'));
        Check(out.size() == header + length, "header length for " + std::to_string(length));
    }

    // Masked by a client, read back by a server, at the default bound.
    std::string stream;
    const ws::MaskKey mask = {1, 2, 3, 4};
    ws::AppendFrame(&stream, ws::Opcode::Text, "{\"a\":1}", true, &mask);
    ws::AppendFrame(&stream, ws::Opcode::Text, "{\"b\":", false, &mask);
    ws::AppendFrame(&stream, ws::Opcode::Continuation, "2}", true, &mask);
    ws::FrameReader reader(true, 65507);
    reader.Append(stream);
    ws::FrameEvent event;
    Check(reader.Next(&event) && event.kind == ws::FrameEvent::Kind::Message &&
              event.payload == "{\"a\":1}",
          "masked message");
    Check(reader.Next(&event) && event.kind == ws::FrameEvent::Kind::Message &&
              event.payload == "{\"b\":2}" && event.byte == 24,
          "reassembled message names its last frame");
    Check(!reader.Next(&event) && !reader.Stopped(), "nothing more");
}

void
TestDefaultBound()
{
    // §5: 65 507 bytes, checked from the header before a payload byte.
    const auto header = [](std::uint64_t length)
    {
        std::string out;
        out.push_back(static_cast<char>(0x81));
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>(length >> 8));
        out.push_back(static_cast<char>(length & 0xff));
        out += std::string("\x00\x00\x00\x00", 4);
        return out;
    };
    ws::FrameReader atBound(true, 65507);
    atBound.Append(header(65507));
    ws::FrameEvent event;
    Check(!atBound.Next(&event) && !atBound.Stopped(), "65 507 waits for its payload");

    ws::FrameReader overBound(true, 65507);
    overBound.Append(header(65508));
    Check(overBound.Next(&event) && event.kind == ws::FrameEvent::Kind::Fault &&
              event.fault == ws::FramingFault::MessageTooLarge && event.byte == 0,
          "65 508 is refused from the header");
    Check(ws::CloseStatusFor(event.fault) == 1009, "too large closes 1009");
}

void
Deliver(ws::Session& from, ws::Session& to)
{
    std::string& out = from.Outbound();
    to.Receive(out);
    out.clear();
}

std::vector<ws::SessionEvent>
Drain(ws::Session& session)
{
    std::vector<ws::SessionEvent> events;
    ws::SessionEvent event;
    while (session.Next(&event))
    {
        events.push_back(event);
    }
    return events;
}

void
TestSessionsTalk()
{
    ws::SessionOptions serverOptions;
    serverOptions.side = ws::Side::Server;
    ws::SessionOptions clientOptions;
    clientOptions.side = ws::Side::Client;
    clientOptions.host = "127.0.0.1:8765";
    ws::Session server(serverOptions);
    ws::Session client(clientOptions);

    Deliver(client, server);
    auto events = Drain(server);
    Check(events.size() == 1 && events[0].kind == ws::SessionEvent::Kind::Opened, "server opens");
    Deliver(server, client);
    events = Drain(client);
    Check(events.size() == 1 && events[0].kind == ws::SessionEvent::Kind::Opened, "client opens");

    client.SendText("{\"up\":1}");
    server.SendText("{\"down\":2}");
    Deliver(client, server);
    Deliver(server, client);
    events = Drain(server);
    Check(events.size() == 1 && events[0].text == "{\"up\":1}", "client to server");
    events = Drain(client);
    Check(events.size() == 1 && events[0].text == "{\"down\":2}", "server to client");

    client.Close(1000);
    Check(client.HasEnded(), "close ends the closing side");
    Deliver(client, server);
    events = Drain(server);
    Check(events.size() == 1 && events[0].kind == ws::SessionEvent::Kind::Closed &&
              events[0].closeStatus == 1000 && server.HasEnded(),
          "server sees the close");
    Check(!server.Outbound().empty(), "and owes the echo");
}

// ---------------------------------------------------------------------------
// The framing corpus
// ---------------------------------------------------------------------------

std::vector<std::string>
SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t'))
    {
        fields.push_back(field);
    }
    return fields;
}

std::string
CodeFor(const ws::SessionEvent& event)
{
    switch (event.kind)
    {
    case ws::SessionEvent::Kind::Refused:
        return event.refusal == ws::Refusal::Origin ? "WEBSOCKET_ORIGIN_REFUSED"
                                                    : "WEBSOCKET_HANDSHAKE_REFUSED";
    case ws::SessionEvent::Kind::Closed:
        return "WEBSOCKET_PEER_DISCONNECTED";
    case ws::SessionEvent::Kind::Fault:
        switch (event.fault)
        {
        case ws::FramingFault::MessageTooLarge:
            return "WEBSOCKET_MESSAGE_TOO_LARGE";
        case ws::FramingFault::UnsupportedMessage:
            return "WEBSOCKET_UNSUPPORTED_MESSAGE";
        default:
            return "WEBSOCKET_PROTOCOL_VIOLATION";
        }
    default:
        return "-";
    }
}

// What the session wrote back, read as frames: its pongs, and the status of
// its last close frame (0 when it sent none, 1005 for one without a status).
void
ReadOutbound(std::string outbound, ws::Side side, int* pongs, int* closeStatus)
{
    *pongs = 0;
    *closeStatus = 0;
    const std::size_t head = ws::FindHeadEnd(outbound);
    outbound.erase(0, head);
    // A server's frames are unmasked and a client's masked.
    ws::FrameReader reader(side == ws::Side::Client, std::size_t{1} << 20);
    reader.Append(outbound);
    ws::FrameEvent event;
    while (reader.Next(&event))
    {
        if (event.kind == ws::FrameEvent::Kind::Pong)
        {
            ++*pongs;
        }
        else if (event.kind == ws::FrameEvent::Kind::Close)
        {
            *closeStatus = event.closeStatus.value_or(1005);
        }
        else if (event.kind == ws::FrameEvent::Kind::Fault)
        {
            *closeStatus = -1;
        }
    }
}

int
RunCorpus(const std::filesystem::path& directory)
{
    std::ifstream expectations(directory / "expectations.txt");
    if (!expectations)
    {
        std::fprintf(stderr, "no expectations.txt in %s\n", directory.string().c_str());
        return 1;
    }
    std::set<std::string> expected;
    std::string line;
    int cases = 0;
    while (std::getline(expectations, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const auto fields = SplitTabs(line);
        if (fields.size() != 8)
        {
            Check(false, "malformed expectation: " + line);
            continue;
        }
        const std::string& file = fields[0];
        expected.insert(file);
        const ws::Side side = fields[1] == "client" ? ws::Side::Client : ws::Side::Server;

        transport::PacketCapture capture;
        transport::PacketCaptureError error;
        if (!transport::ReadPacketCaptureFile("!websocket-stream-capture",
                                              (directory / file).string(), &capture, &error))
        {
            Check(false, file + ": line " + std::to_string(error.line) + ": " + error.message);
            continue;
        }

        ws::SessionOptions options;
        options.side = side;
        options.path = "/";
        options.allowedOrigins = {"https://studio.example"};
        options.maxMessageBytes = 1024;
        options.host = "127.0.0.1:8765";
        options.clientKey = "dGhlIHNhbXBsZSBub25jZQ==";
        ws::Session session(options);
        const std::string request = session.Outbound();
        if (side == ws::Side::Client)
        {
            session.Outbound().clear();
        }

        int messages = 0;
        std::string result = "open";
        std::string code = "-";
        std::string status = "-";
        std::string byte = "-";
        for (const auto& record : capture.datagrams)
        {
            session.Receive(std::string_view(reinterpret_cast<const char*>(record.bytes.data()),
                                             record.bytes.size()));
            ws::SessionEvent event;
            while (session.Next(&event))
            {
                switch (event.kind)
                {
                case ws::SessionEvent::Kind::Opened:
                    break;
                case ws::SessionEvent::Kind::Message:
                    ++messages;
                    break;
                case ws::SessionEvent::Kind::Refused:
                    result = "refused";
                    code = CodeFor(event);
                    status = std::to_string(event.httpStatus);
                    break;
                case ws::SessionEvent::Kind::Closed:
                    result = "closed";
                    code = CodeFor(event);
                    status = std::to_string(event.closeStatus);
                    break;
                case ws::SessionEvent::Kind::Fault:
                    result = "fault";
                    code = CodeFor(event);
                    status = std::to_string(event.closeStatus);
                    byte = std::to_string(event.byte);
                    break;
                }
            }
        }

        const std::string got = std::to_string(messages) + " " + result + " " + code + " " +
                                status + " " + byte;
        const std::string want =
            fields[2] + " " + fields[4] + " " + fields[5] + " " + fields[6] + " " + fields[7];
        Check(got == want, file + ": got `" + got + "`, expected `" + want + "`");

        // What the session owes its peer must agree with what it reported.
        const std::string& outbound = session.Outbound();
        int pongs = 0;
        int closeStatus = 0;
        if (result == "refused")
        {
            const bool answered = side == ws::Side::Server && status != "0";
            Check(answered ? outbound.rfind("HTTP/1.1 " + status + " ", 0) == 0 : outbound.empty(),
                  file + ": the refusal's answer");
        }
        else
        {
            ReadOutbound(outbound, side, &pongs, &closeStatus);
            Check(std::to_string(pongs) == fields[3], file + ": pongs " + std::to_string(pongs));
            const int owed = result == "open" ? 0 : std::stoi(status);
            Check(closeStatus == owed, file + ": close sent " + std::to_string(closeStatus));
            if (side == ws::Side::Server)
            {
                Check(outbound.rfind("HTTP/1.1 101 ", 0) == 0, file + ": the 101 answer");
            }
        }
        if (side == ws::Side::Client)
        {
            Check(request.rfind("GET / HTTP/1.1\r\n", 0) == 0, file + ": the client's request");
        }
        ++cases;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".wsstream" && expected.count(name) == 0)
        {
            Check(false, name + " has no expectation");
        }
    }
    std::printf("framing corpus: %d stream(s)\n", cases);
    return cases > 0 ? 0 : 1;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1)
    {
        const int status = RunCorpus(argv[1]);
        return failures == 0 ? status : 1;
    }
    TestSha1();
    TestBase64();
    TestAcceptKey();
    TestUtf8();
    TestFrameLengths();
    TestDefaultBound();
    TestSessionsTalk();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("motionConnectorWebSocket rfc6455 tests passed\n");
    return 0;
}
