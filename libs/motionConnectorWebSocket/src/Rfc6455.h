// SPDX-License-Identifier: Apache-2.0
//
// The RFC 6455 subset this repository speaks (docs/design/WEBSOCKET_CONNECTOR.md
// §4, §5), on bytes alone: the accept key and what it needs (SHA-1, base64),
// the opening handshake's request and response, frame headers, masking and
// reassembly. Nothing here opens a socket or reads a clock, so a test drives it
// with byte strings, as `motionConnectorOsc` is driven.
//
// It is private to this library (§10). It becomes a library of its own when a
// second component needs it, and not before.
//
// **Every refusal is a property of the parse.** A non-minimal length, an
// unmasked client frame and a declared length over the bound are seen in a
// frame's header, before its payload is read (§2). That is why this parser is
// the repository's own.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openstrata::connectors::websocket::internal
{

// ---------------------------------------------------------------------------
// What the accept key needs.
// ---------------------------------------------------------------------------

std::array<std::uint8_t, 20> Sha1(std::string_view data);

std::string Base64Encode(std::string_view bytes);
// Strict: padded, no whitespace, no other alphabet. Empty on a refusal, which a
// caller tells from an empty input by the input.
std::optional<std::string> Base64Decode(std::string_view text);

// RFC 6455 §4.2.2: base64(SHA-1(key + the GUID)).
std::string AcceptKey(std::string_view clientKey);

// Well-formed UTF-8: no overlong form, no surrogate, nothing above U+10FFFF.
bool IsUtf8(std::string_view text);

// ---------------------------------------------------------------------------
// The opening handshake (§4).
// ---------------------------------------------------------------------------

struct HttpHead
{
    // A request's start line.
    std::string method;
    std::string target;
    // A response's start line.
    int status = 0;
    // Header names lower-cased, values with surrounding whitespace removed, in
    // arrival order. A repeated header is kept twice.
    std::vector<std::pair<std::string, std::string>> headers;

    // The first value of `lowerName`, or null.
    const std::string* Find(std::string_view lowerName) const;
    // Every comma-separated token of every `lowerName` header, trimmed.
    std::vector<std::string> Tokens(std::string_view lowerName) const;
};

// The length of the head in `buffer`, the blank line included, or 0 while the
// blank line has not arrived.
std::size_t FindHeadEnd(std::string_view buffer);

bool ParseRequestHead(std::string_view head, HttpHead* out);
bool ParseResponseHead(std::string_view head, HttpHead* out);

enum class Refusal : std::uint8_t
{
    None,
    // WEBSOCKET_HANDSHAKE_REFUSED.
    Handshake,
    // WEBSOCKET_ORIGIN_REFUSED.
    Origin,
};

// A listener's answer to one request head.
struct UpgradeAnswer
{
    Refusal refusal = Refusal::None;
    // 101 when accepted; 400, 403 or 404 otherwise (§4).
    int status = 101;
    // The bytes to write back: the 101 response, or the refusal.
    std::string response;
    // The request's `Origin`, when it carried one.
    std::string origin;
    std::string detail;
};

UpgradeAnswer AnswerUpgrade(std::string_view head, std::string_view path,
                            const std::vector<std::string>& allowedOrigins);

// An HTTP error response with no body, which closes the connection.
std::string WriteHttpRefusal(int status);

// A connect role's request. `host` is the Host header's value.
std::string WriteUpgradeRequest(std::string_view host, std::string_view path,
                                std::string_view key);

// Whether a response head completes the upgrade `key` asked for: 101, the
// upgrade headers, the accept key, the subprotocol selected and no extension.
// `status` receives the response's status when it parsed.
bool CheckUpgradeResponse(std::string_view head, std::string_view key, std::string* detail,
                          int* status);

// ---------------------------------------------------------------------------
// Framing (§5).
// ---------------------------------------------------------------------------

enum class Opcode : std::uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

using MaskKey = std::array<std::uint8_t, 4>;

// Appends one frame to `out`. With `mask`, the payload is masked, as every
// client-to-server frame must be.
void AppendFrame(std::string* out, Opcode opcode, std::string_view payload, bool fin = true,
                 const MaskKey* mask = nullptr);

// A close frame's payload: the status, big-endian, then the reason.
std::string ClosePayload(std::uint16_t status, std::string_view reason = {});

// The status codes §5 closes with.
inline constexpr std::uint16_t CloseNormal = 1000;
inline constexpr std::uint16_t CloseGoingAway = 1001;
inline constexpr std::uint16_t CloseProtocolError = 1002;
inline constexpr std::uint16_t CloseUnsupportedData = 1003;
// Never sent: what a close frame without a status reports (RFC 6455 §7.1.5).
inline constexpr std::uint16_t CloseNoStatus = 1005;
inline constexpr std::uint16_t CloseInvalidPayload = 1007;
inline constexpr std::uint16_t CloseMessageTooBig = 1009;

enum class FramingFault : std::uint8_t
{
    // 1002, WEBSOCKET_PROTOCOL_VIOLATION.
    ProtocolViolation,
    // 1007, WEBSOCKET_PROTOCOL_VIOLATION.
    InvalidUtf8,
    // 1009, WEBSOCKET_MESSAGE_TOO_LARGE.
    MessageTooLarge,
    // 1003, WEBSOCKET_UNSUPPORTED_MESSAGE.
    UnsupportedMessage,
};

std::uint16_t CloseStatusFor(FramingFault fault) noexcept;

struct FrameEvent
{
    enum class Kind : std::uint8_t
    {
        // A complete text message, valid UTF-8.
        Message,
        Ping,
        Pong,
        // A close frame; nothing after it is read.
        Close,
        // A framing error; nothing after it is read (§5: a framing error ends
        // the connection, not just the message).
        Fault,
    };

    Kind kind = Kind::Message;
    // The message text, the ping or pong payload, or the close reason.
    std::string payload;
    // A close frame's status, when it carried one.
    std::optional<std::uint16_t> closeStatus;
    FramingFault fault = FramingFault::ProtocolViolation;
    // The stream offset of the first byte of the frame that completed this
    // event or was refused: `byte N` in a diagnostic.
    std::uint64_t byte = 0;
    std::string detail;
};

// Frames in, events out. `expectMasked` is true on a server, which reads
// client frames, and false on a client.
class FrameReader
{
  public:
    FrameReader(bool expectMasked, std::size_t maxMessageBytes, std::uint64_t baseOffset = 0);

    void Append(std::string_view bytes);

    // The next event, or false while it needs more bytes, and always after a
    // close or a fault.
    bool Next(FrameEvent* event);

    bool
    Stopped() const noexcept
    {
        return _stopped;
    }

  private:
    bool _Fault(FrameEvent* event, FramingFault fault, std::uint64_t byte, std::string detail);

    bool _expectMasked;
    std::size_t _maxMessageBytes;
    // Unread bytes start at `_buffer[_read]`, which is stream offset `_offset`.
    std::string _buffer;
    std::size_t _read = 0;
    std::uint64_t _offset;
    // A fragmented message in progress.
    bool _inMessage = false;
    std::string _message;
    bool _stopped = false;
};

} // namespace openstrata::connectors::websocket::internal
