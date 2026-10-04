// SPDX-License-Identifier: Apache-2.0
//
// `WebSocketConnector` without a peer (WEBSOCKET_CONNECTOR.md §6–§9): the code
// table, `Open`'s refusals, and §7's assembly through `PushMessage`, the
// hardware-free path a replayed capture takes.
//
// With a directory, the message corpus: each capture is replayed through
// `PushMessage`, and the frames delivered and every diagnostic raised, in
// order, must be what `expectations.txt` says. The frames must also carry this
// connector's numbering and the capture's receive times, and nothing else may
// change.
#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/Diagnostics.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#include "motionConnectorWire/FrameWire.h"

#include "motionCore/MotionPose.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

namespace core = openstrata::connectors::core;
namespace motion = openstrata::motion;
namespace websocket = openstrata::connectors::websocket;
namespace wire = openstrata::connectors::wire;

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

std::vector<std::string>
Codes(const websocket::WebSocketConnector& connector)
{
    std::vector<std::string> codes;
    for (const auto& diagnostic : connector.GetDiagnostics())
    {
        codes.emplace_back(websocket::DiagnosticName(diagnostic));
    }
    return codes;
}

core::MotionFrame
BodyFrame(std::uint64_t number, double timestamp, const std::string& profile = "example.body.v1")
{
    core::MotionFrame frame;
    frame.frameNumber = number;
    frame.sourceProfile = profile;
    frame.timing.sourceTimestamp = timestamp;
    frame.timing.receiveTimestamp = 1000.0 + timestamp;
    frame.timing.sequence = number * 10;
    frame.timing.sourceClock = core::ClockDomain::Device;

    core::ActorFrame actor;
    actor.actor = "0";
    motion::MotionPose pose;
    pose.timestamp = timestamp;
    pose.localRotations[static_cast<std::size_t>(motion::HumanJoint::Hips)] =
        pxr::GfQuatf(1.0f, 0.0f, 0.0f, 0.0f);
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanJoint::Hips));
    actor.pose = pose;
    frame.actors.push_back(std::move(actor));
    return frame;
}

std::string
Encode(const core::MotionFrame& frame)
{
    std::string text;
    const bool encoded = wire::EncodeFrame(frame, &text);
    assert(encoded);
    (void)encoded;
    return text;
}

// A connector whose profile and buffer are set, opened in the connect role so
// that `Open` touches no network: the first attempt is `Poll`'s, and these
// tests poll only what they pushed, which the buffer hands back before any
// attempt is made.
void
OpenOffline(websocket::WebSocketConnector& connector, const std::string& profile)
{
    core::ConnectorConfig config;
    config.sourceProfile = profile;
    config.bufferMode = core::BufferMode::Lossless;
    config.bufferCapacity = 4096;
    websocket::WebSocketConfig socket;
    socket.role = websocket::WebSocketRole::Connect;
    socket.address = "127.0.0.1";
    socket.port = 9;
    const auto status = connector.Open(config, socket);
    Check(static_cast<bool>(status), "offline open: " + status.message);
}

// ---------------------------------------------------------------------------
// Unit cases
// ---------------------------------------------------------------------------

void
TestCodeTable()
{
    // §8's table, in its order: the enum order and the stable strings are
    // held together here.
    const char* names[] = {
        "WEBSOCKET_SOCKET_BIND_FAILED",   "WEBSOCKET_CONNECT_FAILED",
        "WEBSOCKET_HANDSHAKE_REFUSED",    "WEBSOCKET_ORIGIN_REFUSED",
        "WEBSOCKET_PEER_REFUSED",         "WEBSOCKET_PROTOCOL_VIOLATION",
        "WEBSOCKET_MESSAGE_TOO_LARGE",    "WEBSOCKET_UNSUPPORTED_MESSAGE",
        "WEBSOCKET_PEER_DISCONNECTED",    "WEBSOCKET_SOURCE_TIMEOUT",
        "WEBSOCKET_SOURCE_RESTARTED",     "WEBSOCKET_FRAME_GAP",
        "WEBSOCKET_TIMESTAMP_REGRESSION", "WEBSOCKET_PROFILE_MISMATCH",
    };
    Check(websocket::DiagnosticCodeCount == 14, "fourteen codes");
    for (std::size_t i = 0; i < websocket::DiagnosticCodeCount; ++i)
    {
        const auto code = static_cast<websocket::DiagnosticCode>(i);
        Check(websocket::DiagnosticCodeString(code) == names[i], std::string("name ") + names[i]);
        Check(websocket::FindDiagnosticCode(names[i]) == code, std::string("find ") + names[i]);
    }
    using S = websocket::DiagnosticSeverity;
    using C = websocket::DiagnosticCode;
    Check(websocket::DiagnosticDefaultSeverity(C::SocketBindFailed) == S::Error &&
              !websocket::DiagnosticIsRecoverable(C::SocketBindFailed),
          "a bind failure is the one fatal code");
    for (C info : {C::PeerRefused, C::PeerDisconnected, C::SourceRestarted, C::FrameGap})
    {
        Check(websocket::DiagnosticDefaultSeverity(info) == S::Info,
              std::string(websocket::DiagnosticCodeString(info)) + " is info");
    }
    for (std::size_t i = 1; i < websocket::DiagnosticCodeCount; ++i)
    {
        Check(websocket::DiagnosticIsRecoverable(static_cast<C>(i)),
              std::string(names[i]) + " is recoverable");
    }

    wire::WireError error;
    error.code = wire::WireErrorCode::KeyUnknown;
    error.subject = "$.comment";
    const auto passed = websocket::MakeWireDiagnostic(error);
    Check(websocket::DiagnosticName(passed) == "WIRE_KEY_UNKNOWN", "a codec code passes through");
    Check(websocket::FormatDiagnostic(passed).rfind(
              "[WIRE_KEY_UNKNOWN] warning recoverable subject=$.comment", 0) == 0,
          "and is formatted under its own name");
}

