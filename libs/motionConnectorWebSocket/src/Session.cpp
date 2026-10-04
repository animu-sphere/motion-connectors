// SPDX-License-Identifier: Apache-2.0

#include "Session.h"

#include "motionConnectorWebSocket/WebSocketConfig.h"

#include <utility>

namespace openstrata::connectors::websocket::internal
{

namespace
{

std::mt19937
SeededGenerator()
{
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device()};
    return std::mt19937(seed);
}

} // namespace

Session::Session(SessionOptions options) : _options(std::move(options)), _random(SeededGenerator())
{
    if (_options.side != Side::Client)
    {
        return;
    }
    if (_options.clientKey.empty())
    {
        std::string nonce(16, '\0');
        for (char& byte : nonce)
        {
            byte = static_cast<char>(_random() & 0xffu);
        }
        _options.clientKey = Base64Encode(nonce);
    }
    _outbound = WriteUpgradeRequest(_options.host, _options.path, _options.clientKey);
}

void
Session::Receive(std::string_view bytes)
{
    if (_phase == Phase::Ended)
    {
        return;
    }
    if (_phase == Phase::Open)
    {
        _reader->Append(bytes);
        return;
    }
    _inbound.append(bytes);
}

bool
Session::Next(SessionEvent* event)
{
    if (_phase == Phase::Handshake)
    {
        const std::size_t headEnd = FindHeadEnd(_inbound);
        if (headEnd == 0 && _inbound.size() < MaxHandshakeHeadBytes)
        {
            return false;
        }
        *event = SessionEvent();
        if (headEnd == 0 || headEnd > MaxHandshakeHeadBytes)
        {
            // Dropped without an answer (§4.3): a peer that sends more than
            // the bound is not one this endpoint owes HTTP to.
            _phase = Phase::Ended;
            event->kind = SessionEvent::Kind::Refused;
            event->refusal = Refusal::Handshake;
            event->detail = "the handshake head is over " +
                            std::to_string(MaxHandshakeHeadBytes) + " bytes";
            return true;
        }

        const std::string_view head = std::string_view(_inbound).substr(0, headEnd);
        if (_options.side == Side::Server)
        {
            UpgradeAnswer answer = AnswerUpgrade(head, _options.path, _options.allowedOrigins);
            _outbound += answer.response;
            if (answer.refusal != Refusal::None)
            {
                _phase = Phase::Ended;
                event->kind = SessionEvent::Kind::Refused;
                event->refusal = answer.refusal;
                event->httpStatus = answer.status;
                event->origin = std::move(answer.origin);
                event->detail = std::move(answer.detail);
                return true;
            }
        }
        else
        {
            std::string detail;
            int status = 0;
            if (!CheckUpgradeResponse(head, _options.clientKey, &detail, &status))
            {
                _phase = Phase::Ended;
                event->kind = SessionEvent::Kind::Refused;
                event->refusal = Refusal::Handshake;
                event->httpStatus = status;
                event->detail = std::move(detail);
                return true;
            }
        }

        _phase = Phase::Open;
        _reader.emplace(_options.side == Side::Server, _options.maxMessageBytes, headEnd);
        _reader->Append(std::string_view(_inbound).substr(headEnd));
        _inbound.clear();
        _inbound.shrink_to_fit();
        event->kind = SessionEvent::Kind::Opened;
        return true;
    }

    while (_phase == Phase::Open)
    {
        FrameEvent frame;
        if (!_reader->Next(&frame))
        {
            return false;
        }
        *event = SessionEvent();
        switch (frame.kind)
        {
        case FrameEvent::Kind::Message:
            event->kind = SessionEvent::Kind::Message;
            event->text = std::move(frame.payload);
            return true;
        case FrameEvent::Kind::Ping:
            // A pong with the same payload; this endpoint sends no pings of
            // its own in version 1 (§5).
            _AppendFrame(Opcode::Pong, frame.payload);
            continue;
        case FrameEvent::Kind::Pong:
            // Unsolicited, since no ping was sent: ignored.
            continue;
        case FrameEvent::Kind::Close:
            // Echoed, then the session ends (§5, §6).
            _AppendFrame(Opcode::Close,
                         frame.closeStatus ? ClosePayload(*frame.closeStatus) : std::string());
            _phase = Phase::Ended;
            event->kind = SessionEvent::Kind::Closed;
            event->closeStatus = frame.closeStatus.value_or(CloseNoStatus);
            event->text = std::move(frame.payload);
            return true;
        case FrameEvent::Kind::Fault:
        {
            const std::uint16_t status = CloseStatusFor(frame.fault);
            _AppendFrame(Opcode::Close, ClosePayload(status));
            _phase = Phase::Ended;
            event->kind = SessionEvent::Kind::Fault;
            event->fault = frame.fault;
            event->closeStatus = status;
            event->byte = frame.byte;
            event->detail = std::move(frame.detail);
            return true;
        }
        }
    }
    return false;
}

void
Session::SendText(std::string_view text)
{
    if (_phase == Phase::Open)
    {
        _AppendFrame(Opcode::Text, text);
    }
}

void
Session::Close(std::uint16_t status)
{
    if (_phase == Phase::Open)
    {
        _AppendFrame(Opcode::Close, ClosePayload(status));
    }
    _phase = Phase::Ended;
}

void
Session::_AppendFrame(Opcode opcode, std::string_view payload)
{
    if (_options.side == Side::Server)
    {
        AppendFrame(&_outbound, opcode, payload);
        return;
    }
    // Every client frame is masked with a fresh key (RFC 6455 §5.3).
    const std::uint32_t bits = _random();
    const MaskKey mask = {static_cast<std::uint8_t>(bits >> 24),
                          static_cast<std::uint8_t>(bits >> 16),
                          static_cast<std::uint8_t>(bits >> 8),
                          static_cast<std::uint8_t>(bits)};
    AppendFrame(&_outbound, opcode, payload, true, &mask);
}

} // namespace openstrata::connectors::websocket::internal
