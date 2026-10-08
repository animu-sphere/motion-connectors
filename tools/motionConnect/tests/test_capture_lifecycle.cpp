// SPDX-License-Identifier: Apache-2.0
#include "motionConnectorMocopi/Connector.h"
#include "motionConnectorVmc/Connector.h"
#include "motionConnectorVrchatOsc/Connector.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

namespace core = openstrata::connectors::core;

void
Check(bool condition, const char* what)
{
    if (!condition)
        throw std::runtime_error(what);
}

class Sender {
public:
    Sender()
    {
#if defined(_WIN32)
        WSADATA data;
        Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup");
        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(_socket != INVALID_SOCKET, "sender socket");
#else
        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(_socket >= 0, "sender socket");
#endif
    }
    ~Sender()
    {
#if defined(_WIN32)
        closesocket(_socket);
        WSACleanup();
#else
        close(_socket);
#endif
    }
    void Send(const std::string& endpoint)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(
            static_cast<unsigned short>(std::stoul(endpoint.substr(endpoint.rfind(':') + 1))));
        // Deliberately invalid protocol input: capture is below decoding.
        const char payload[] = "bad";
        Check(sendto(_socket,
                     payload,
                     3,
                     0,
                     reinterpret_cast<const sockaddr*>(&address),
                     sizeof(address)) == 3,
              "sendto");
    }

private:
#if defined(_WIN32)
    SOCKET _socket = INVALID_SOCKET;
#else
    int _socket = -1;
#endif
};

template <typename Connector>
void
Receive(Connector& connector)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    core::MotionFrame frame;
    do {
        connector.Poll(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
}

template <typename Connector>
void
Lifecycle(const std::string& profile)
{
    Connector connector;
    core::ConnectorConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = 0;
    config.sourceProfile = profile;
    Check(static_cast<bool>(connector.Open(config)), "connector open");
    Sender sender;
    sender.Send(connector.GetEndpoint());
    Receive(connector);
    Check(connector.GetCapture().datagrams.empty(), "capture must be opt-in");

    connector.StartCapture();
    Check(connector.GetCapture().listenEndpoint == connector.GetEndpoint(), "actual endpoint");
    const std::uint8_t replay[] = {'x'};
    connector.PushDatagram(replay, sizeof(replay), 10.0);
    Check(connector.GetCapture().datagrams.empty(), "replay injection must be excluded");
    sender.Send(connector.GetEndpoint());
    Receive(connector);
    Check(connector.GetCapture().datagrams.size() == 1, "live refused input is captured");
    const auto saved = connector.GetCapture().datagrams.front();
    Check(saved.bytes == std::vector<std::uint8_t>({'b', 'a', 'd'}) && !saved.peer.empty() &&
              saved.receiveTime >= 0.0,
          "bytes peer and receive clock");

    connector.StopCapture();
    sender.Send(connector.GetEndpoint());
    Receive(connector);
    Check(connector.GetCapture().datagrams.size() == 1, "StopCapture retains and stops");
    connector.StartCapture();
    Check(connector.GetCapture().datagrams.empty(), "StartCapture resets");
    sender.Send(connector.GetEndpoint());
    Receive(connector);
    Check(connector.GetCapture().datagrams.size() == 1, "capture can restart");
    connector.Close();
    Check(connector.GetCapture().datagrams.size() == 1, "Close retains saved input");
    Check(static_cast<bool>(connector.Open(config)), "reopen");
    sender.Send(connector.GetEndpoint());
    Receive(connector);
    Check(connector.GetCapture().datagrams.size() == 1, "Open stops previous capture");
    connector.StartCapture();
    Check(connector.GetCapture().datagrams.empty(), "new session resets on StartCapture");
    connector.Close();
}

} // namespace

int
main()
{
    try {
        Lifecycle<openstrata::connectors::vmc::VmcConnector>("vmc.v1");
        Lifecycle<openstrata::connectors::mocopi::MocopiConnector>("mocopi.body.v1");
        Lifecycle<openstrata::connectors::vrchatOsc::VrchatOscConnector>("vrchat-osc.trackers.v1");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
