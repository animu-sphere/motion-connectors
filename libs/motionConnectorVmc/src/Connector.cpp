// SPDX-License-Identifier: Apache-2.0

#include "motionConnectorVmc/Connector.h"

#include <utility>

namespace openstrata::connectors::vmc
{

namespace
{

openstrata::connectors::core::ConnectorCapabilities
VmcCapabilities()
{
    using openstrata::connectors::core::ConnectorCapability;
    openstrata::connectors::core::ConnectorCapabilities capabilities;
    capabilities.Add(ConnectorCapability::Body);
    capabilities.Add(ConnectorCapability::Hands);
    capabilities.Add(ConnectorCapability::Face);
    capabilities.Add(ConnectorCapability::RootMotion);
    capabilities.Add(ConnectorCapability::SourceTimestamps);
    return capabilities;
}

} // namespace

VmcConnector::VmcConnector()
    : _buffer(std::make_unique<openstrata::connectors::core::FrameBuffer>())
{
}

openstrata::connectors::core::Status
VmcConnector::Open(const openstrata::connectors::core::ConnectorConfig& config)
{
    Close();
    if (config.sourceProfile != "vmc.v1")
    {
        _state = openstrata::connectors::core::ConnectorState::Error;
        return openstrata::connectors::core::Status::Failure(
            "VMC connector requires source profile vmc.v1");
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
VmcConnector::Close()
{
    _receiver.Close();
    if (_buffer)
    {
        _buffer->Clear();
    }
    _state = openstrata::connectors::core::ConnectorState::Disconnected;
}

openstrata::connectors::core::ConnectorState
VmcConnector::GetState() const
{
    return _state;
}

openstrata::connectors::core::ConnectorCapabilities
VmcConnector::GetCapabilities() const
{
    return VmcCapabilities();
}

bool
VmcConnector::Poll(openstrata::connectors::core::MotionFrame& out)
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
    const ReceiveStatus status = _receiver.Receive(&datagram, 0.0);
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
VmcConnector::PushDatagram(const std::uint8_t* bytes, std::size_t size,
                           double receiveTimestamp)
{
    _source.PushDatagram(bytes, size, receiveTimestamp, &_diagnostics);
    return _EnqueueFrames(receiveTimestamp);
}

std::size_t
VmcConnector::PushPacket(const VmcPacket& packet, double receiveTimestamp)
{
    _source.PushPacket(packet, receiveTimestamp, &_diagnostics);
    return _EnqueueFrames(receiveTimestamp);
}

std::size_t
VmcConnector::Flush(double receiveTimestamp)
{
    _source.Flush(&_diagnostics);
    return _EnqueueFrames(receiveTimestamp);
}

std::size_t
VmcConnector::_EnqueueFrames(double receiveTimestamp)
{
    std::size_t accepted = 0;
    for (const VmcFrame& frame : _source.GetFramesFromLastPush())
    {
        const auto result = _buffer->Push(_MakeFrame(frame, receiveTimestamp));
        if (result == openstrata::connectors::core::FramePushResult::Accepted)
        {
            ++accepted;
            _state = (frame.missing.any() || frame.stale.any())
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
VmcConnector::_MakeFrame(const VmcFrame& frame, double receiveTimestamp)
{
    using namespace openstrata::connectors::core;

    MotionFrame out;
    out.sourceProfile = _sourceProfile;
    out.frameNumber = ++_frameNumber;
    out.timing.receiveTimestamp = receiveTimestamp;
    out.timing.sourceTimestamp = frame.timestampFromSender
                                     ? std::optional<double>(frame.pose.timestamp)
                                     : std::nullopt;
    out.timing.sourceClock = frame.timestampFromSender ? ClockDomain::Device : ClockDomain::None;

    ActorFrame actor;
    actor.actor = "vmc:0";
    actor.pose = frame.pose;
    actor.pose->metadata = _source.GetSourceMetadata();
    actor.pose->metadata.sourceTimestamp = out.timing.sourceTimestamp;
    out.actors.push_back(std::move(actor));
    return out;
}

openstrata::connectors::core::FrameBufferStats
VmcConnector::GetBufferStats() const noexcept
{
    return _buffer ? _buffer->GetStats()
                   : openstrata::connectors::core::FrameBufferStats();
}

} // namespace openstrata::connectors::vmc