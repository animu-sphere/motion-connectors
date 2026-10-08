// SPDX-License-Identifier: Apache-2.0
//
// Both directions, in both roles, over real loopback sockets in one process
// (WEBSOCKET_CONNECTOR.md §3): a sender that listens and a connector that
// connects, and the reverse. Then what only a socket can show: the `Origin`
// rule, the one-peer bound and its 503, a connect role that retries and
// reports once per episode, a reconnect reported as a restart, the silence
// timeout, the sender's queue, raw capture, and a port already taken.
//
// Every bind is loopback on an OS-chosen port. This binary binds sockets, so
// it has a CTest name of its own, which a lane that forbids binds excludes.
#include "Session.h"
#include "Tcp.h"

#include "motionConnectorWebSocket/Connector.h"
#include "motionConnectorWebSocket/FrameSender.h"
#include "motionConnectorWebSocket/PacketCapture.h"

#include "motionConnectorWire/FrameWire.h"

#include "motionCore/MotionPose.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

namespace core = openstrata::connectors::core;
namespace motion = openstrata::motion;
namespace websocket = openstrata::connectors::websocket;
namespace internal = openstrata::connectors::websocket::internal;
namespace wire = openstrata::connectors::wire;

int failures = 0;

void
Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++failures;
    }
}

// Runs `step` until `done` holds or `seconds` pass. Windows takes about two
// seconds to report a refused loopback connect, so no deadline here is short.
bool
Pump(const std::function<bool()>& done, const std::function<void()>& step, double seconds = 15.0)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        step();
        if (done())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::uint16_t
PortOf(const std::string& endpoint)
{
    return static_cast<std::uint16_t>(std::stoul(endpoint.substr(endpoint.rfind(':') + 1)));
}

std::size_t
Count(const std::vector<websocket::Diagnostic>& diagnostics, std::string_view name)
{
    std::size_t count = 0;
    for (const auto& diagnostic : diagnostics)
    {
        count += websocket::DiagnosticName(diagnostic) == name ? 1 : 0;
    }
    return count;
}

const websocket::Diagnostic*
Find(const std::vector<websocket::Diagnostic>& diagnostics, std::string_view name)
{
    for (const auto& diagnostic : diagnostics)
    {
        if (websocket::DiagnosticName(diagnostic) == name)
        {
            return &diagnostic;
        }
    }
    return nullptr;
}

core::MotionFrame
BodyFrame(std::uint64_t number)
{
    core::MotionFrame frame;
    frame.frameNumber = number;
    frame.sourceProfile = "example.body.v1";
    frame.timing.sourceTimestamp = number / 60.0;
    frame.timing.receiveTimestamp = 99.0;
    frame.timing.sourceClock = core::ClockDomain::Device;
    core::ActorFrame actor;
    actor.actor = "0";
    motion::MotionPose pose;
    pose.timestamp = number / 60.0;
    pose.localRotations[static_cast<std::size_t>(motion::HumanJoint::Hips)] =
        pxr::GfQuatf(0.6f, 0.8f, 0.0f, 0.0f);
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanJoint::Hips));
    actor.pose = pose;
    frame.actors.push_back(std::move(actor));
    return frame;
}

core::ConnectorConfig
Lossless()
{
    core::ConnectorConfig config;
    config.bufferMode = core::BufferMode::Lossless;
    config.bufferCapacity = 1024;
    return config;
}

websocket::WebSocketConfig
Listen()
{
    websocket::WebSocketConfig config;
    config.role = websocket::WebSocketRole::Listen;
    config.address = "127.0.0.1";
    config.port = 0;
    return config;
}

websocket::WebSocketConfig
ConnectTo(std::uint16_t port)
{
    websocket::WebSocketConfig config;
    config.role = websocket::WebSocketRole::Connect;
    config.address = "127.0.0.1";
    config.port = port;
    config.reconnectInitialSeconds = 0.05;
    config.reconnectMaxSeconds = 0.2;
    return config;
}

// Sends frames until the connector has delivered `count`, and returns them.
std::vector<core::MotionFrame>
Transfer(websocket::WebSocketFrameSender& sender, websocket::WebSocketConnector& connector,
         std::uint64_t first, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        Check(sender.Send(BodyFrame(first + i)), "send");
    }
    std::vector<core::MotionFrame> received;
    Pump([&] { return received.size() >= count; },
         [&]
         {
             sender.Service();
             core::MotionFrame frame;
             while (connector.Poll(frame))
             {
                 received.push_back(frame);
             }
         });
    return received;
}

