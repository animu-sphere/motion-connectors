// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionConnectorMocopi/Diagnostics.h"
#include "motionConnectorMocopi/PacketCapture.h"
#include "motionConnectorMocopi/FrameSource.h"
#include "motionConnectorMocopi/UdpReceiver.h"
#include "motionConnectorMocopi/api.h"

#include "motionConnectorCore/FrameBuffer.h"
#include "motionConnectorCore/IMotionConnector.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openstrata::connectors::mocopi {

class MOTIONCONNECTORMOCOPI_API MocopiConnector final
    : public openstrata::connectors::core::IMotionConnector {
public:
    MocopiConnector();

    openstrata::connectors::core::Status
    Open(const openstrata::connectors::core::ConnectorConfig& config) override;
    void Close() override;

    openstrata::connectors::core::ConnectorState GetState() const override;
    openstrata::connectors::core::ConnectorCapabilities GetCapabilities() const override;
    bool Poll(openstrata::connectors::core::MotionFrame& out) override;

    // Opt-in live capture before decoding. StartCapture clears the saved
    // records; StopCapture and Close retain them. Replay injection is excluded.
    // Times remain on the receiver's monotonic clock since Open.
    void StartCapture();
    void StopCapture();
    const PacketCapture& GetCapture() const noexcept { return _capture; }
    const std::string& GetEndpoint() const noexcept { return _receiver.GetBoundEndpoint(); }

    // Hardware-free input paths used by replay and connector tests. Both paths
    // enqueue the same MotionFrame values that Poll returns after UDP receive.
    std::size_t PushDatagram(const std::uint8_t* bytes, std::size_t size, double receiveTimestamp);
    std::size_t PushPacket(const MotionPacket& packet, double receiveTimestamp);

    const std::vector<Diagnostic>& GetDiagnostics() const noexcept { return _diagnostics; }

    void ClearDiagnostics() noexcept { _diagnostics.clear(); }

    openstrata::connectors::core::FrameBufferStats GetBufferStats() const noexcept;

private:
    std::size_t _EnqueueFrames(double receiveTimestamp);
    openstrata::connectors::core::MotionFrame _MakeFrame(const MocopiFrame& frame,
                                                         double receiveTimestamp);

    bool _capturing = false;
    PacketCapture _capture;
    UdpReceiver _receiver;
    MocopiFrameSource _source;
    std::unique_ptr<openstrata::connectors::core::FrameBuffer> _buffer;
    openstrata::connectors::core::ConnectorState _state =
        openstrata::connectors::core::ConnectorState::Disconnected;
    openstrata::connectors::core::ConnectorConfig _config;
    std::string _sourceProfile = "mocopi.body.v1";
    std::uint64_t _frameNumber = 0;
    std::vector<Diagnostic> _diagnostics;
};

} // namespace openstrata::connectors::mocopi