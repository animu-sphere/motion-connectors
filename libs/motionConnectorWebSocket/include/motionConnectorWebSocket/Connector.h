// SPDX-License-Identifier: Apache-2.0
//
// The receiving direction: `openstrata.motion.frame/v1` messages in, as an
// `IMotionConnector` (docs/design/WEBSOCKET_CONNECTOR.md §3, §6, §7).
//
// **One message is one frame**, and nothing is assembled across messages. A
// frame on the wire is already canonical, so nothing is converted. What the
// connector does to a received frame is §7's, and nothing else:
//
// * `frameNumber` becomes its own, from 1 after each `Open` and monotone
//   across connections;
// * `timing.receiveTimestamp` becomes this process's clock, read when the
//   message's last fragment was;
// * everything else is carried verbatim, the profile and the pose's
//   provenance included.
//
// It reports a sender restart, a gap in the sender's `frameNumber`, and drops
// a frame whose actor's pose timestamp does not advance, or whose profile is
// not the configured one. A codec refusal drops that message and the session
// continues; a framing error ends the connection (§5).
//
// `ConnectorState` is the state of this link, not of the source behind it.
#pragma once

#include "motionConnectorWebSocket/Diagnostics.h"
#include "motionConnectorWebSocket/PacketCapture.h"
#include "motionConnectorWebSocket/WebSocketConfig.h"
#include "motionConnectorWebSocket/api.h"

#include "motionConnectorCore/FrameBuffer.h"
#include "motionConnectorCore/IMotionConnector.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openstrata::connectors::websocket
{

namespace internal
{
class Endpoint;
}

class MOTIONCONNECTORWEBSOCKET_API WebSocketConnector final
    : public openstrata::connectors::core::IMotionConnector
{
  public:
    WebSocketConnector();
    ~WebSocketConnector() override;

    WebSocketConnector(const WebSocketConnector&) = delete;
    WebSocketConnector& operator=(const WebSocketConnector&) = delete;

    // The receiving listen role on `bindAddress:port`, every other value at
    // its default (§3).
    openstrata::connectors::core::Status
    Open(const openstrata::connectors::core::ConnectorConfig& config) override;

    // The rest stated: the role, address and port, path, origins, bounds and
    // timeouts come from `websocket`. From `connector` this reads the profile,
    // the buffer and `coordinateConversion`, which must be empty: a frame on
    // the wire is already canonical. Its `bindAddress` and `port` are not read.
    openstrata::connectors::core::Status
    Open(const openstrata::connectors::core::ConnectorConfig& connector,
         const WebSocketConfig& websocket);

    // Close 1001 to the peer, without waiting for the echo.
    void Close() override;

    openstrata::connectors::core::ConnectorState GetState() const override;
    openstrata::connectors::core::ConnectorCapabilities GetCapabilities() const override;

    // Non-blocking: accepts or reconnects, reads at most the configured
    // budget, and returns one buffered frame.
    bool Poll(openstrata::connectors::core::MotionFrame& out) override;

    // The hardware-free path (§9): one message, as the session would have
    // handed it to the codec, through the same assembly as a live one. A
    // change of `peer` is a new connection, as a capture's `p` line is.
    // Returns the frames accepted, 0 or 1.
    std::size_t PushMessage(std::string_view text, double receiveTimestamp,
                            std::string_view peer = {});

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

    // Listen: the bound endpoint, the OS-chosen port included. Connect: the
    // peer. Empty before `Open`.
    const std::string& GetEndpoint() const noexcept;
    // The peer in session, or empty.
    const std::string&
    GetPeer() const noexcept
    {
        return _peer;
    }

    // Seconds since `Open` on the clock every live `receiveTimestamp` is read
    // from.
    double Now() const;

    // Raw capture (§9): from here on, each message received live is appended,
    // after unmasking and reassembly and before the codec reads it, at its
    // receive time with its peer. `StartCapture` clears what was there. A
    // reconnection to the same peer endpoint is not visible in the capture;
    // the format names peers, not connections.
    void StartCapture();
    void StopCapture();
    const PacketCapture&
    GetCapture() const noexcept
    {
        return _capture;
    }

  private:
    void _BeginConnection(std::string_view peer);
    std::size_t _AcceptMessage(std::string_view text, double receiveTimestamp);
    void _CheckSilence();

    std::unique_ptr<internal::Endpoint> _endpoint;
    std::unique_ptr<openstrata::connectors::core::FrameBuffer> _buffer;
    openstrata::connectors::core::ConnectorState _state =
        openstrata::connectors::core::ConnectorState::Disconnected;
    std::string _sourceProfile;
    WebSocketConfig _websocket;
    std::vector<Diagnostic> _diagnostics;
    std::string _endpointName;
    std::string _peer;
    std::uint64_t _frameNumber = 0;

    // §7's assembly state.
    bool _pushPeerSeen = false;
    bool _newConnection = false;
    bool _deliveredAny = false;
    bool _haveSenderNumber = false;
    std::uint64_t _senderNumber = 0;
    std::map<std::string, double> _poseTimestamps;

    // §6's silence episode.
    double _lastMessageTime = 0.0;
    bool _silenceReported = false;

    bool _capturing = false;
    PacketCapture _capture;
};

} // namespace openstrata::connectors::websocket
