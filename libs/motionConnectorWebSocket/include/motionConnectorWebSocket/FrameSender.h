// SPDX-License-Identifier: Apache-2.0
//
// The sending direction: `MotionFrame`s out as `openstrata.motion.frame/v1`
// text messages, listening for peers or connecting to one
// (docs/design/WEBSOCKET_CONNECTOR.md §3, §7.1). `motion_connect bridge` is
// its first caller.
//
// It writes what it is given. The codec is a pure function, so a bridge sends
// the source connector's frame as that connector delivered it, `frameNumber`
// and `receiveTimestamp` included, and the next receiver rewrites both. A frame
// the encoder refuses is reported with its `WIRE_*` code and sent to nobody.
//
// Caller-driven like the connector: `Send` encodes and queues; `Service`
// accepts peers, answers control frames, reconnects and flushes the queues,
// and neither waits.
#pragma once

#include "motionConnectorWebSocket/Diagnostics.h"
#include "motionConnectorWebSocket/WebSocketConfig.h"
#include "motionConnectorWebSocket/api.h"

#include "motionConnectorCore/Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openstrata::connectors::websocket
{

namespace internal
{
class Endpoint;
}

// One peer's queue, as the sender sees it.
struct WebSocketPeerStats
{
    std::string endpoint;
    // Messages waiting that have not started.
    std::size_t queuedMessages = 0;
    std::uint64_t sentMessages = 0;
    // Messages dropped from a full queue, oldest first, never one part-written.
    std::uint64_t droppedMessages = 0;
};

class MOTIONCONNECTORWEBSOCKET_API WebSocketFrameSender final
{
  public:
    WebSocketFrameSender();
    ~WebSocketFrameSender();

    WebSocketFrameSender(const WebSocketFrameSender&) = delete;
    WebSocketFrameSender& operator=(const WebSocketFrameSender&) = delete;

    // Listen: binds, and fails with WEBSOCKET_SOCKET_BIND_FAILED when it
    // cannot. Connect: the first attempt is made by `Service`. A configuration
    // this endpoint cannot honour fails with the reason in `GetLastError`.
    bool Open(const WebSocketConfig& config);
    // Close 1001 to every peer, without waiting for an echo.
    void Close();

    bool IsOpen() const noexcept;

    // Encodes `frame` once and queues the same text to every peer in session.
    // False when the encoder refused it (a `WIRE_*` diagnostic) or it is over
    // the message bound (WEBSOCKET_MESSAGE_TOO_LARGE); either way nobody gets
    // it. With no peer in session, the frame goes to nobody and that is not a
    // failure.
    bool Send(const openstrata::connectors::core::MotionFrame& frame);

    // Accepts or reconnects, completes handshakes, answers pings and closes,
    // and writes as much of each queue as the sockets take. A text message a
    // peer sends is read, checked as framing, and discarded.
    void Service();

    // Peers whose handshake has completed.
    std::size_t GetPeerCount() const;
    std::vector<WebSocketPeerStats> GetPeerStats() const;

    // Listen: the bound endpoint, the OS-chosen port included. Connect: the
    // peer.
    const std::string& GetEndpoint() const noexcept;

    const std::string&
    GetLastError() const noexcept
    {
        return _lastError;
    }

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

  private:
    std::unique_ptr<internal::Endpoint> _endpoint;
    WebSocketConfig _config;
    std::vector<Diagnostic> _diagnostics;
    std::string _lastError;
};

} // namespace openstrata::connectors::websocket