void
CheckFrames(const std::vector<core::MotionFrame>& received, std::uint64_t firstSender,
            std::uint64_t firstOwn, const std::string& what)
{
    for (std::size_t i = 0; i < received.size(); ++i)
    {
        const core::MotionFrame expected = BodyFrame(firstSender + i);
        Check(received[i].frameNumber == firstOwn + i, what + ": the connector's numbering");
        Check(received[i].timing.receiveTimestamp != 99.0, what + ": the receiver's clock");
        Check(received[i].timing.sourceTimestamp == expected.timing.sourceTimestamp &&
                  received[i].actors.size() == 1 && received[i].actors[0].pose &&
                  *received[i].actors[0].pose == *expected.actors[0].pose,
              what + ": the frame carried verbatim");
    }
}

// ---------------------------------------------------------------------------

void
TestConnectorListensSenderConnects()
{
    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), Listen())), "connector listens");
    Check(connector.GetState() == core::ConnectorState::Connecting, "listening is connecting");

    websocket::WebSocketFrameSender sender;
    Check(sender.Open(ConnectTo(PortOf(connector.GetEndpoint()))), "sender connects");
    core::MotionFrame unused;
    Check(Pump([&] { return sender.GetPeerCount() == 1 && !connector.GetPeer().empty(); },
               [&]
               {
                   sender.Service();
                   connector.Poll(unused);
               }),
          "handshake both ways");
    Check(connector.GetState() == core::ConnectorState::Connecting,
          "a handshake alone is still connecting");

    connector.StartCapture();
    const auto received = Transfer(sender, connector, 41, 3);
    Check(received.size() == 3, "three frames arrive");
    CheckFrames(received, 41, 1, "listen/receive");
    Check(connector.GetState() == core::ConnectorState::Connected, "a frame connects");

    // §9: each message as received, with its peer, and the capture round-trips.
    const auto& capture = connector.GetCapture();
    Check(capture.datagrams.size() == 3 && capture.datagrams[0].peer == connector.GetPeer(),
          "three messages captured with their peer");
    std::stringstream file;
    Check(websocket::WritePacketCapture(file, capture), "capture writes");
    websocket::PacketCapture read;
    Check(websocket::ReadPacketCapture(file, &read) && read.datagrams.size() == 3 &&
              read.datagrams[2].bytes == capture.datagrams[2].bytes,
          "capture reads back");

    connector.StopCapture();
    Check(Transfer(sender, connector, 44, 1).size() == 1, "traffic continues after StopCapture");
    Check(connector.GetCapture().datagrams.size() == 3, "StopCapture retains and stops");
    connector.StartCapture();
    Check(connector.GetCapture().datagrams.empty(), "StartCapture resets saved input");
    Check(Transfer(sender, connector, 45, 1).size() == 1, "capture can restart");
    Check(connector.GetCapture().datagrams.size() == 1, "one new live message captured");

    // Going away: 1001, a transition and never an error (§5, §6).
    sender.Close();
    Check(
        Pump([&] { return Count(connector.GetDiagnostics(), "WEBSOCKET_PEER_DISCONNECTED") == 1; },
             [&] { connector.Poll(unused); }),
        "the receiver sees the peer go");
    const auto* gone = Find(connector.GetDiagnostics(), "WEBSOCKET_PEER_DISCONNECTED");
    Check(gone && gone->detail.find("1001") != std::string::npos, "with 1001");
    Check(connector.GetState() == core::ConnectorState::Connecting, "and waits for the next");
    connector.Close();
    Check(connector.GetCapture().datagrams.size() == 1, "Close retains saved input");
    Check(static_cast<bool>(connector.Open(Lossless(), Listen())), "reopen for capture");
    Check(sender.Open(ConnectTo(PortOf(connector.GetEndpoint()))), "sender reopens");
    Check(Pump([&] { return sender.GetPeerCount() == 1 && !connector.GetPeer().empty(); },
               [&] {
                   sender.Service();
                   connector.Poll(unused);
               }),
          "handshake after reopen");
    Check(Transfer(sender, connector, 1, 1).size() == 1, "traffic after reopen");
    Check(connector.GetCapture().datagrams.size() == 1, "Open stops previous capture");
}

