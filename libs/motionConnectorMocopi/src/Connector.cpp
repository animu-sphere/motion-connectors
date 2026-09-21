// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorMocopi/Connector.h"

#include <utility>

namespace openstrata::connectors::mocopi
{

namespace
{

openstrata::connectors::core::ConnectorCapabilities
MocopiCapabilities()
{
    using openstrata::connectors::core::ConnectorCapability;
    openstrata::connectors::core::ConnectorCapabilities capabilities;
    capabilities.Add(ConnectorCapability::Body);
    capabilities.Add(ConnectorCapability::RootMotion);
    capabilities.Add(ConnectorCapability::SourceTimestamps);
    return capabilities;
}

} // namespace

MocopiConnector::MocopiConnector()
    : _buffer(std::make_unique<openstrata::connectors::core::FrameBuffer>())
{
}

openstrata::connectors::core::Status
MocopiConnector::Open(const openstrata::connectors::core::ConnectorConfig& config)
{
    Close();
    if (config.sourceProfile != "mocopi.body.v1")
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return openstrata::connectors::core::Status::Failure(
            "mocopi connector requires source profile mocopi.body.v1");
    }
    _config = config;
    _sourceProfile = config.sourceProfile;
    _buffer = std::make_unique<openstrata::connectors::core::FrameBuffer>(
        config.bufferCapacity, config.bufferMode);
    _source.Reset();
    _frameNumber = 0;
    _diagnostics.clear();

    UdpReceiverConfig receiverConfig;
    receiverConfig.listenAddress = config.bindAddress;
    receiverConfig.listenPort = config.port;
    if (!_receiver.Open(receiverConfig, &_diagnostics))
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return openstrata::connectors::core::Status::Failure(_receiver.GetLastErrorText());
    }

    _state = openstrata::connectors::core::ConnectorState::Connecting;
    return openstrata::connectors::core::Status::Ok();
}

void
MocopiConnector::Close()
{
    _receiver.Close();
    if (_buffer)
    {
        _buffer->Clear();
    }
    _state = openstrata::connectors::core::ConnectorState::Disconnected;
}

openstrata::connectors::core::ConnectorState
MocopiConnector::GetState() const
{
    return _state;
}

openstrata::connectors::core::ConnectorCapabilities
MocopiConnector::GetCapabilities() const
{
    return MocopiCapabilities();
}

bool
MocopiConnector::Poll(openstrata::connectors::core::MotionFrame& out)
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

    PushDatagram(datagram.bytes.data(), datagram.bytes.size(), datagram.receiveTime);
    return _buffer->Poll(out);
}

std::size_t
MocopiConnector::PushDatagram(const std::uint8_t* bytes, std::size_t size,
                              double receiveTimestamp)
{
    _source.PushDatagram(bytes, size, receiveTimestamp, &_diagnostics);
    return _EnqueueFrames(receiveTimestamp);
}

std::size_t
MocopiConnector::PushPacket(const MotionPacket& packet, double receiveTimestamp)
{
    _source.PushPacket(packet, receiveTimestamp, &_diagnostics);
    return _EnqueueFrames(receiveTimestamp);
}

std::size_t
MocopiConnector::_EnqueueFrames(double receiveTimestamp)
{
    std::size_t accepted = 0;
    for (const MocopiFrame& frame : _source.GetFramesFromLastPush())
    {
        const auto result = _buffer->Push(_MakeFrame(frame, receiveTimestamp));
        if (result == openstrata::connectors::core::FramePushResult::Accepted)
        {
            ++accepted;
            _state = frame.missing.any() ? openstrata::connectors::core::ConnectorState::Degraded
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
MocopiConnector::_MakeFrame(const MocopiFrame& frame, double receiveTimestamp)
{
    openstrata::connectors::core::MotionFrame out;
    out.sourceProfile = _sourceProfile;
    out.frameNumber = ++_frameNumber;
    out.timing.receiveTimestamp = receiveTimestamp;
    out.timing.sourceTimestamp = static_cast<double>(frame.pose.timestamp);
    out.timing.sourceClock = openstrata::connectors::core::ClockDomain::Device;

    openstrata::connectors::core::ActorFrame actor;
    actor.actor = "mocopi:0";
    actor.pose = frame.pose;
    actor.pose->metadata = _source.GetSourceMetadata();
    actor.pose->metadata.sourceTimestamp = out.timing.sourceTimestamp;
    out.actors.push_back(std::move(actor));
    return out;
}

openstrata::connectors::core::FrameBufferStats
MocopiConnector::GetBufferStats() const noexcept
{
    return _buffer ? _buffer->GetStats()
                   : openstrata::connectors::core::FrameBufferStats();
}

} // namespace openstrata::connectors::mocopi