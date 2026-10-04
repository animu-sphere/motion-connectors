// SPDX-License-Identifier: Apache-2.0

#include "Rfc6455.h"

#include "motionConnectorWebSocket/WebSocketConfig.h"

#include <algorithm>
#include <cstring>

namespace openstrata::connectors::websocket::internal
{

namespace
{

// RFC 6455 §1.3.
constexpr std::string_view kAcceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::uint32_t
RotateLeft(std::uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

char
LowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string
Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
    {
        c = LowerAscii(c);
    }
    return out;
}

std::string_view
Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

bool
ContainsToken(const std::vector<std::string>& tokens, std::string_view lowerToken)
{
    return std::any_of(tokens.begin(), tokens.end(),
                       [&](const std::string& token) { return Lower(token) == lowerToken; });
}

std::size_t
CountHeader(const HttpHead& head, std::string_view lowerName)
{
    return static_cast<std::size_t>(
        std::count_if(head.headers.begin(), head.headers.end(),
                      [&](const auto& header) { return header.first == lowerName; }));
}

// A token character (RFC 9110 §5.6.2), for a header name.
bool
IsTokenChar(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    {
        return true;
    }
    return std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

// Splits a head into its start line and headers. Obsolete line folding, a
// header line without a colon and a name that is not a token are refused.
bool
ParseHeadLines(std::string_view head, std::string* startLine, HttpHead* out)
{
    if (head.size() < 4 || head.substr(head.size() - 4) != "\r\n\r\n")
    {
        return false;
    }
    head.remove_suffix(2);
    std::size_t lineEnd = head.find("\r\n");
    *startLine = std::string(head.substr(0, lineEnd));
    head.remove_prefix(lineEnd + 2);
    while (!head.empty())
    {
        lineEnd = head.find("\r\n");
        const std::string_view line = head.substr(0, lineEnd);
        head.remove_prefix(lineEnd + 2);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
        {
            return false;
        }
        const std::string_view name = line.substr(0, colon);
        if (!std::all_of(name.begin(), name.end(), IsTokenChar))
        {
            return false;
        }
        out->headers.emplace_back(Lower(name), std::string(Trim(line.substr(colon + 1))));
    }
    return startLine->find('\n') == std::string::npos &&
           startLine->find('\r') == std::string::npos;
}

std::string_view
ReasonPhrase(int status)
{
    switch (status)
    {
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

UpgradeAnswer
Refuse(Refusal refusal, int status, std::string detail)
{
    UpgradeAnswer answer;
    answer.refusal = refusal;
    answer.status = status;
    answer.response = WriteHttpRefusal(status);
    answer.detail = std::move(detail);
    return answer;
}

bool
CloseStatusIsValid(std::uint16_t status)
{
    // RFC 6455 §7.4 and the IANA registry: 1004–1006 and 1015 may not be
    // sent, and 1016–2999 are reserved.
    return (status >= 1000 && status <= 1003) || (status >= 1007 && status <= 1014) ||
           (status >= 3000 && status <= 4999);
}

} // namespace

// ---------------------------------------------------------------------------
// SHA-1, base64, UTF-8
// ---------------------------------------------------------------------------

std::array<std::uint8_t, 20>
Sha1(std::string_view data)
{
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    std::string message(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56)
    {
        message.push_back('\0');
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<char>((bits >> shift) & 0xffu));
    }

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64)
    {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            const auto* p = reinterpret_cast<const unsigned char*>(message.data() + chunk + i * 4);
            w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                   (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
        }
        for (int i = 16; i < 80; ++i)
        {
            w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f;
            std::uint32_t k;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotateLeft(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::array<std::uint8_t, 20> digest{};
    for (int i = 0; i < 5; ++i)
    {
        digest[i * 4 + 0] = static_cast<std::uint8_t>(h[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return digest;
}

std::string
Base64Encode(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3)
    {
        const std::uint32_t v = (std::uint32_t(std::uint8_t(bytes[i])) << 16) |
                                (std::uint32_t(std::uint8_t(bytes[i + 1])) << 8) |
                                std::uint32_t(std::uint8_t(bytes[i + 2]));
        out += kBase64Alphabet[(v >> 18) & 63];
        out += kBase64Alphabet[(v >> 12) & 63];
        out += kBase64Alphabet[(v >> 6) & 63];
        out += kBase64Alphabet[v & 63];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1)
    {
        const std::uint32_t v = std::uint32_t(std::uint8_t(bytes[i])) << 16;
        out += kBase64Alphabet[(v >> 18) & 63];
        out += kBase64Alphabet[(v >> 12) & 63];
        out += "==";
    }
    else if (rest == 2)
    {
        const std::uint32_t v = (std::uint32_t(std::uint8_t(bytes[i])) << 16) |
                                (std::uint32_t(std::uint8_t(bytes[i + 1])) << 8);
        out += kBase64Alphabet[(v >> 18) & 63];
        out += kBase64Alphabet[(v >> 12) & 63];
        out += kBase64Alphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::optional<std::string>
Base64Decode(std::string_view text)
{
    if (text.size() % 4 != 0)
    {
        return std::nullopt;
    }
    std::size_t padding = 0;
    if (!text.empty() && text.back() == '=')
    {
        ++padding;
        if (text.size() >= 2 && text[text.size() - 2] == '=')
        {
            ++padding;
        }
    }
    std::string out;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (std::size_t i = 0; i < text.size() - padding; ++i)
    {
        const std::size_t value = kBase64Alphabet.find(text[i]);
        if (value == std::string_view::npos)
        {
            return std::nullopt;
        }
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += static_cast<char>((accumulator >> bits) & 0xffu);
        }
    }
    return out;
}

std::string
AcceptKey(std::string_view clientKey)
{
    std::string input(clientKey);
    input += kAcceptGuid;
    const auto digest = Sha1(input);
    return Base64Encode(
        std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

bool
IsUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        std::size_t length;
        std::uint32_t codepoint;
        if (c >= 0xC2 && c <= 0xDF)
        {
            length = 2;
            codepoint = c & 0x1Fu;
        }
        else if (c >= 0xE0 && c <= 0xEF)
        {
            length = 3;
            codepoint = c & 0x0Fu;
        }
        else if (c >= 0xF0 && c <= 0xF4)
        {
            length = 4;
            codepoint = c & 0x07u;
        }
        else
        {
            return false;
        }
        if (i + length > text.size())
        {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u)
            {
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        {
            return false;
        }
        i += length;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The opening handshake
// ---------------------------------------------------------------------------

const std::string*
HttpHead::Find(std::string_view lowerName) const
{
    for (const auto& header : headers)
    {
        if (header.first == lowerName)
        {
            return &header.second;
        }
    }
    return nullptr;
}

std::vector<std::string>
HttpHead::Tokens(std::string_view lowerName) const
{
    std::vector<std::string> tokens;
    for (const auto& header : headers)
    {
        if (header.first != lowerName)
        {
            continue;
        }
        std::string_view rest = header.second;
        while (!rest.empty())
        {
            const std::size_t comma = rest.find(',');
            const std::string_view token = Trim(rest.substr(0, comma));
            if (!token.empty())
            {
                tokens.emplace_back(token);
            }
            if (comma == std::string_view::npos)
            {
                break;
            }
            rest.remove_prefix(comma + 1);
        }
    }
    return tokens;
}

std::size_t
FindHeadEnd(std::string_view buffer)
{
    const std::size_t end = buffer.find("\r\n\r\n");
    return end == std::string_view::npos ? 0 : end + 4;
}

bool
ParseRequestHead(std::string_view head, HttpHead* out)
{
    HttpHead parsed;
    std::string line;
    if (!ParseHeadLines(head, &line, &parsed))
    {
        return false;
    }
    const std::size_t first = line.find(' ');
    const std::size_t second =
        first == std::string::npos ? std::string::npos : line.find(' ', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        line.find(' ', second + 1) != std::string::npos)
    {
        return false;
    }
    parsed.method = line.substr(0, first);
    parsed.target = line.substr(first + 1, second - first - 1);
    if (parsed.method.empty() || parsed.target.empty() || line.substr(second + 1) != "HTTP/1.1")
    {
        return false;
    }
    *out = std::move(parsed);
    return true;
}

bool
ParseResponseHead(std::string_view head, HttpHead* out)
{
    HttpHead parsed;
    std::string line;
    if (!ParseHeadLines(head, &line, &parsed))
    {
        return false;
    }
    // "HTTP/1.1 101 Switching Protocols": the reason phrase may be empty.
    if (line.size() < 12 || line.compare(0, 9, "HTTP/1.1 ") != 0 ||
        (line.size() > 12 && line[12] != ' '))
    {
        return false;
    }
    int status = 0;
    for (std::size_t i = 9; i < 12; ++i)
    {
        if (line[i] < '0' || line[i] > '9')
        {
            return false;
        }
        status = status * 10 + (line[i] - '0');
    }
    parsed.status = status;
    *out = std::move(parsed);
    return true;
}

UpgradeAnswer
AnswerUpgrade(std::string_view head, std::string_view path,
              const std::vector<std::string>& allowedOrigins)
{
    HttpHead request;
    if (!ParseRequestHead(head, &request))
    {
        return Refuse(Refusal::Handshake, 400, "the request head is not an HTTP/1.1 request");
    }

    // The origin first: a page that is not let in learns nothing else about
    // this endpoint, not even whether its path exists (§4.2).
    if (const std::string* origin = request.Find("origin"))
    {
        if (CountHeader(request, "origin") != 1 ||
            std::find(allowedOrigins.begin(), allowedOrigins.end(), *origin) ==
                allowedOrigins.end())
        {
            UpgradeAnswer answer = Refuse(Refusal::Origin, 403,
                                          "the request's Origin is not on the allow list");
            answer.origin = *origin;
            return answer;
        }
    }

    if (request.target != path)
    {
        return Refuse(Refusal::Handshake, 404,
                      "the request target " + request.target + " is not the configured path");
    }
    if (request.method != "GET")
    {
        return Refuse(Refusal::Handshake, 400, "the request is not a GET");
    }
    if (!ContainsToken(request.Tokens("upgrade"), "websocket") ||
        !ContainsToken(request.Tokens("connection"), "upgrade"))
    {
        return Refuse(Refusal::Handshake, 400, "the request is not a WebSocket upgrade");
    }
    if (CountHeader(request, "host") != 1)
    {
        return Refuse(Refusal::Handshake, 400, "the request does not carry one Host");
    }
    const std::string* version = request.Find("sec-websocket-version");
    if (!version || CountHeader(request, "sec-websocket-version") != 1 || *version != "13")
    {
        return Refuse(Refusal::Handshake, 400, "the request does not ask for version 13");
    }
    const std::string* key = request.Find("sec-websocket-key");
    const std::optional<std::string> nonce =
        key ? Base64Decode(*key) : std::optional<std::string>();
    if (!key || CountHeader(request, "sec-websocket-key") != 1 || !nonce || nonce->size() != 16)
    {
        return Refuse(Refusal::Handshake, 400,
                      "the request's Sec-WebSocket-Key is not 16 base64 bytes");
    }
    const std::vector<std::string> offered = request.Tokens("sec-websocket-protocol");
    if (std::find(offered.begin(), offered.end(), Subprotocol) == offered.end())
    {
        return Refuse(Refusal::Handshake, 400,
                      "the request does not offer the subprotocol " + std::string(Subprotocol));
    }

    // Extensions are ignored and never echoed (§4.3).
    UpgradeAnswer answer;
    answer.response = "HTTP/1.1 101 Switching Protocols\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: " +
                      AcceptKey(*key) +
                      "\r\n"
                      "Sec-WebSocket-Protocol: " +
                      std::string(Subprotocol) + "\r\n\r\n";
    if (const std::string* origin = request.Find("origin"))
    {
        answer.origin = *origin;
    }
    return answer;
}

std::string
WriteHttpRefusal(int status)
{
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " +
                           std::string(ReasonPhrase(status)) + "\r\n";
    if (status == 400)
    {
        // RFC 6455 §4.4: say which version this server speaks.
        response += "Sec-WebSocket-Version: 13\r\n";
    }
    response += "Connection: close\r\nContent-Length: 0\r\n\r\n";
    return response;
}

std::string
WriteUpgradeRequest(std::string_view host, std::string_view path, std::string_view key)
{
    std::string request = "GET ";
    request += path;
    request += " HTTP/1.1\r\nHost: ";
    request += host;
    request += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
    request += key;
    request += "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: ";
    request += Subprotocol;
    request += "\r\n\r\n";
    return request;
}

bool
CheckUpgradeResponse(std::string_view head, std::string_view key, std::string* detail,
                     int* status)
{
    const auto fail = [&](std::string text)
    {
        if (detail)
        {
            *detail = std::move(text);
        }
        return false;
    };
    HttpHead response;
    if (!ParseResponseHead(head, &response))
    {
        return fail("the response head is not an HTTP/1.1 response");
    }
    if (status)
    {
        *status = response.status;
    }
    if (response.status != 101)
    {
        return fail("the server answered " + std::to_string(response.status));
    }
    if (!ContainsToken(response.Tokens("upgrade"), "websocket") ||
        !ContainsToken(response.Tokens("connection"), "upgrade"))
    {
        return fail("the response is not a WebSocket upgrade");
    }
    const std::string* accept = response.Find("sec-websocket-accept");
    if (!accept || *accept != AcceptKey(key))
    {
        return fail("the response's Sec-WebSocket-Accept does not answer the key");
    }
    const std::string* selected = response.Find("sec-websocket-protocol");
    if (!selected || CountHeader(response, "sec-websocket-protocol") != 1 ||
        *selected != Subprotocol)
    {
        return fail("the response does not select the subprotocol " + std::string(Subprotocol));
    }
    if (response.Find("sec-websocket-extensions"))
    {
        return fail("the response selects an extension that was not offered");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

void
AppendFrame(std::string* out, Opcode opcode, std::string_view payload, bool fin,
            const MaskKey* mask)
{
    out->push_back(static_cast<char>((fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(opcode)));
    const std::uint8_t maskBit = mask ? 0x80 : 0x00;
    const std::uint64_t length = payload.size();
    if (length < 126)
    {
        out->push_back(static_cast<char>(maskBit | length));
    }
    else if (length <= 0xFFFF)
    {
        out->push_back(static_cast<char>(maskBit | 126));
        out->push_back(static_cast<char>((length >> 8) & 0xff));
        out->push_back(static_cast<char>(length & 0xff));
    }
    else
    {
        out->push_back(static_cast<char>(maskBit | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            out->push_back(static_cast<char>((length >> shift) & 0xff));
        }
    }
    if (!mask)
    {
        out->append(payload);
        return;
    }
    out->append(reinterpret_cast<const char*>(mask->data()), mask->size());
    const std::size_t start = out->size();
    out->append(payload);
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        (*out)[start + i] = static_cast<char>((*out)[start + i] ^ (*mask)[i % 4]);
    }
}

std::string
ClosePayload(std::uint16_t status, std::string_view reason)
{
    std::string payload;
    payload.push_back(static_cast<char>(status >> 8));
    payload.push_back(static_cast<char>(status & 0xff));
    payload.append(reason);
    return payload;
}

std::uint16_t
CloseStatusFor(FramingFault fault) noexcept
{
    switch (fault)
    {
    case FramingFault::ProtocolViolation:
        return CloseProtocolError;
    case FramingFault::InvalidUtf8:
        return CloseInvalidPayload;
    case FramingFault::MessageTooLarge:
        return CloseMessageTooBig;
    case FramingFault::UnsupportedMessage:
        return CloseUnsupportedData;
    }
    return CloseProtocolError;
}

FrameReader::FrameReader(bool expectMasked, std::size_t maxMessageBytes, std::uint64_t baseOffset)
    : _expectMasked(expectMasked), _maxMessageBytes(maxMessageBytes), _offset(baseOffset)
{
}

void
FrameReader::Append(std::string_view bytes)
{
    if (_stopped)
    {
        return;
    }
    if (_read > 0)
    {
        _buffer.erase(0, _read);
        _read = 0;
    }
    _buffer.append(bytes);
}

bool
FrameReader::_Fault(FrameEvent* event, FramingFault fault, std::uint64_t byte, std::string detail)
{
    _stopped = true;
    _buffer.clear();
    _read = 0;
    _message.clear();
    *event = FrameEvent();
    event->kind = FrameEvent::Kind::Fault;
    event->fault = fault;
    event->byte = byte;
    event->detail = std::move(detail);
    return true;
}

bool
FrameReader::Next(FrameEvent* event)
{
    using FF = FramingFault;
    while (!_stopped)
    {
        const std::size_t available = _buffer.size() - _read;
        if (available < 2)
        {
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(_buffer.data() + _read);
        const std::uint64_t frameStart = _offset;

        const bool fin = (bytes[0] & 0x80) != 0;
        const std::uint8_t opcode = bytes[0] & 0x0F;
        const bool masked = (bytes[1] & 0x80) != 0;
        const std::uint8_t length7 = bytes[1] & 0x7F;

        if ((bytes[0] & 0x70) != 0)
        {
            return _Fault(event, FF::ProtocolViolation, frameStart, "a reserved bit is set");
        }
        const bool known = opcode <= 0x2 || (opcode >= 0x8 && opcode <= 0xA);
        if (!known)
        {
            return _Fault(event, FF::ProtocolViolation, frameStart,
                          "opcode " + std::to_string(opcode) + " is not defined");
        }
        const bool control = opcode >= 0x8;
        if (control && !fin)
        {
            return _Fault(
                event, FF::ProtocolViolation, frameStart, "a control frame is fragmented");
        }
        if (control && length7 > 125)
        {
            return _Fault(event, FF::ProtocolViolation, frameStart,
                          "a control frame is over 125 bytes");
        }
        if (masked != _expectMasked)
        {
            return _Fault(event, FF::ProtocolViolation, frameStart,
                          _expectMasked ? "a client frame is not masked"
                                        : "a server frame is masked");
        }

        std::size_t header = 2 + (length7 == 126 ? 2 : length7 == 127 ? 8 : 0) + (masked ? 4 : 0);
        if (available < header)
        {
            return false;
        }
        std::uint64_t length = length7;
        if (length7 == 126)
        {
            length = (std::uint64_t(bytes[2]) << 8) | bytes[3];
            if (length < 126)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a 16-bit length of " + std::to_string(length) +
                                  " is not minimally encoded");
            }
        }
        else if (length7 == 127)
        {
            if ((bytes[2] & 0x80) != 0)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a 64-bit length has its top bit set");
            }
            length = 0;
            for (int i = 0; i < 8; ++i)
            {
                length = (length << 8) | bytes[2 + i];
            }
            if (length <= 0xFFFF)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a 64-bit length of " + std::to_string(length) +
                                  " is not minimally encoded");
            }
        }

        if (!control)
        {
            const auto op = static_cast<Opcode>(opcode);
            if (op == Opcode::Continuation && !_inMessage)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a continuation frame has nothing to continue");
            }
            if (op != Opcode::Continuation && _inMessage)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a new message began before the last one finished");
            }
            if (op == Opcode::Binary)
            {
                return _Fault(event, FF::UnsupportedMessage, frameStart,
                              "a binary message; version 1 negotiates no binary encoding");
            }
            const std::size_t held = _inMessage ? _message.size() : 0;
            if (length > _maxMessageBytes - held)
            {
                return _Fault(event, FF::MessageTooLarge, frameStart,
                              "a message of at least " + std::to_string(held + length) +
                                  " bytes would exceed the " + std::to_string(_maxMessageBytes) +
                                  "-byte bound");
            }
        }

        if (available - header < length)
        {
            return false;
        }

        std::string payload(_buffer.data() + _read + header, static_cast<std::size_t>(length));
        if (masked)
        {
            const std::uint8_t* key = bytes + header - 4;
            for (std::size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<char>(payload[i] ^ key[i % 4]);
            }
        }
        _read += header + static_cast<std::size_t>(length);
        _offset += header + length;

        if (control)
        {
            *event = FrameEvent();
            event->byte = frameStart;
            switch (static_cast<Opcode>(opcode))
            {
            case Opcode::Ping:
                event->kind = FrameEvent::Kind::Ping;
                event->payload = std::move(payload);
                return true;
            case Opcode::Pong:
                event->kind = FrameEvent::Kind::Pong;
                event->payload = std::move(payload);
                return true;
            default:
                break;
            }
            // A close frame.
            if (payload.size() == 1)
            {
                return _Fault(event, FF::ProtocolViolation, frameStart,
                              "a close frame carries one byte of status");
            }
            if (payload.size() >= 2)
            {
                const auto status = static_cast<std::uint16_t>(
                    (std::uint16_t(std::uint8_t(payload[0])) << 8) | std::uint8_t(payload[1]));
                if (!CloseStatusIsValid(status))
                {
                    return _Fault(event, FF::ProtocolViolation, frameStart,
                                  "close status " + std::to_string(status) + " may not be sent");
                }
                if (!IsUtf8(std::string_view(payload).substr(2)))
                {
                    return _Fault(event, FF::InvalidUtf8, frameStart,
                                  "a close reason is not UTF-8");
                }
                event->closeStatus = status;
                event->payload = payload.substr(2);
            }
            event->kind = FrameEvent::Kind::Close;
            _stopped = true;
            _buffer.clear();
            _read = 0;
            return true;
        }

        _message += payload;
        _inMessage = !fin;
        if (!fin)
        {
            continue;
        }
        if (!IsUtf8(_message))
        {
            return _Fault(event, FF::InvalidUtf8, frameStart, "a text message is not UTF-8");
        }
        *event = FrameEvent();
        event->kind = FrameEvent::Kind::Message;
        event->byte = frameStart;
        event->payload = std::move(_message);
        _message.clear();
        return true;
    }
    return false;
}

} // namespace openstrata::connectors::websocket::internal
