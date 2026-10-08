// SPDX-License-Identifier: Apache-2.0
//
// A non-blocking TCP listener and stream, on the OS sockets the transport
// already uses (docs/design/WEBSOCKET_CONNECTOR.md §10). Caller-driven: no
// thread, no callback, and no call that waits. Private to this library.
//
// Sockets are kept as integers so that Winsock's macros reach Tcp.cpp alone,
// as `motionConnectorTransport`'s `UdpReceiver.h` keeps its own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace openstrata::connectors::websocket::internal
{

// Seconds on a steady clock, from an arbitrary origin.
double SteadySeconds() noexcept;

// Validate a literal without DNS or opening a socket.
bool IsNumericAddress(const std::string& address);

enum class IoStatus : std::uint8_t {
    Ok,
    // Nothing to read, or no room to write. Not an error.
    WouldBlock,
    // The peer closed its half in order.
    Closed,
    // Reset, or any other failure; the error text says which.
    Failed,
};

class TcpStream
{
  public:
    TcpStream() = default;
    ~TcpStream();

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    TcpStream(TcpStream&& other) noexcept;
    TcpStream& operator=(TcpStream&& other) noexcept;

    enum class ConnectStatus : std::uint8_t
    {
        Connected,
        InProgress,
        Failed,
    };

    // Starts a connect to a numeric address and returns without waiting.
    ConnectStatus Connect(const std::string& address, std::uint16_t port, std::string* error);
    // Whether a connect that was `InProgress` has completed, without waiting.
    ConnectStatus FinishConnect(std::string* error);

    IoStatus Read(char* buffer, std::size_t capacity, std::size_t* got, std::string* error);
    IoStatus Write(const char* data, std::size_t size, std::size_t* written, std::string* error);

    // Sends a FIN and keeps reading possible, so what was written is not lost
    // to a reset when the socket closes with unread input.
    void ShutdownWrite() noexcept;
    void Close() noexcept;

    bool
    IsOpen() const noexcept
    {
        return _socket != -1;
    }

    // The peer, numerically: "127.0.0.1:52001" or "[::1]:52001".
    const std::string&
    Peer() const noexcept
    {
        return _peer;
    }

  private:
    friend class TcpListener;

    std::intptr_t _socket = -1;
    std::string _peer;
};

class TcpListener
{
  public:
    TcpListener() = default;
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    bool Open(const std::string& address, std::uint16_t port, std::string* error);
    void Close() noexcept;

    bool
    IsOpen() const noexcept
    {
        return _socket != -1;
    }

    // What the socket got, the OS-chosen port included.
    const std::string&
    BoundEndpoint() const noexcept
    {
        return _bound;
    }

    enum class AcceptStatus : std::uint8_t
    {
        Accepted,
        // Nothing waiting, or a connection that was aborted before it could
        // be accepted.
        None,
        // The listening socket failed.
        Failed,
    };

    AcceptStatus Accept(TcpStream* out, std::string* error);

  private:
    std::intptr_t _socket = -1;
    std::string _bound;
};

} // namespace openstrata::connectors::websocket::internal
