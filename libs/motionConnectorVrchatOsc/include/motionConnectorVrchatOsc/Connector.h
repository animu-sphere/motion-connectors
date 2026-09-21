// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorVrchatOsc/Diagnostics.h"
#include "motionConnectorVrchatOsc/FrameAssembler.h"
#include "motionConnectorVrchatOsc/TrackerMessage.h"
#include "motionConnectorVrchatOsc/UdpReceiver.h"
#include "motionConnectorVrchatOsc/api.h"

#include "motionConnectorCore/FrameBuffer.h"
#include "motionConnectorCore/IMotionConnector.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openstrata::connectors::vrchatOsc
{

class MOTIONCONNECTORVRCHATOSC_API VrchatOscConnector final
    : public openstrata::connectors::core::IMotionConnector
{
  public:
    VrchatOscConnector();

    openstrata::connectors::core::Status
    Open(const openstrata::connectors::core::ConnectorConfig& config) override;
    void Close() override;

    openstrata::connectors::core::ConnectorState GetState() const override;
    openstrata::connectors::core::ConnectorCapabilities GetCapabilities() const override;
    bool Poll(openstrata::connectors::core::MotionFrame& out) override;

    // Hardware-free input paths used by replay and connector tests. Both paths
    // enqueue the same MotionFrame values that Poll returns after UDP receive.
    std::size_t PushDatagram(const std::uint8_t* bytes, std::size_t size,
                             double receiveTimestamp);
    std::size_t PushPacket(const TrackerPacket& packet, double receiveTimestamp);

    const std::vector<Diagnostic>&
    GetDiagnostics() const noexcept
    {
        return _diagnostics;
    }

    void
    ClearDiagnostics() noexcept
    {
        _diagnostics.clear();
    }

    openstrata::connectors::core::FrameBufferStats GetBufferStats() const noexcept;

  private:
    std::size_t _PushPacket(const TrackerPacket& packet, double receiveTimestamp,
                            std::string_view peer);
    std::size_t _EnqueueFrames();
    void _StampDiagnostics(std::size_t from);
    openstrata::connectors::core::MotionFrame _MakeFrame(const TrackerFrame& frame);

    UdpReceiver _receiver;
    TrackerFrameAssembler _source;
    std::unique_ptr<openstrata::connectors::core::FrameBuffer> _buffer;
    openstrata::connectors::core::ConnectorState _state =
        openstrata::connectors::core::ConnectorState::Disconnected;
    openstrata::connectors::core::ConnectorConfig _config;
    std::string _sourceProfile = "vrchat-osc.trackers.v1";
    std::uint64_t _frameNumber = 0;
    std::uint64_t _datagramSerial = 0;
    std::vector<TrackerFrame> _frames;
    std::vector<Diagnostic> _diagnostics;
};

} // namespace openstrata::connectors::vrchatOsc
