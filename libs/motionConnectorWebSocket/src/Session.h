// SPDX-License-Identifier: Apache-2.0
//
// One WebSocket connection's protocol state, from its first inbound byte to its
// end, with no socket and no clock: the opening handshake (§4), then framing
// (§5). Bytes go in through `Receive`, events come out of `Next`, and the bytes
// the connection owes its peer -- the handshake answer, a pong, a close -- wait
// in `Outbound` for whoever owns the socket.
//
// This is what the framing corpus drives: a byte stream in, chunked as reads
// would chunk it, and the events and close status that come out. The socket
// layer adds only time (the handshake timeout) and who the peer is.
#pragma once

#include "Rfc6455.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace openstrata::connectors::websocket::internal
{

enum class Side : std::uint8_t
{
    // Accepts an upgrade and reads masked client frames.
    Server,
    // Sends an upgrade and reads unmasked server frames.
    Client,
};

struct SessionOptions
{
    Side side = Side::Server;
    std::string path = "/";
    std::vector<std::string> allowedOrigins;
    std::size_t maxMessageBytes = 65507;
    // The client's Host header.
    std::string host;
    // The client's Sec-WebSocket-Key. Empty draws 16 random bytes; a test sets
    // it, so a recorded response can answer it.
    std::string clientKey;
};

struct SessionEvent
{
    enum class Kind : std::uint8_t
    {
        // The handshake completed.
        Opened,
        // A text message.
        Message,
        // The handshake failed. The session has ended.
        Refused,
        // A framing error. A close frame is owed and the session has ended.
        Fault,
        // The peer sent a close frame, which has been echoed. The session has
        // ended.
        Closed,
    };

    Kind kind = Kind::Message;
    std::string text;
    // Refused.
    Refusal refusal = Refusal::None;
    // Refused: the HTTP status sent (server) or received (client); 0 when
    // nothing was, as for a head over the bound.
    int httpStatus = 0;
    // Refused: the request's Origin.
    std::string origin;
    // Fault.
    FramingFault fault = FramingFault::ProtocolViolation;
    // Fault: the close status sent. Closed: the peer's, or 1005 for none.
    std::uint16_t closeStatus = 0;
    // Fault: the stream offset the refusal names, counted from the
    // connection's first inbound byte.
    std::uint64_t byte = 0;
    std::string detail;
};

class Session
{
  public:
    explicit Session(SessionOptions options);

    void Receive(std::string_view bytes);
    bool Next(SessionEvent* event);

    // Bytes owed to the peer. The caller erases what it wrote.
    std::string&
    Outbound() noexcept
    {
        return _outbound;
    }

    bool
    IsOpen() const noexcept
    {
        return _phase == Phase::Open;
    }
    bool
    HasEnded() const noexcept
    {
        return _phase == Phase::Ended;
    }

    // Queues one text message; masked on a client. Ignored unless open.
    void SendText(std::string_view text);
    // Queues a close frame and ends the session.
    void Close(std::uint16_t status);

  private:
    enum class Phase : std::uint8_t
    {
        Handshake,
        Open,
        Ended,
    };

    void _AppendFrame(Opcode opcode, std::string_view payload);

    SessionOptions _options;
    Phase _phase = Phase::Handshake;
    std::string _inbound;
    std::string _outbound;
    std::optional<FrameReader> _reader;
    std::mt19937 _random;
};

} // namespace openstrata::connectors::websocket::internal
