// SPDX-License-Identifier: Apache-2.0
//
// What both directions share once the protocol is settled: a role (listen or
// connect), its peers, their handshakes and timeouts, reconnects, refusals
// that are owed an answer, and each peer's outbound queue
// (docs/design/WEBSOCKET_CONNECTOR.md §3–§6). It reports what happened as
// events and diagnostics; what a message means is the caller's.
//
// The receiving connector holds one peer and reads its messages; the sender
// holds up to `maxPeers` and queues messages to them. Both call `Service`
// from their own tick, and nothing here waits.
#pragma once

#include "Session.h"
#include "Tcp.h"

#include "motionConnectorWebSocket/Diagnostics.h"
#include "motionConnectorWebSocket/FrameSender.h"
#include "motionConnectorWebSocket/WebSocketConfig.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace openstrata::connectors::websocket::internal
{

struct EndpointEvent
{
    enum class Kind : std::uint8_t
    {
        // A peer completed its handshake.
        PeerOpened,
        // A text message from a peer.
        Message,
        // A peer that had opened is gone, by close, fault or reset.
        PeerClosed,
    };

    Kind kind = Kind::Message;
    std::string peer;
    std::string text;
    // Seconds since `Open`, read when the bytes completing the message were.
    double time = 0.0;
};

class Endpoint
{
  public:
    Endpoint();
    ~Endpoint();

    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    // Listen: binds, or fails with WEBSOCKET_SOCKET_BIND_FAILED. Connect:
    // resolves the peer address and attempts nothing until `Service`.
    bool Open(const WebSocketConfig& config, std::size_t maxPeers,
              std::vector<Diagnostic>* diagnostics, std::string* error);
    // Sends close 1001 to every open peer and releases every socket without
    // waiting for an echo (§5).
    void Close();

    bool
    IsOpen() const noexcept
    {
        return _open;
    }
    // The listening socket failed after `Open`.
    bool
    HasFailed() const noexcept
    {
        return _failed;
    }

    void Service(std::vector<EndpointEvent>* events, std::vector<Diagnostic>* diagnostics);

    // Queues one message to every open peer, the oldest not-started message
    // dropped from a full queue. `Service` writes it.
    void Enqueue(const std::shared_ptr<const std::string>& text);

    std::size_t OpenPeerCount() const;
    std::vector<WebSocketPeerStats> GetPeerStats() const;

    // Seconds since `Open`, monotonic.
    double Now() const;

    // Listen: the bound endpoint. Connect: the configured peer.
    const std::string&
    Source() const noexcept
    {
        return _source;
    }

  private:
    struct Peer
    {
        explicit Peer(SessionOptions options) : session(std::move(options)) {}

        TcpStream stream;
        Session session;
        std::string endpoint;
        double startedAt = 0.0;
        // A connect role's TCP connect has not completed yet.
        bool connecting = false;
        bool open = false;
        bool done = false;
        std::deque<std::shared_ptr<const std::string>> queue;
        std::uint64_t sent = 0;
        std::uint64_t dropped = 0;
    };

    // A connection that owes its peer a last answer -- a refusal or a close
    // frame -- and is closed once that is written and the peer has gone, or
    // after a second, so the answer is not lost to a reset.
    struct Lingering
    {
        TcpStream stream;
        std::string outbound;
        double deadline = 0.0;
        bool shutdown = false;
    };

    Diagnostic _Make(DiagnosticCode code, std::string subject, std::string detail) const;
    void _Accept(double now, std::vector<Diagnostic>* diagnostics);
    void _StartConnect(double now, std::vector<Diagnostic>* diagnostics);
    void _ConnectFailed(double now, const std::string& detail,
                        std::vector<Diagnostic>* diagnostics);
    void _ScheduleRetry(double now);
    void _ServicePeer(Peer& peer, std::vector<EndpointEvent>* events,
                      std::vector<Diagnostic>* diagnostics);
    // Ends a peer: its stream moves to the lingering list with whatever it
    // still owes, and a peer that had opened is reported closed.
    void _Retire(Peer& peer, std::vector<EndpointEvent>* events, bool linger);
    bool _Flush(Peer& peer, std::vector<EndpointEvent>* events,
                std::vector<Diagnostic>* diagnostics);
    void _Linger(TcpStream stream, std::string outbound);
    void _ServiceLingering(double now);

    WebSocketConfig _config;
    std::size_t _maxPeers = 1;
    bool _open = false;
    bool _failed = false;
    double _epoch = 0.0;
    std::string _source;

    TcpListener _listener;
    std::vector<std::unique_ptr<Peer>> _peers;
    std::vector<Lingering> _lingering;
    std::vector<char> _readBuffer;

    // The connect role's retry state (§3).
    double _nextAttempt = 0.0;
    double _backoff = 0.5;
    bool _connectFailureReported = false;
};

} // namespace openstrata::connectors::websocket::internal
