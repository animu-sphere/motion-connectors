// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorWebSocket/FrameSender.h"

#include "Endpoint.h"

#include "motionConnectorWire/FrameWire.h"

#include <utility>

namespace openstrata::connectors::websocket
{

WebSocketFrameSender::WebSocketFrameSender() : _endpoint(std::make_unique<internal::Endpoint>())
{
}

WebSocketFrameSender::~WebSocketFrameSender() = default;

bool
WebSocketFrameSender::Open(const WebSocketConfig& config)
{
    Close();
    _diagnostics.clear();
    _lastError.clear();
    if (config.maxMessageBytes == 0 || config.maxMessageBytes > MaxMessageBytes)
    {
        _lastError = "maxMessageBytes must be between 1 and " + std::to_string(MaxMessageBytes);
        return false;
    }
    if (config.role == WebSocketRole::Connect && config.port == 0)
    {
        _lastError = "a connect role needs the peer's port";
        return false;
    }
    _config = config;
    const std::size_t maxPeers = config.role == WebSocketRole::Listen ? config.maxPeers : 1;
    return _endpoint->Open(config, maxPeers, &_diagnostics, &_lastError);
}

void
WebSocketFrameSender::Close()
{
    _endpoint->Close();
}

bool
WebSocketFrameSender::IsOpen() const noexcept
{
    return _endpoint->IsOpen();
}

bool
WebSocketFrameSender::Send(const openstrata::connectors::core::MotionFrame& frame)
{
    auto text = std::make_shared<std::string>();
    wire::WireError error;
    if (!wire::EncodeFrame(frame, text.get(), &error))
    {
        Diagnostic diagnostic = MakeWireDiagnostic(error);
        diagnostic.source = _endpoint->Source();
        diagnostic.sequence = frame.frameNumber;
        _diagnostics.push_back(std::move(diagnostic));
        return false;
    }
    if (text->size() > _config.maxMessageBytes)
    {
        // A receiver would close the connection with 1009 (§5), so the frame
        // is refused here instead, and the session keeps going.
        Diagnostic diagnostic = MakeDiagnostic(
            DiagnosticCode::MessageTooLarge,
            "an encoded frame of " + std::to_string(text->size()) + " bytes exceeds the " +
                std::to_string(_config.maxMessageBytes) + "-byte bound; sent to nobody");
        diagnostic.source = _endpoint->Source();
        diagnostic.sequence = frame.frameNumber;
        _diagnostics.push_back(std::move(diagnostic));
        return false;
    }
    // Encoded once; every peer's queue holds the same text (§3).
    _endpoint->Enqueue(std::shared_ptr<const std::string>(std::move(text)));
    return true;
}

void
WebSocketFrameSender::Service()
{
    // Peer events are the endpoint's own business here: a sender has no state
    // of its own to move, and a text message a peer sends is not one it reads.
    _endpoint->Service(nullptr, &_diagnostics);
}

std::size_t
WebSocketFrameSender::GetPeerCount() const
{
    return _endpoint->OpenPeerCount();
}

std::vector<WebSocketPeerStats>
WebSocketFrameSender::GetPeerStats() const
{
    return _endpoint->GetPeerStats();
}

const std::string&
WebSocketFrameSender::GetEndpoint() const noexcept
{
    return _endpoint->Source();
}

} // namespace openstrata::connectors::websocket