void
TestSenderListensConnectorConnects()
{
    websocket::WebSocketFrameSender sender;
    Check(sender.Open(Listen()), "sender listens");

    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), ConnectTo(PortOf(sender.GetEndpoint())))),
          "connector connects");
    core::MotionFrame unused;
    Check(Pump([&] { return sender.GetPeerCount() == 1; },
               [&]
               {
                   sender.Service();
                   connector.Poll(unused);
               }),
          "handshake both ways");
    const auto received = Transfer(sender, connector, 7, 4);
    Check(received.size() == 4, "four frames arrive");
    CheckFrames(received, 7, 1, "connect/receive");

    connector.Close();
    Check(Pump([&] { return sender.GetPeerCount() == 0; }, [&] { sender.Service(); }),
          "the sender drops the peer that left");
    Check(Count(sender.GetDiagnostics(), "WEBSOCKET_PEER_DISCONNECTED") == 1,
          "and says so once");
}

void
TestSenderServesSeveralPeers()
{
    websocket::WebSocketConfig config = Listen();
    config.maxPeers = 2;
    config.peerQueueMessages = 2;
    websocket::WebSocketFrameSender sender;
    Check(sender.Open(config), "sender listens");
    const std::uint16_t port = PortOf(sender.GetEndpoint());

    websocket::WebSocketConnector first;
    websocket::WebSocketConnector second;
    websocket::WebSocketConnector third;
    Check(first.Open(Lossless(), ConnectTo(port)) && second.Open(Lossless(), ConnectTo(port)),
          "two connect");
    core::MotionFrame unused;
    Check(Pump([&] { return sender.GetPeerCount() == 2; },
               [&]
               {
                   sender.Service();
                   first.Poll(unused);
                   second.Poll(unused);
               }),
          "both in session");

    // A third is over the bound: 503 before the upgrade (§3).
    Check(static_cast<bool>(third.Open(Lossless(), ConnectTo(port))), "a third connects");
    Check(Pump([&]
               {
                   return Count(sender.GetDiagnostics(), "WEBSOCKET_PEER_REFUSED") >= 1 &&
                          Count(third.GetDiagnostics(), "WEBSOCKET_HANDSHAKE_REFUSED") >= 1;
               },
               [&]
               {
                   sender.Service();
                   first.Poll(unused);
                   second.Poll(unused);
                   third.Poll(unused);
               }),
          "the third is refused with 503");
    const auto* refused = Find(third.GetDiagnostics(), "WEBSOCKET_HANDSHAKE_REFUSED");
    Check(refused && refused->detail.find("503") != std::string::npos, "and is told 503");
    third.Close();

    // One encoding, both peers. Three sends before one `Service`, into queues
    // of two: each peer drops its oldest message that had not started and
    // counts it (§3).
    for (std::uint64_t n = 1; n <= 3; ++n)
    {
        Check(sender.Send(BodyFrame(n)), "send");
    }
    std::vector<core::MotionFrame> firstFrames;
    std::vector<core::MotionFrame> secondFrames;
    Pump([&] { return firstFrames.size() == 2 && secondFrames.size() == 2; },
         [&]
         {
             sender.Service();
             core::MotionFrame frame;
             while (first.Poll(frame))
             {
                 firstFrames.push_back(frame);
             }
             while (second.Poll(frame))
             {
                 secondFrames.push_back(frame);
             }
         });
    Check(firstFrames.size() == 2 && secondFrames.size() == 2, "both receive the newest two");
    const auto frameTwo = BodyFrame(2).timing.sourceTimestamp;
    Check(firstFrames.size() == 2 && firstFrames[0].timing.sourceTimestamp == frameTwo &&
              secondFrames[0].timing.sourceTimestamp == frameTwo,
          "the oldest is the one dropped");
    const auto stats = sender.GetPeerStats();
    Check(stats.size() == 2 && stats[0].sentMessages == 2 && stats[1].sentMessages == 2 &&
              stats[0].droppedMessages == 1 && stats[1].droppedMessages == 1 &&
              stats[0].queuedMessages == 0 && stats[0].pendingBytes == 0 &&
              stats[1].pendingBytes == 0,
          "and the queues say so");

    // A frame the encoder refuses goes to nobody (§7.1).
    core::MotionFrame broken = BodyFrame(4);
    broken.timing.sourceTimestamp = std::numeric_limits<double>::infinity();
    Check(!sender.Send(broken) && Count(sender.GetDiagnostics(), "WIRE_VALUE_NOT_FINITE") == 1,
          "an encoder refusal is reported and sent to nobody");
}

// A listener of the test's own, speaking the server side through the private
// session, so a test can drop a connection and keep listening.
struct ManualServer
{
    internal::TcpListener listener;
    internal::TcpStream stream;
    std::optional<internal::Session> session;

    bool
    Accept()
    {
        std::string error;
        if (listener.Accept(&stream, &error) != internal::TcpListener::AcceptStatus::Accepted)
        {
            return false;
        }
        session.emplace(internal::SessionOptions{});
        return true;
    }