void
TestOpen()
{
    websocket::WebSocketConnector connector;
    Check(connector.GetState() == core::ConnectorState::Disconnected, "starts disconnected");

    const auto capabilities = connector.GetCapabilities();
    for (auto capability :
         {core::ConnectorCapability::Body, core::ConnectorCapability::Hands,
          core::ConnectorCapability::Face, core::ConnectorCapability::Eyes,
          core::ConnectorCapability::RootMotion, core::ConnectorCapability::Trackers,
          core::ConnectorCapability::SourceTimestamps, core::ConnectorCapability::Confidence,
          core::ConnectorCapability::MultipleActors})
    {
        Check(capabilities.Has(capability), "the wire carries every part");
    }
    Check(!capabilities.Has(core::ConnectorCapability::Controllers), "but no controllers");

    core::ConnectorConfig converting;
    converting.coordinateConversion = "y-up-to-z-up";
    Check(!connector.Open(converting) && connector.GetState() == core::ConnectorState::Error,
          "a coordinate conversion is refused");

    core::ConnectorConfig plain;
    websocket::WebSocketConfig oversized;
    oversized.maxMessageBytes = websocket::MaxMessageBytes + 1;
    Check(!connector.Open(plain, oversized), "a bound above 65 507 is refused");

    websocket::WebSocketConfig noPort;
    noPort.role = websocket::WebSocketRole::Connect;
    Check(!connector.Open(plain, noPort), "a connect role needs a port");

    websocket::WebSocketConfig hostname;
    hostname.role = websocket::WebSocketRole::Listen;
    hostname.address = "localhost";
    Check(!connector.Open(plain, hostname) && Codes(connector) ==
                                                  std::vector<std::string>{
                                                      "WEBSOCKET_SOCKET_BIND_FAILED"},
          "a hostname is refused without a lookup");
    connector.Close();
    Check(connector.GetState() == core::ConnectorState::Disconnected, "close disconnects");
}

void
TestRewritesAndCarries()
{
    websocket::WebSocketConnector connector;
    OpenOffline(connector, "");

    core::MotionFrame sent = BodyFrame(500, 2.0);
    Check(connector.PushMessage(Encode(sent), 7.25, "127.0.0.1:50000") == 1, "accepted");
    sent = BodyFrame(501, 2.1);
    Check(connector.PushMessage(Encode(sent), 7.5, "127.0.0.1:50000") == 1, "accepted again");

    core::MotionFrame first;
    core::MotionFrame second;
    Check(connector.Poll(first) && connector.Poll(second), "two frames");
    // What this connector rewrites (§7).
    Check(first.frameNumber == 1 && second.frameNumber == 2, "frameNumber is the connector's");
    Check(first.timing.receiveTimestamp == 7.25 && second.timing.receiveTimestamp == 7.5,
          "receiveTimestamp is the receiver's");
    // And everything it carries.
    Check(second.sourceProfile == "example.body.v1", "profile carried");
    Check(second.timing.sourceTimestamp == 2.1 && second.timing.sequence == 5010u &&
              second.timing.sourceClock == core::ClockDomain::Device,
          "source timing carried");
    Check(second.actors.size() == 1 && second.actors[0].actor == "0" && second.actors[0].pose &&
              *second.actors[0].pose == *sent.actors[0].pose,
          "the pose carried");
    Check(connector.GetDiagnostics().empty(), "nothing to report");
    Check(connector.GetState() == core::ConnectorState::Connected, "a frame connects");

    // A new Open starts the numbering again.
    OpenOffline(connector, "");
    Check(connector.PushMessage(Encode(BodyFrame(9, 3.0)), 1.0, "a") == 1, "after reopen");
    core::MotionFrame again;
    Check(connector.Poll(again) && again.frameNumber == 1, "numbering restarts at Open");
}

