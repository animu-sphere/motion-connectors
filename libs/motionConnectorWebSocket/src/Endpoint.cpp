// SPDX-License-Identifier: Apache-2.0

#include "Endpoint.h"

#include "motionConnectorTransport/Diagnostics.h"

#include <algorithm>
#include <utility>

namespace openstrata::connectors::websocket::internal
{

namespace
{

// How long a refused or closed connection is kept to deliver its last answer.
constexpr double kLingerSeconds = 1.0;
// More than this many lingering connections are closed outright: a flood of
// refused peers is owed nothing.
constexpr std::size_t kMaxLingering = 16;
// Accepts per `Service`, so a connection flood cannot hold the caller's tick.
constexpr int kAcceptsPerService = 16;
constexpr std::size_t kReadChunk = 16384;

std::string
Seconds(double seconds)
{
    return transport::FormatSeconds(seconds);
}

DiagnosticCode
CodeFor(FramingFault fault)
{
    switch (fault)
    {
    case FramingFault::MessageTooLarge:
        return DiagnosticCode::MessageTooLarge;
    case FramingFault::UnsupportedMessage:
        return DiagnosticCode::UnsupportedMessage;
    case FramingFault::ProtocolViolation:
    case FramingFault::InvalidUtf8:
        break;
    }
    return DiagnosticCode::ProtocolViolation;
}

} // namespace

Endpoint::Endpoint() : _epoch(SteadySeconds())
{
}

Endpoint::~Endpoint()
{
    Close();
}

double
Endpoint::Now() const
{
    return SteadySeconds() - _epoch;
}

Diagnostic
Endpoint::_Make(DiagnosticCode code, std::string subject, std::string detail) const
{
    Diagnostic diagnostic = MakeDiagnostic(code, std::move(detail));
    diagnostic.source = _source;
    diagnostic.subject = std::move(subject);
    return diagnostic;
}

bool
Endpoint::Open(const WebSocketConfig& config, std::size_t maxPeers,
               std::vector<Diagnostic>* diagnostics, std::string* error)
{
    Close();
    _config = config;
    _maxPeers = std::max<std::size_t>(maxPeers, 1);
    _epoch = SteadySeconds();
    _failed = false;
    _backoff = config.reconnectInitialSeconds;
    _nextAttempt = 0.0;
    _connectFailureReported = false;
    _readBuffer.resize(kReadChunk);

    if (config.role == WebSocketRole::Listen)
    {
        std::string failure;
        if (!_listener.Open(config.address, config.port, &failure))
        {
            const std::string requested = config.address + ":" + std::to_string(config.port);
            if (diagnostics)
            {
                Diagnostic diagnostic =
                    MakeDiagnostic(DiagnosticCode::SocketBindFailed, failure);
                diagnostic.subject = requested;
                diagnostics->push_back(std::move(diagnostic));
            }
            if (error)
            {
                *error = "cannot listen on " + requested + ": " + failure;
            }
            return false;
        }
        _source = _listener.BoundEndpoint();
    }
    else
    {
        _source = (config.address.find(':') != std::string::npos
                       ? "[" + config.address + "]"
                       : config.address) +
                  ":" + std::to_string(config.port);
    }
    _open = true;
    return true;
}

void
Endpoint::Close()
{
    for (const auto& peer : _peers)
    {
        if (peer->open)
        {
            // Going away, written once and not waited on (§5).
            peer->session.Close(CloseGoingAway);
            std::string& out = peer->session.Outbound();
            std::size_t written = 0;
            peer->stream.Write(out.data(), out.size(), &written, nullptr);
        }
        peer->stream.Close();
    }
    _peers.clear();
    for (Lingering& lingering : _lingering)
    {
        lingering.stream.Close();
    }
    _lingering.clear();
    _listener.Close();
    _open = false;
    _source.clear();
}

void
Endpoint::Service(std::vector<EndpointEvent>* events, std::vector<Diagnostic>* diagnostics)
{
    if (!_open)
    {
        return;
    }
    const double now = Now();
    if (_config.role == WebSocketRole::Listen)
    {
        _Accept(now, diagnostics);
    }
    else if (_peers.empty() && now >= _nextAttempt)
    {
        _StartConnect(now, diagnostics);
    }

    for (const auto& peer : _peers)
    {
        _ServicePeer(*peer, events, diagnostics);
    }
    _peers.erase(std::remove_if(_peers.begin(), _peers.end(),
                                [](const std::unique_ptr<Peer>& peer) { return peer->done; }),
                 _peers.end());
    _ServiceLingering(Now());
}

void
Endpoint::_Accept(double now, std::vector<Diagnostic>* diagnostics)
{
    for (int count = 0; count < kAcceptsPerService && _listener.IsOpen(); ++count)
    {
        TcpStream stream;
        std::string failure;
        const auto status = _listener.Accept(&stream, &failure);
        if (status == TcpListener::AcceptStatus::None)
        {
            return;
        }
        if (status == TcpListener::AcceptStatus::Failed)
        {
            if (diagnostics)
            {
                diagnostics->push_back(
                    _Make(DiagnosticCode::SocketBindFailed, _source,
                          "the listening socket failed: " + failure));
            }
            _listener.Close();
            _failed = true;
            return;
        }

        if (_peers.size() >= _maxPeers)
        {
            // Refused before the upgrade (§3): a second sender is not merged
            // into the first one's stream (WSC-O2), and the sender's peer
            // bound is a bound.
            if (diagnostics)
            {
                diagnostics->push_back(_Make(
                    DiagnosticCode::PeerRefused, stream.Peer(),
                    "answered 503: " + std::to_string(_maxPeers) + " peer(s) already in session"));
            }
            _Linger(std::move(stream), WriteHttpRefusal(503));
            continue;
        }

        SessionOptions options;
        options.side = Side::Server;
        options.path = _config.path;
        options.allowedOrigins = _config.allowedOrigins;
        options.maxMessageBytes = _config.maxMessageBytes;
        auto peer = std::make_unique<Peer>(std::move(options));
        peer->endpoint = stream.Peer();
        peer->stream = std::move(stream);
        peer->startedAt = now;
        _peers.push_back(std::move(peer));
    }
}

void
Endpoint::_StartConnect(double now, std::vector<Diagnostic>* diagnostics)
{
    TcpStream stream;
    std::string failure;
    const auto status = stream.Connect(_config.address, _config.port, &failure);
    if (status == TcpStream::ConnectStatus::Failed)
    {
        _ConnectFailed(now, failure, diagnostics);
        return;
    }
    SessionOptions options;
    options.side = Side::Client;
    options.path = _config.path;
    options.maxMessageBytes = _config.maxMessageBytes;
    options.host = _source;
    auto peer = std::make_unique<Peer>(std::move(options));
    peer->endpoint = stream.Peer().empty() ? _source : stream.Peer();
    peer->stream = std::move(stream);
    peer->startedAt = now;
    peer->connecting = status == TcpStream::ConnectStatus::InProgress;
    _peers.push_back(std::move(peer));
}

void
Endpoint::_ConnectFailed(double now, const std::string& detail,
                         std::vector<Diagnostic>* diagnostics)
{
    // Once per episode, as the transport reports silence (§3): the episode
    // ends when a handshake completes.
    if (!_connectFailureReported && diagnostics)
    {
        diagnostics->push_back(_Make(DiagnosticCode::ConnectFailed, _source,
                                     detail + "; retrying every " +
                                         Seconds(_config.reconnectMaxSeconds) + "s at most"));
    }
    _connectFailureReported = true;
    _ScheduleRetry(now);
}

void
Endpoint::_ScheduleRetry(double now)
{
    if (_config.role != WebSocketRole::Connect)
    {
        return;
    }
    _nextAttempt = now + _backoff;
    _backoff = std::min(_backoff * 2.0, _config.reconnectMaxSeconds);
}

void
Endpoint::_ServicePeer(Peer& peer, std::vector<EndpointEvent>* events,
                       std::vector<Diagnostic>* diagnostics)
{
    if (peer.done)
    {
        return;
    }
    if (peer.connecting)
    {
        std::string failure;
        const auto status = peer.stream.FinishConnect(&failure);
        if (status == TcpStream::ConnectStatus::InProgress)
        {
            if (Now() - peer.startedAt <= _config.handshakeTimeoutSeconds)
            {
                return;
            }
            failure = "the connection did not complete within " +
                      Seconds(_config.handshakeTimeoutSeconds) + "s";
        }
        if (status != TcpStream::ConnectStatus::Connected)
        {
            peer.stream.Close();
            peer.done = true;
            _ConnectFailed(Now(), failure, diagnostics);
            return;
        }
        peer.connecting = false;
        // The handshake's own clock starts when the connection does (§4.3).
        peer.startedAt = Now();
    }

    // Read up to the budget, handing each read to the session before the
    // next, so a session that has ended stops being read from.
    const std::size_t budget =
        std::max<std::size_t>(_config.readBudgetMessages, 1) * _config.maxMessageBytes;
    std::size_t total = 0;
    bool streamEnded = false;
    std::string streamFailure;
    while (total < budget && !peer.session.HasEnded())
    {
        std::size_t got = 0;
        const IoStatus status = peer.stream.Read(
            _readBuffer.data(), std::min(_readBuffer.size(), budget - total), &got, &streamFailure);
        if (status == IoStatus::WouldBlock)
        {
            break;
        }
        if (status != IoStatus::Ok)
        {
            streamEnded = true;
            if (status == IoStatus::Closed)
            {
                streamFailure.clear();
            }
            break;
        }
        total += got;
        const double readAt = Now();
        peer.session.Receive(std::string_view(_readBuffer.data(), got));

        SessionEvent event;
        while (peer.session.Next(&event))
        {
            switch (event.kind)
            {
            case SessionEvent::Kind::Opened:
                peer.open = true;
                if (_config.role == WebSocketRole::Connect)
                {
                    _backoff = _config.reconnectInitialSeconds;
                    _connectFailureReported = false;
                }
                if (events)
                {
                    events->push_back({EndpointEvent::Kind::PeerOpened, peer.endpoint, {}, readAt});
                }
                break;
            case SessionEvent::Kind::Message:
                if (events)
                {
                    events->push_back({EndpointEvent::Kind::Message,
                                       peer.endpoint,
                                       std::move(event.text),
                                       readAt});
                }
                break;
            case SessionEvent::Kind::Refused:
                if (diagnostics)
                {
                    std::string detail = event.detail;
                    if (event.httpStatus != 0 && _config.role == WebSocketRole::Listen)
                    {
                        detail = "answered " + std::to_string(event.httpStatus) + ": " + detail;
                    }
                    diagnostics->push_back(
                        event.refusal == Refusal::Origin
                            ? _Make(DiagnosticCode::OriginRefused, event.origin, std::move(detail))
                            : _Make(DiagnosticCode::HandshakeRefused, peer.endpoint,
                                    std::move(detail)));
                }
                break;
            case SessionEvent::Kind::Fault:
                if (diagnostics)
                {
                    diagnostics->push_back(
                        _Make(CodeFor(event.fault), "byte " + std::to_string(event.byte),
                              event.detail + "; closed with " +
                                  std::to_string(event.closeStatus)));
                }
                break;
            case SessionEvent::Kind::Closed:
                if (diagnostics)
                {
                    diagnostics->push_back(
                        _Make(DiagnosticCode::PeerDisconnected, peer.endpoint,
                              "the peer closed with " + std::to_string(event.closeStatus)));
                }
                break;
            }
        }
    }

    if (peer.session.HasEnded())
    {
        // Refused, faulted or closed: what it owes the peer is in its
        // outbound, and is delivered while it lingers.
        _ScheduleRetry(Now());
        _Retire(peer, events, true);
        return;
    }
    if (streamEnded)
    {
        if (diagnostics)
        {
            if (peer.open)
            {
                diagnostics->push_back(_Make(
                    DiagnosticCode::PeerDisconnected, peer.endpoint,
                    streamFailure.empty()
                        ? std::string("the connection ended without a close frame")
                        : "the connection was reset: " + streamFailure));
            }
            else
            {
                diagnostics->push_back(
                    _Make(DiagnosticCode::HandshakeRefused, peer.endpoint,
                          "the connection ended before the handshake completed"));
            }
        }
        _ScheduleRetry(Now());
        _Retire(peer, events, false);
        return;
    }
    if (!peer.open && Now() - peer.startedAt > _config.handshakeTimeoutSeconds)
    {
        if (diagnostics)
        {
            diagnostics->push_back(_Make(DiagnosticCode::HandshakeRefused, peer.endpoint,
                                         "the handshake did not complete within " +
                                             Seconds(_config.handshakeTimeoutSeconds) + "s"));
        }
        _ScheduleRetry(Now());
        _Retire(peer, events, false);
        return;
    }
    _Flush(peer, events, diagnostics);
}

bool
Endpoint::_Flush(Peer& peer, std::vector<EndpointEvent>* events,
                 std::vector<Diagnostic>* diagnostics)
{
    if (peer.done || peer.connecting)
    {
        return false;
    }
    for (;;)
    {
        std::string& out = peer.session.Outbound();
        if (out.empty())
        {
            if (!peer.open || peer.queue.empty())
            {
                return true;
            }
            // A message starts here; from now on it is never dropped.
            peer.session.SendText(*peer.queue.front());
            peer.queue.pop_front();
            ++peer.sent;
            continue;
        }
        std::size_t written = 0;
        std::string failure;
        const IoStatus status = peer.stream.Write(out.data(), out.size(), &written, &failure);
        out.erase(0, written);
        if (status == IoStatus::WouldBlock)
        {
            return true;
        }
        if (status != IoStatus::Ok)
        {
            if (diagnostics)
            {
                diagnostics->push_back(
                    peer.open ? _Make(DiagnosticCode::PeerDisconnected, peer.endpoint,
                                      "the connection was reset: " + failure)
                              : _Make(DiagnosticCode::HandshakeRefused, peer.endpoint,
                                      "the connection ended before the handshake completed"));
            }
            _ScheduleRetry(Now());
            _Retire(peer, events, false);
            return false;
        }
    }
}

void
Endpoint::_Retire(Peer& peer, std::vector<EndpointEvent>* events, bool linger)
{
    if (peer.open && events)
    {
        events->push_back({EndpointEvent::Kind::PeerClosed, peer.endpoint, {}, Now()});
    }
    peer.open = false;
    peer.done = true;
    if (linger && !peer.session.Outbound().empty())
    {
        _Linger(std::move(peer.stream), std::move(peer.session.Outbound()));
    }
    else
    {
        peer.stream.Close();
    }
}

void
Endpoint::_Linger(TcpStream stream, std::string outbound)
{
    if (_lingering.size() >= kMaxLingering)
    {
        stream.Close();
        return;
    }
    Lingering lingering;
    lingering.stream = std::move(stream);
    lingering.outbound = std::move(outbound);
    lingering.deadline = Now() + kLingerSeconds;
    _lingering.push_back(std::move(lingering));
}

void
Endpoint::_ServiceLingering(double now)
{
    for (Lingering& lingering : _lingering)
    {
        bool finished = now >= lingering.deadline;
        while (!finished && !lingering.outbound.empty())
        {
            std::size_t written = 0;
            const IoStatus status = lingering.stream.Write(
                lingering.outbound.data(), lingering.outbound.size(), &written, nullptr);
            lingering.outbound.erase(0, written);
            if (status == IoStatus::WouldBlock)
            {
                break;
            }
            finished = status != IoStatus::Ok;
        }
        if (!finished && lingering.outbound.empty())
        {
            if (!lingering.shutdown)
            {
                lingering.stream.ShutdownWrite();
                lingering.shutdown = true;
            }
            // Read until the peer closes, so the socket does not close with
            // unread input and reset away what was just written.
            for (int reads = 0; reads < 8; ++reads)
            {
                std::size_t got = 0;
                const IoStatus status =
                    lingering.stream.Read(_readBuffer.data(), _readBuffer.size(), &got, nullptr);
                if (status == IoStatus::WouldBlock)
                {
                    break;
                }
                if (status != IoStatus::Ok)
                {
                    finished = true;
                    break;
                }
            }
        }
        if (finished)
        {
            lingering.stream.Close();
        }
    }
    _lingering.erase(std::remove_if(_lingering.begin(), _lingering.end(),
                                    [](const Lingering& lingering)
                                    { return !lingering.stream.IsOpen(); }),
                     _lingering.end());
}

void
Endpoint::Enqueue(const std::shared_ptr<const std::string>& text)
{
    const std::size_t bound = std::max<std::size_t>(_config.peerQueueMessages, 1);
    for (const auto& peer : _peers)
    {
        if (!peer->open || peer->done)
        {
            continue;
        }
        while (peer->queue.size() >= bound)
        {
            // The oldest that has not started, counted for this peer (§3).
            peer->queue.pop_front();
            ++peer->dropped;
        }
        peer->queue.push_back(text);
    }
}

std::size_t
Endpoint::OpenPeerCount() const
{
    return static_cast<std::size_t>(
        std::count_if(_peers.begin(), _peers.end(),
                      [](const std::unique_ptr<Peer>& peer) { return peer->open && !peer->done; }));
}

std::vector<WebSocketPeerStats>
Endpoint::GetPeerStats() const
{
    std::vector<WebSocketPeerStats> stats;
    for (const auto& peer : _peers)
    {
        if (!peer->open || peer->done)
        {
            continue;
        }
        WebSocketPeerStats entry;
        entry.endpoint = peer->endpoint;
        entry.queuedMessages = peer->queue.size();
        entry.sentMessages = peer->sent;
        entry.droppedMessages = peer->dropped;
        stats.push_back(std::move(entry));
    }
    return stats;
}

} // namespace openstrata::connectors::websocket::internal
