// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorWebSocket/Connector.h"

#include "Endpoint.h"

#include "motionConnectorTransport/Diagnostics.h"
#include "motionConnectorWire/FrameWire.h"

#include <utility>

namespace openstrata::connectors::websocket
{

namespace core = openstrata::connectors::core;

namespace
{

// What the wire can carry (§7): a relay can fill any of these, and a single
// frame may be sparser. `Controllers` is not among them, because version 1 has
// no field for it.
core::ConnectorCapabilities
WebSocketCapabilities()
{
    using core::ConnectorCapability;
    core::ConnectorCapabilities capabilities;
    for (const ConnectorCapability capability :
         {ConnectorCapability::Body, ConnectorCapability::Hands, ConnectorCapability::Face,
          ConnectorCapability::Eyes, ConnectorCapability::RootMotion, ConnectorCapability::Trackers,
          ConnectorCapability::SourceTimestamps, ConnectorCapability::Confidence,
          ConnectorCapability::MultipleActors})
    {
        capabilities.Add(capability);
    }
    return capabilities;
}

} // namespace

WebSocketConnector::WebSocketConnector()
    : _endpoint(std::make_unique<internal::Endpoint>()),
      _buffer(std::make_unique<core::FrameBuffer>())
{
}

WebSocketConnector::~WebSocketConnector() = default;

core::Status
WebSocketConnector::Open(const core::ConnectorConfig& config)
{
    WebSocketConfig websocket;
    websocket.role = WebSocketRole::Listen;
    websocket.address = config.bindAddress;
    websocket.port = config.port;
    return Open(config, websocket);
}

core::Status
WebSocketConnector::Open(const core::ConnectorConfig& connector, const WebSocketConfig& websocket)
{
    Close();
    const auto refuse = [&](std::string reason)
    {
        _state = core::ConnectorState::Error;
        return core::Status::Failure(std::move(reason));
    };
    if (!connector.coordinateConversion.empty())
    {
        return refuse("a frame on the wire is already in the canonical basis; the WebSocket "
                      "connector converts nothing and takes no coordinateConversion");
    }
    if (websocket.maxMessageBytes == 0 || websocket.maxMessageBytes > MaxMessageBytes)
    {
        return refuse("maxMessageBytes must be between 1 and " + std::to_string(MaxMessageBytes));
    }
    if (websocket.role == WebSocketRole::Connect && websocket.port == 0)
    {
        return refuse("a connect role needs the peer's port");
    }

    _sourceProfile = connector.sourceProfile;
    _websocket = websocket;
    _buffer = std::make_unique<core::FrameBuffer>(connector.bufferCapacity, connector.bufferMode);
    _diagnostics.clear();
    _frameNumber = 0;
    _peer.clear();
    _pushPeerSeen = false;
    _newConnection = false;
    _deliveredAny = false;
    _haveSenderNumber = false;
    _senderNumber = 0;
    _poseTimestamps.clear();
    _lastMessageTime = 0.0;
    _silenceReported = false;

    std::string error;
    if (!_endpoint->Open(websocket, 1, &_diagnostics, &error))
    {
        return refuse(error);
    }
    _endpointName = _endpoint->Source();
    _state = core::ConnectorState::Connecting;
    return core::Status::Ok();
}

void
WebSocketConnector::Close()
{
    StopCapture();
    _endpoint->Close();
    if (_buffer)
    {
        _buffer->Clear();
    }
    _endpointName.clear();
    _peer.clear();
    _state = core::ConnectorState::Disconnected;
}

core::ConnectorState
WebSocketConnector::GetState() const
{
    return _state;
}

core::ConnectorCapabilities
WebSocketConnector::GetCapabilities() const
{
    return WebSocketCapabilities();
}

const std::string&
WebSocketConnector::GetEndpoint() const noexcept
{
    return _endpointName;
}

double
WebSocketConnector::Now() const
{
    return _endpoint->Now();
}

core::FrameBufferStats
WebSocketConnector::GetBufferStats() const noexcept
{
    return _buffer ? _buffer->GetStats() : core::FrameBufferStats();
}

void
WebSocketConnector::StartCapture()
{
    _capture = PacketCapture();
    _capture.listenEndpoint =
        _websocket.role == WebSocketRole::Listen ? _endpointName : std::string();
    _capturing = true;
}

void
WebSocketConnector::StopCapture()
{
    _capturing = false;
}

bool
WebSocketConnector::Poll(core::MotionFrame& out)
{
    if (_buffer->Poll(out))
    {
        return true;
    }
    if (!_endpoint->IsOpen() || _state == core::ConnectorState::Error)
    {
        return false;
    }

    std::vector<internal::EndpointEvent> events;
    _endpoint->Service(&events, &_diagnostics);
    if (_endpoint->HasFailed())
    {
        _state = core::ConnectorState::Error;
    }
    for (internal::EndpointEvent& event : events)
    {
        switch (event.kind)
        {
        case internal::EndpointEvent::Kind::PeerOpened:
            _BeginConnection(event.peer);
            _lastMessageTime = event.time;
            _silenceReported = false;
            break;
        case internal::EndpointEvent::Kind::Message:
            if (_capturing)
            {
                RecordedDatagram record;
                record.receiveTime = event.time;
                record.peer = event.peer;
                record.bytes.assign(event.text.begin(), event.text.end());
                _capture.datagrams.push_back(std::move(record));
            }
            _AcceptMessage(event.text, event.time);
            break;
        case internal::EndpointEvent::Kind::PeerClosed:
            // A peer going away is a transition, never an error (§6): a
            // listener waits for the next peer and a connect role retries.
            _peer.clear();
            if (_state != core::ConnectorState::Error)
            {
                _state = core::ConnectorState::Connecting;
            }
            break;
        }
    }
    _CheckSilence();
    return _buffer->Poll(out);
}

void
WebSocketConnector::_CheckSilence()
{
    if (_websocket.silenceTimeoutSeconds <= 0.0 || _silenceReported)
    {
        return;
    }
    const double since = _endpoint->Now() - _lastMessageTime;
    if (since < _websocket.silenceTimeoutSeconds)
    {
        return;
    }
    _silenceReported = true;
    Diagnostic diagnostic = MakeDiagnostic(
        DiagnosticCode::SourceTimeout,
        "no message for " + transport::FormatSeconds(since) + "s");
    diagnostic.source = _endpointName;
    diagnostic.subject = _peer.empty() ? _endpointName : _peer;
    _diagnostics.push_back(std::move(diagnostic));
    if (_state == core::ConnectorState::Connected)
    {
        _state = core::ConnectorState::Degraded;
    }
}

std::size_t
WebSocketConnector::PushMessage(std::string_view text, double receiveTimestamp,
                                std::string_view peer)
{
    if (!_pushPeerSeen || peer != _peer)
    {
        _pushPeerSeen = true;
        _BeginConnection(peer);
    }
    return _AcceptMessage(text, receiveTimestamp);
}

void
WebSocketConnector::_BeginConnection(std::string_view peer)
{
    _peer = std::string(peer);
    _newConnection = true;
    _haveSenderNumber = false;
}

std::size_t
WebSocketConnector::_AcceptMessage(std::string_view text, double receiveTimestamp)
{
    _lastMessageTime = receiveTimestamp;
    _silenceReported = false;

    const auto report = [&](Diagnostic diagnostic)
    {
        diagnostic.source = _peer;
        _diagnostics.push_back(std::move(diagnostic));
    };

    core::MotionFrame frame;
    wire::WireError error;
    wire::DecodeOptions options;
    options.maxMessageBytes = _websocket.maxMessageBytes;
    if (!wire::DecodeFrame(text, &frame, &error, options))
    {
        // The text was well delimited, so this message is dropped and the
        // session continues (§5). The codec's code passes through.
        report(MakeWireDiagnostic(error));
        return 0;
    }

    const auto stamp = [&](DiagnosticCode code, std::string subject, std::string detail)
    {
        Diagnostic diagnostic = MakeDiagnostic(code, std::move(detail));
        diagnostic.subject = std::move(subject);
        diagnostic.timestamp = frame.timing.sourceTimestamp;
        diagnostic.sequence = frame.frameNumber;
        report(std::move(diagnostic));
    };

    // The sender's numbering (§7): a restart, or a gap. Neither drops the
    // frame, and every decoded frame counts, whatever is decided about it
    // below: the numbering is a fact about the sender.
    const std::uint64_t number = frame.frameNumber;
    if (_newConnection)
    {
        _newConnection = false;
        if (_deliveredAny)
        {
            stamp(DiagnosticCode::SourceRestarted, std::to_string(number),
                  "a new connection began after an earlier one delivered frames");
            _poseTimestamps.clear();
        }
    }
    else if (_haveSenderNumber)
    {
        if (number <= _senderNumber)
        {
            stamp(DiagnosticCode::SourceRestarted, std::to_string(number),
                  "the sender's frameNumber went back from " + std::to_string(_senderNumber));
            _poseTimestamps.clear();
        }
        else if (number > _senderNumber + 1)
        {
            const std::uint64_t first = _senderNumber + 1;
            const std::uint64_t last = number - 1;
            stamp(DiagnosticCode::FrameGap, std::to_string(first) + ".." + std::to_string(last),
                  "the sender skipped " + std::to_string(last - first + 1) + " frame(s)");
        }
    }
    _senderNumber = number;
    _haveSenderNumber = true;

    // The profile is the sender's, and only a configured one is enforced: a
    // runtime that expects one profile is never handed another (§7).
    if (!_sourceProfile.empty() && frame.sourceProfile != _sourceProfile)
    {
        stamp(DiagnosticCode::ProfileMismatch,
              frame.sourceProfile.empty() ? std::string("\"\"") : frame.sourceProfile,
              "expected " + _sourceProfile + "; the frame was dropped");
        return 0;
    }

    // A pose timestamp that does not advance drops the whole frame: a partly
    // delivered frame would be a frame nobody sent (§7).
    bool regressed = false;
    for (const core::ActorFrame& actor : frame.actors)
    {
        if (!actor.pose)
        {
            continue;
        }
        const auto found = _poseTimestamps.find(actor.actor);
        if (found != _poseTimestamps.end() && !(actor.pose->timestamp > found->second))
        {
            stamp(DiagnosticCode::TimestampRegression, actor.actor,
                  "pose timestamp " + transport::FormatSeconds(actor.pose->timestamp) +
                      " does not follow " + transport::FormatSeconds(found->second) +
                      "; the frame was dropped");
            regressed = true;
        }
    }
    if (regressed)
    {
        return 0;
    }
    for (const core::ActorFrame& actor : frame.actors)
    {
        if (actor.pose)
        {
            _poseTimestamps[actor.actor] = actor.pose->timestamp;
        }
    }

    // What this connector rewrites, and only that (§7).
    frame.frameNumber = ++_frameNumber;
    frame.timing.receiveTimestamp = receiveTimestamp;

    _deliveredAny = true;
    if (_buffer->Push(std::move(frame)) == core::FramePushResult::Accepted)
    {
        _state = core::ConnectorState::Connected;
        return 1;
    }
    _state = core::ConnectorState::Degraded;
    return 0;
}

} // namespace openstrata::connectors::websocket