void
TestCapture()
{
    // Pushed messages are not recorded: a capture is of what was received
    // live, and a replay that recorded itself would grow a second copy.
    websocket::WebSocketConnector connector;
    OpenOffline(connector, "");
    connector.StartCapture();
    connector.PushMessage(Encode(BodyFrame(1, 0.1)), 0.5, "a");
    Check(connector.GetCapture().datagrams.empty(), "a push is not a capture");
}

// ---------------------------------------------------------------------------
// The message corpus
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

struct Expectation
{
    std::string profile;
    std::size_t delivered = 0;
    std::vector<std::pair<std::string, std::string>> diagnostics;
};

int
RunCorpus(const std::filesystem::path& directory)
{
    std::ifstream file(directory / "expectations.txt");
    if (!file)
    {
        std::fprintf(stderr, "no expectations.txt in %s\n", directory.string().c_str());
        return 1;
    }
    std::map<std::string, Expectation> expectations;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const auto fields = SplitTabs(line);
        if (fields.size() == 4 && fields[0] == "capture")
        {
            expectations[fields[1]].profile = fields[2] == "-" ? std::string() : fields[2];
            expectations[fields[1]].delivered = std::stoul(fields[3]);
        }
        else if (fields.size() == 4 && fields[0] == "diagnostic")
        {
            expectations[fields[1]].diagnostics.emplace_back(fields[2], fields[3]);
        }
        else
        {
            Check(false, "malformed expectation: " + line);
        }
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().extension() == ".websocketpackets" &&
            expectations.count(entry.path().filename().string()) == 0)
        {
            Check(false, entry.path().filename().string() + " has no expectation");
        }
    }

    for (const auto& [name, expectation] : expectations)
    {
        websocket::PacketCapture capture;
        websocket::PacketCaptureError error;
        if (!websocket::ReadPacketCaptureFile((directory / name).string(), &capture, &error))
        {
            Check(false, name + ": line " + std::to_string(error.line) + ": " + error.message);
            continue;
        }

        websocket::WebSocketConnector connector;
        OpenOffline(connector, expectation.profile);
        std::size_t delivered = 0;
        std::vector<double> times;
        std::vector<std::string> profiles;
        for (const auto& record : capture.datagrams)
        {
            const std::string text(record.bytes.begin(), record.bytes.end());
            const std::size_t accepted =
                connector.PushMessage(text, record.receiveTime, record.peer);
            delivered += accepted;
            if (accepted)
            {
                times.push_back(record.receiveTime);
                core::MotionFrame sent;
                wire::DecodeFrame(text, &sent);
                profiles.push_back(sent.sourceProfile);
            }
        }
        Check(delivered == expectation.delivered,
              name + ": delivered " + std::to_string(delivered) + ", expected " +
                  std::to_string(expectation.delivered));

        std::vector<std::pair<std::string, std::string>> raised;
        for (const auto& diagnostic : connector.GetDiagnostics())
        {
            raised.emplace_back(std::string(websocket::DiagnosticName(diagnostic)),
                                diagnostic.subject);
        }
        if (raised != expectation.diagnostics)
        {
            Check(false, name + ": diagnostics differ");
            for (const auto& [code, subject] : raised)
            {
                std::fprintf(stderr, "  raised   %s %s\n", code.c_str(), subject.c_str());
            }
            for (const auto& [code, subject] : expectation.diagnostics)
            {
                std::fprintf(stderr, "  expected %s %s\n", code.c_str(), subject.c_str());
            }
        }

        // Polled exactly as many times as frames were accepted, so the buffer
        // answers every call and no connect is attempted.
        for (std::size_t index = 0; index < delivered && index < times.size(); ++index)
        {
            core::MotionFrame frame;
            if (!connector.Poll(frame))
            {
                Check(false, name + ": fewer frames than accepted");
                break;
            }
            Check(frame.frameNumber == index + 1, name + ": the connector's numbering");
            Check(frame.timing.receiveTimestamp == times[index], name + ": the capture's time");
            Check(frame.sourceProfile == profiles[index], name + ": the sender's profile");
        }
    }
    std::printf("message corpus: %zu capture(s)\n", expectations.size());
    return expectations.empty() ? 1 : 0;
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
    TestCodeTable();
    TestOpen();
    TestRewritesAndCarries();
    TestCapture();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("motionConnectorWebSocket connector tests passed\n");
    return 0;
}