    // Reads, writes, and reports whether the session has opened.
    bool
    Service()
    {
        char buffer[4096];
        std::size_t got = 0;
        while (stream.Read(buffer, sizeof(buffer), &got, nullptr) == internal::IoStatus::Ok)
        {
            session->Receive(std::string_view(buffer, got));
        }
        internal::SessionEvent event;
        while (session->Next(&event))
        {
        }
        std::string& out = session->Outbound();
        std::size_t written = 0;
        stream.Write(out.data(), out.size(), &written, nullptr);
        out.erase(0, written);
        return session->IsOpen();
    }

    void
    Send(const core::MotionFrame& frame)
    {
        std::string text;
        wire::EncodeFrame(frame, &text);
        session->SendText(text);
        Service();
    }
};

void
TestConnectRoleRetriesAndRestarts()
{
    // A port nobody serves yet.
    ManualServer server;
    Check(server.listener.Open("127.0.0.1", 0, nullptr), "find a port");
    const std::uint16_t port = PortOf(server.listener.BoundEndpoint());
    server.listener.Close();

    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), ConnectTo(port))), "connect role opens");
    core::MotionFrame frame;
    Check(Pump([&] { return Count(connector.GetDiagnostics(), "WEBSOCKET_CONNECT_FAILED") >= 1; },
               [&] { connector.Poll(frame); }),
          "a refused connect is reported");
    // Several more attempts fail in the same episode, and say nothing more.
    Pump([] { return false; }, [&] { connector.Poll(frame); }, 3.0);
    Check(Count(connector.GetDiagnostics(), "WEBSOCKET_CONNECT_FAILED") == 1,
          "once per episode");
    Check(connector.GetState() == core::ConnectorState::Connecting, "and keeps trying");

    // Now someone listens; the connector finds it on its next attempt.
    Check(server.listener.Open("127.0.0.1", port, nullptr), "listen on that port");
    Check(Pump([&] { return server.stream.IsOpen() || server.Accept(); },
               [&] { connector.Poll(frame); }),
          "the retry arrives");
    Check(Pump([&] { return server.Service(); }, [&] { connector.Poll(frame); }), "and upgrades");

    std::vector<core::MotionFrame> received;
    const auto drain = [&]
    {
        server.Service();
        while (connector.Poll(frame))
        {
            received.push_back(frame);
        }
    };
    server.Send(BodyFrame(1));
    server.Send(BodyFrame(2));
    Pump([&] { return received.size() == 2; }, drain);

    // The connection is lost without a close frame, and the connector comes
    // back by itself; the first frame of the new connection is a restart even
    // though the sender's numbering carries on (§7).
    server.stream.Close();
    server.session.reset();
    Check(Pump([&] { return server.Accept(); }, [&] { connector.Poll(frame); }), "reconnects");
    Check(Pump([&] { return server.Service(); }, [&] { connector.Poll(frame); }),
          "and upgrades again");
    server.Send(BodyFrame(3));
    Pump([&] { return received.size() == 3; }, drain);

    Check(received.size() == 3 && received[2].frameNumber == 3,
          "numbering is monotone across connections");
    Check(Count(connector.GetDiagnostics(), "WEBSOCKET_PEER_DISCONNECTED") == 1,
          "the loss is reported");
    const auto* restart = Find(connector.GetDiagnostics(), "WEBSOCKET_SOURCE_RESTARTED");
    Check(restart && restart->subject == "3", "the new connection is a restart");
    Check(Count(connector.GetDiagnostics(), "WEBSOCKET_FRAME_GAP") == 0, "and not a gap");
}

// Writes `request` on a raw connection and returns the status line it got.
std::string
RawRequest(websocket::WebSocketConnector& connector, const std::string& request)
{
    internal::TcpStream stream;
    std::string error;
    auto status = stream.Connect("127.0.0.1", PortOf(connector.GetEndpoint()), &error);
    core::MotionFrame unused;
    Pump([&]
         {
             if (status == internal::TcpStream::ConnectStatus::InProgress)
             {
                 status = stream.FinishConnect(&error);
             }
             return status != internal::TcpStream::ConnectStatus::InProgress;
         },
         [&] { connector.Poll(unused); });
    std::size_t written = 0;
    stream.Write(request.data(), request.size(), &written, &error);
    std::string answer;
    Pump([&] { return answer.find("\r\n") != std::string::npos; },
         [&]
         {
             connector.Poll(unused);
             char buffer[512];
             std::size_t got = 0;
             if (stream.Read(buffer, sizeof(buffer), &got, nullptr) == internal::IoStatus::Ok)
             {
                 answer.append(buffer, got);
             }
         });
    return answer.substr(0, answer.find("\r\n"));
}

