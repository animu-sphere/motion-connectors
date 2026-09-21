// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVrchatOsc/Connector.h"

#include <utility>

namespace openstrata::connectors::vrchatOsc
{

namespace
{

openstrata::connectors::core::ConnectorCapabilities
VrchatOscCapabilities()
{
    using openstrata::connectors::core::ConnectorCapability;
    openstrata::connectors::core::ConnectorCapabilities capabilities;
    capabilities.Add(ConnectorCapability::Trackers);
    return capabilities;
}

bool
FrameIsDegraded(const TrackerFrame& frame)
{
    return !frame.missing.empty() || !frame.stale.empty() || frame.partial != 0 ||
           frame.followsDiscontinuity;
}

} // namespace

VrchatOscConnector::VrchatOscConnector()
    : _buffer(std::make_unique<openstrata::connectors::core::FrameBuffer>())
{
}

openstrata::connectors::core::Status
VrchatOscConnector::Open(const openstrata::connectors::core::ConnectorConfig& config)
{
    Close();
    if (config.sourceProfile != "vrchat-osc.trackers.v1")
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return openstrata::connectors::core::Status::Failure(
            "VRChat OSC connector requires source profile vrchat-osc.trackers.v1");
    }

    _config = config;
    _sourceProfile = config.sourceProfile;
    _buffer = std::make_unique<openstrata::connectors::core::FrameBuffer>(
        config.bufferCapacity, config.bufferMode);
    _source.Reset();
    _frameNumber = 0;
    _datagramSerial = 0;
    _frames.clear();
    _diagnostics.clear();

    UdpReceiverConfig receiverConfig;
    receiverConfig.listenAddress = config.bindAddress;
    receiverConfig.listenPort = config.port;
    if (!_receiver.Open(receiverConfig, &_diagnostics))
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return openstrata::connectors::core::Status::Failure(_receiver.GetLastErrorText());
    }
    _source.SetSource(_receiver.GetBoundEndpoint());

    _state = openstrata::connectors::core::ConnectorState::Connecting;
    return openstrata::connectors::core::Status::Ok();
}

void
VrchatOscConnector::Close()
{
    _receiver.Close();
    if (_buffer)
    {
        _buffer->Clear();
    }
    _frames.clear();
    _state = openstrata::connectors::core::ConnectorState::Disconnected;
}

openstrata::connectors::core::ConnectorState
VrchatOscConnector::GetState() const
{
    return _state;
}

openstrata::connectors::core::ConnectorCapabilities
VrchatOscConnector::GetCapabilities() const
{
    return VrchatOscCapabilities();
}

bool
VrchatOscConnector::Poll(openstrata::connectors::core::MotionFrame& out)
{
    if (_buffer->Poll(out))
    {
        return true;
    }
    if (!_receiver.IsOpen() || _state == openstrata::connectors::core::ConnectorState::Error)
    {
        return false;
    }

    ReceivedDatagram datagram;
    const ReceiveStatus status = _receiver.Receive(&datagram, 0.0, &_diagnostics);
    if (status == ReceiveStatus::Failed)
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return false;
    }
    if (status != ReceiveStatus::Received)
    {
        return false;
    }

    _PushPacket(DecodeTrackerDatagram(datagram.bytes), datagram.receiveTime, datagram.peer);
    return _buffer->Poll(out);
}

std::size_t
VrchatOscConnector::PushDatagram(const std::uint8_t* bytes, std::size_t size,
                                 double receiveTimestamp)
{
    return _PushPacket(DecodeTrackerDatagram(bytes, size), receiveTimestamp, {});
}

std::size_t
VrchatOscConnector::PushPacket(const TrackerPacket& packet, double receiveTimestamp)
{
    return _PushPacket(packet, receiveTimestamp, {});
}

std::size_t
VrchatOscConnector::_PushPacket(const TrackerPacket& packet, double receiveTimestamp,
                                std::string_view peer)
{
    ++_datagramSerial;
    _frames.clear();
    const std::size_t diagnosticsBefore = _diagnostics.size();
    for (Diagnostic diagnostic : packet.diagnostics)
    {
        _diagnostics.push_back(std::move(diagnostic));
    }

    _source.Push(packet, receiveTimestamp, peer, &_frames, &_diagnostics);
    _StampDiagnostics(diagnosticsBefore);
    return _EnqueueFrames();
}

void
VrchatOscConnector::_StampDiagnostics(std::size_t from)
{
    for (std::size_t index = from; index < _diagnostics.size(); ++index)
    {
        _diagnostics[index].source = _source.GetSource();
        _diagnostics[index].sequence = _datagramSerial;
    }
}

std::size_t
VrchatOscConnector::_EnqueueFrames()
{
    std::size_t accepted = 0;
    for (const TrackerFrame& frame : _frames)
    {
        const auto result = _buffer->Push(_MakeFrame(frame));
        if (result == openstrata::connectors::core::FramePushResult::Accepted)
        {
            ++accepted;
            _state = FrameIsDegraded(frame)
                          ? openstrata::connectors::core::ConnectorState::Degraded
                          : openstrata::connectors::core::ConnectorState::Connected;
        }
        else
        {
            _state = openstrata::connectors::core::ConnectorState::Degraded;
        }
    }
    return accepted;
}

openstrata::connectors::core::MotionFrame
VrchatOscConnector::_MakeFrame(const TrackerFrame& frame)
{
    using namespace openstrata::connectors::core;

    MotionFrame out;
    out.sourceProfile = _sourceProfile;
    out.frameNumber = ++_frameNumber;
    out.timing.receiveTimestamp = frame.receiveTime;
    out.timing.sourceClock = ClockDomain::None;

    ActorFrame actor;
    actor.actor = "vrchat-osc:0";
    actor.trackers.reserve(frame.samples.size());
    for (const TrackerSample& sample : frame.samples)
    {
        TrackerObservation observation;
        observation.trackerId = sample.tracker;
        observation.position = sample.position;
        observation.rotation = sample.rotation;
        observation.hasPosition = sample.hasPosition;
        observation.hasRotation = sample.hasRotation;
        actor.trackers.push_back(std::move(observation));
    }
    out.actors.push_back(std::move(actor));
    return out;
}

openstrata::connectors::core::FrameBufferStats
VrchatOscConnector::GetBufferStats() const noexcept
{
    return _buffer ? _buffer->GetStats()
                   : openstrata::connectors::core::FrameBufferStats();
}

} // namespace openstrata::connectors::vrchatOsc