std::string
Upgrade(const std::string& extra, const std::string& path = "/")
{
    return "GET " + path +
           " HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
           "Sec-WebSocket-Protocol: openstrata.motion.frame.v1\r\n" +
           extra + "\r\n";
}

void
TestOriginAndPath()
{
    websocket::WebSocketConfig config = Listen();
    config.path = "/motion";
    config.allowedOrigins = {"https://studio.example"};
    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), config)), "listens");

    // §4.2: loopback does not stop a web page.
    Check(RawRequest(connector, Upgrade("Origin: https://ads.example\r\n", "/motion")) ==
              "HTTP/1.1 403 Forbidden",
          "a page's origin is refused");
    const auto* origin = Find(connector.GetDiagnostics(), "WEBSOCKET_ORIGIN_REFUSED");
    Check(origin && origin->subject == "https://ads.example", "naming the origin");

    Check(RawRequest(connector, Upgrade("", "/")) == "HTTP/1.1 404 Not Found",
          "another path is 404");
    Check(RawRequest(connector, Upgrade("Origin: https://studio.example\r\n", "/motion")) ==
              "HTTP/1.1 101 Switching Protocols",
          "an allowed origin upgrades");
}

void
TestOnePeerAtATime()
{
    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), Listen())), "listens");
    websocket::WebSocketFrameSender first;
    Check(first.Open(ConnectTo(PortOf(connector.GetEndpoint()))), "a first sender");
    core::MotionFrame unused;
    Check(Pump([&] { return first.GetPeerCount() == 1; },
               [&]
               {
                   first.Service();
                   connector.Poll(unused);
               }),
          "in session");

    Check(RawRequest(connector, Upgrade("")) == "HTTP/1.1 503 Service Unavailable",
          "a second peer is answered 503");
    Check(Count(connector.GetDiagnostics(), "WEBSOCKET_PEER_REFUSED") == 1, "and reported");
    Check(first.GetPeerCount() == 1, "the first is undisturbed");
}

void
TestSilence()
{
    websocket::WebSocketConfig config = Listen();
    config.silenceTimeoutSeconds = 0.2;
    websocket::WebSocketConnector connector;
    Check(static_cast<bool>(connector.Open(Lossless(), config)), "listens");
    websocket::WebSocketFrameSender sender;
    Check(sender.Open(ConnectTo(PortOf(connector.GetEndpoint()))), "connects");
    core::MotionFrame unused;
    Pump([&] { return sender.GetPeerCount() == 1; },
         [&]
         {
             sender.Service();
             connector.Poll(unused);
         });
    Check(Transfer(sender, connector, 1, 1).size() == 1, "one frame");
    Check(connector.GetState() == core::ConnectorState::Connected, "connected");

    Check(Pump([&] { return connector.GetState() == core::ConnectorState::Degraded; },
               [&]
               {
                   sender.Service();
                   connector.Poll(unused);
               }),
          "silence degrades");
    Pump([] { return false; },
         [&]
         {
             sender.Service();
             connector.Poll(unused);
         },
         0.5);
    Check(Count(connector.GetDiagnostics(), "WEBSOCKET_SOURCE_TIMEOUT") == 1, "once per episode");
    Check(Transfer(sender, connector, 2, 1).size() == 1, "a frame after silence");
    Check(connector.GetState() == core::ConnectorState::Connected, "connects again");
}

void
TestPortTaken()
{
    websocket::WebSocketConnector first;
    Check(static_cast<bool>(first.Open(Lossless(), Listen())), "the first binds");
    websocket::WebSocketConfig same = Listen();
    same.port = PortOf(first.GetEndpoint());
    websocket::WebSocketConnector second;
    Check(!second.Open(Lossless(), same), "the second cannot");
    Check(second.GetState() == core::ConnectorState::Error, "and is in error");
    Check(Count(second.GetDiagnostics(), "WEBSOCKET_SOCKET_BIND_FAILED") == 1,
          "with the bind code");
}

} // namespace

int
main()
{
    TestConnectorListensSenderConnects();
    TestSenderListensConnectorConnects();
    TestSenderServesSeveralPeers();
    TestConnectRoleRetriesAndRestarts();
    TestOriginAndPath();
    TestOnePeerAtATime();
    TestSilence();
    TestPortTaken();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("motionConnectorWebSocket loopback tests passed\n");
    return 0;
}
