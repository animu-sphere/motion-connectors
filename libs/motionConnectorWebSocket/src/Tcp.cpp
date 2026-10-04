// SPDX-License-Identifier: Apache-2.0
//
// The one file in this library that includes a platform header.

#include "Tcp.h"

#include <chrono>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
// After winsock2.h, always.
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace openstrata::connectors::websocket::internal
{

namespace
{

#if defined(_WIN32)

using SocketHandle = SOCKET;

// Winsock is initialised once per process; WSAStartup counts, so this and the
// transport's own start-up do not interfere.
bool
EnsureSocketsUsable()
{
    struct Startup
    {
        Startup()
        {
            WSADATA data;
            ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~Startup()
        {
            if (ok)
            {
                WSACleanup();
            }
        }
        bool ok = false;
    };
    static Startup startup;
    return startup.ok;
}

int
LastSocketError()
{
    return WSAGetLastError();
}

std::string
SocketErrorText(int code)
{
    char* text = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&text), 0, nullptr);
    std::string message;
    if (length != 0 && text)
    {
        message.assign(text, length);
    }
    if (text)
    {
        LocalFree(text);
    }
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
    {
        message.pop_back();
    }
    if (message.empty())
    {
        message = "socket error " + std::to_string(code);
    }
    return message;
}

void
CloseSocket(SocketHandle handle)
{
    closesocket(handle);
}

bool
SetNonBlocking(SocketHandle handle)
{
    u_long mode = 1;
    return ioctlsocket(handle, FIONBIO, &mode) == 0;
}

bool
ErrorIsWouldBlock(int code)
{
    return code == WSAEWOULDBLOCK;
}

bool
ErrorIsInProgress(int code)
{
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
}

bool
ErrorIsInterrupted(int code)
{
    return code == WSAEINTR;
}

bool
AcceptErrorIsTransient(int code)
{
    return code == WSAECONNRESET || code == WSAEINTR || code == WSAEMFILE || code == WSAENOBUFS;
}

constexpr int kSendFlags = 0;
constexpr int kShutdownWrite = SD_SEND;

#else

using SocketHandle = int;

bool
EnsureSocketsUsable()
{
    return true;
}

int
LastSocketError()
{
    return errno;
}

std::string
SocketErrorText(int code)
{
    return std::strerror(code);
}

void
CloseSocket(SocketHandle handle)
{
    ::close(handle);
}

bool
SetNonBlocking(SocketHandle handle)
{
    const int flags = ::fcntl(handle, F_GETFL, 0);
    return flags != -1 && ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool
ErrorIsWouldBlock(int code)
{
    return code == EAGAIN || code == EWOULDBLOCK;
}

bool
ErrorIsInProgress(int code)
{
    return code == EINPROGRESS;
}

bool
ErrorIsInterrupted(int code)
{
    return code == EINTR;
}

bool
AcceptErrorIsTransient(int code)
{
    return code == ECONNABORTED || code == EINTR || code == EPROTO || code == EMFILE ||
           code == ENFILE || code == ENOBUFS || code == ENOMEM || code == EPERM;
}

// A write to a peer that has gone raises SIGPIPE by default, which would end
// the host process for a peer's absence. Linux suppresses it per call; macOS
// per socket (`SO_NOSIGPIPE`, below).
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
constexpr int kShutdownWrite = SHUT_WR;

#endif

SocketHandle
ToHandle(std::intptr_t socket)
{
    return static_cast<SocketHandle>(socket);
}

std::string
FormatEndpoint(const sockaddr* address, std::size_t length)
{
    char host[64] = {};
    char service[16] = {};
    if (::getnameinfo(address, static_cast<socklen_t>(length), host, sizeof(host), service,
                      sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    {
        return std::string();
    }
    std::string endpoint =
        address->sa_family == AF_INET6 ? "[" + std::string(host) + "]" : std::string(host);
    endpoint += ":";
    endpoint += service;
    return endpoint;
}

// A stream socket's options: non-blocking always; no Nagle delay, because a
// motion frame is small and late is worse than a few more packets; no SIGPIPE
// where it is a socket option.
bool
ConfigureStream(SocketHandle handle)
{
    if (!SetNonBlocking(handle))
    {
        return false;
    }
    const int on = 1;
    ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
#if defined(SO_NOSIGPIPE)
    ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    return true;
}

bool
Resolve(const std::string& address, std::uint16_t port, bool passive, addrinfo** out)
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // No DNS: an address is a literal, so an unresolvable string fails here
    // rather than after a resolver timeout (§3).
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | (passive ? AI_PASSIVE : 0);
    const std::string service = std::to_string(port);
    *out = nullptr;
    if (::getaddrinfo(address.c_str(), service.c_str(), &hints, out) != 0 || !*out)
    {
        if (*out)
        {
            ::freeaddrinfo(*out);
            *out = nullptr;
        }
        return false;
    }
    return true;
}

void
SetError(std::string* error, std::string text)
{
    if (error)
    {
        *error = std::move(text);
    }
}

} // namespace

double
SteadySeconds() noexcept
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// TcpStream
// ---------------------------------------------------------------------------

TcpStream::~TcpStream()
{
    Close();
}

TcpStream::TcpStream(TcpStream&& other) noexcept
    : _socket(std::exchange(other._socket, -1)), _peer(std::move(other._peer))
{
}

TcpStream&
TcpStream::operator=(TcpStream&& other) noexcept
{
    if (this != &other)
    {
        Close();
        _socket = std::exchange(other._socket, -1);
        _peer = std::move(other._peer);
    }
    return *this;
}

TcpStream::ConnectStatus
TcpStream::Connect(const std::string& address, std::uint16_t port, std::string* error)
{
    Close();
    if (!EnsureSocketsUsable())
    {
        SetError(error, "the platform's socket layer could not be initialised");
        return ConnectStatus::Failed;
    }
    addrinfo* resolved = nullptr;
    if (!Resolve(address, port, false, &resolved))
    {
        SetError(error, "the peer address is not a numeric address this host understands");
        return ConnectStatus::Failed;
    }

    const SocketHandle handle =
        ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (static_cast<std::intptr_t>(handle) == -1)
    {
        SetError(error, SocketErrorText(LastSocketError()));
        ::freeaddrinfo(resolved);
        return ConnectStatus::Failed;
    }
    if (!ConfigureStream(handle))
    {
        SetError(error, SocketErrorText(LastSocketError()));
        CloseSocket(handle);
        ::freeaddrinfo(resolved);
        return ConnectStatus::Failed;
    }
    _peer = FormatEndpoint(resolved->ai_addr, resolved->ai_addrlen);
    const int result =
        ::connect(handle, resolved->ai_addr, static_cast<socklen_t>(resolved->ai_addrlen));
    const int code = result == 0 ? 0 : LastSocketError();
    ::freeaddrinfo(resolved);

    _socket = static_cast<std::intptr_t>(handle);
    if (result == 0)
    {
        return ConnectStatus::Connected;
    }
    if (ErrorIsInProgress(code))
    {
        return ConnectStatus::InProgress;
    }
    SetError(error, SocketErrorText(code));
    Close();
    return ConnectStatus::Failed;
}

TcpStream::ConnectStatus
TcpStream::FinishConnect(std::string* error)
{
    if (_socket == -1)
    {
        SetError(error, "no connect is in progress");
        return ConnectStatus::Failed;
    }
    const SocketHandle handle = ToHandle(_socket);
#if defined(_WIN32)
    // select rather than WSAPoll: WSAPoll on older Windows releases never
    // reports a failed connect, and a connect role would wait out its timeout
    // against a peer that had already refused it.
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(handle, &writable);
    FD_SET(handle, &failed);
    timeval zero = {0, 0};
    const int ready = ::select(0, nullptr, &writable, &failed, &zero);
    if (ready == 0)
    {
        return ConnectStatus::InProgress;
    }
    if (ready < 0)
    {
        SetError(error, SocketErrorText(LastSocketError()));
        Close();
        return ConnectStatus::Failed;
    }
#else
    pollfd descriptor = {};
    descriptor.fd = handle;
    descriptor.events = POLLOUT;
    const int ready = ::poll(&descriptor, 1, 0);
    if (ready == 0)
    {
        return ConnectStatus::InProgress;
    }
    if (ready < 0)
    {
        const int code = LastSocketError();
        if (ErrorIsInterrupted(code))
        {
            return ConnectStatus::InProgress;
        }
        SetError(error, SocketErrorText(code));
        Close();
        return ConnectStatus::Failed;
    }
#endif
    int pending = 0;
    socklen_t length = sizeof(pending);
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending), &length) !=
            0 ||
        pending != 0)
    {
        SetError(error, SocketErrorText(pending != 0 ? pending : LastSocketError()));
        Close();
        return ConnectStatus::Failed;
    }
    return ConnectStatus::Connected;
}

IoStatus
TcpStream::Read(char* buffer, std::size_t capacity, std::size_t* got, std::string* error)
{
    *got = 0;
    if (_socket == -1)
    {
        SetError(error, "the connection is closed");
        return IoStatus::Failed;
    }
    for (;;)
    {
        const auto received =
            ::recv(ToHandle(_socket),
                   buffer,
                   static_cast<int>(capacity > 0x7fffffff ? 0x7fffffff : capacity),
                   0);
        if (received > 0)
        {
            *got = static_cast<std::size_t>(received);
            return IoStatus::Ok;
        }
        if (received == 0)
        {
            return IoStatus::Closed;
        }
        const int code = LastSocketError();
        if (ErrorIsInterrupted(code))
        {
            continue;
        }
        if (ErrorIsWouldBlock(code))
        {
            return IoStatus::WouldBlock;
        }
        SetError(error, SocketErrorText(code));
        return IoStatus::Failed;
    }
}

IoStatus
TcpStream::Write(const char* data, std::size_t size, std::size_t* written, std::string* error)
{
    *written = 0;
    if (_socket == -1)
    {
        SetError(error, "the connection is closed");
        return IoStatus::Failed;
    }
    while (*written < size)
    {
        const std::size_t remaining = size - *written;
        const auto sent =
            ::send(ToHandle(_socket), data + *written,
                   static_cast<int>(remaining > 0x7fffffff ? 0x7fffffff : remaining), kSendFlags);
        if (sent >= 0)
        {
            *written += static_cast<std::size_t>(sent);
            continue;
        }
        const int code = LastSocketError();
        if (ErrorIsInterrupted(code))
        {
            continue;
        }
        if (ErrorIsWouldBlock(code))
        {
            return IoStatus::WouldBlock;
        }
        SetError(error, SocketErrorText(code));
        return IoStatus::Failed;
    }
    return IoStatus::Ok;
}

void
TcpStream::ShutdownWrite() noexcept
{
    if (_socket != -1)
    {
        ::shutdown(ToHandle(_socket), kShutdownWrite);
    }
}

void
TcpStream::Close() noexcept
{
    if (_socket != -1)
    {
        CloseSocket(ToHandle(_socket));
        _socket = -1;
    }
}

// ---------------------------------------------------------------------------
// TcpListener
// ---------------------------------------------------------------------------

TcpListener::~TcpListener()
{
    Close();
}

bool
TcpListener::Open(const std::string& address, std::uint16_t port, std::string* error)
{
    Close();
    if (!EnsureSocketsUsable())
    {
        SetError(error, "the platform's socket layer could not be initialised");
        return false;
    }
    addrinfo* resolved = nullptr;
    if (!Resolve(address, port, true, &resolved))
    {
        SetError(error, "the listen address is not a numeric address this host understands");
        return false;
    }

    std::string failure;
    for (const addrinfo* candidate = resolved; candidate; candidate = candidate->ai_next)
    {
        const SocketHandle handle =
            ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (static_cast<std::intptr_t>(handle) == -1)
        {
            failure = SocketErrorText(LastSocketError());
            continue;
        }
        const int on = 1;
#if defined(_WIN32)
        // Windows' SO_REUSEADDR lets a second process take a port that is
        // already served; exclusive use is what refuses that.
        ::setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on),
                     sizeof(on));
#else
        // POSIX SO_REUSEADDR only allows a bind over connections still in
        // TIME_WAIT, so a restarted listener can take its port back at once.
        ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
        const auto length = static_cast<socklen_t>(candidate->ai_addrlen);
        if (::bind(handle, candidate->ai_addr, length) != 0 || ::listen(handle, 16) != 0 ||
            !SetNonBlocking(handle))
        {
            failure = SocketErrorText(LastSocketError());
            CloseSocket(handle);
            continue;
        }
        _socket = static_cast<std::intptr_t>(handle);
        break;
    }
    ::freeaddrinfo(resolved);
    if (_socket == -1)
    {
        SetError(error, failure.empty() ? std::string("the address could not be bound") : failure);
        return false;
    }

    sockaddr_storage bound = {};
    socklen_t length = sizeof(bound);
    if (::getsockname(ToHandle(_socket), reinterpret_cast<sockaddr*>(&bound), &length) == 0)
    {
        _bound = FormatEndpoint(reinterpret_cast<const sockaddr*>(&bound),
                                static_cast<std::size_t>(length));
    }
    else
    {
        _bound = address + ":" + std::to_string(port);
    }
    return true;
}

void
TcpListener::Close() noexcept
{
    if (_socket != -1)
    {
        CloseSocket(ToHandle(_socket));
        _socket = -1;
    }
    _bound.clear();
}

TcpListener::AcceptStatus
TcpListener::Accept(TcpStream* out, std::string* error)
{
    if (_socket == -1)
    {
        SetError(error, "the listener is closed");
        return AcceptStatus::Failed;
    }
    sockaddr_storage from = {};
    socklen_t length = sizeof(from);
    const SocketHandle handle =
        ::accept(ToHandle(_socket), reinterpret_cast<sockaddr*>(&from), &length);
    if (static_cast<std::intptr_t>(handle) == -1)
    {
        const int code = LastSocketError();
        if (ErrorIsWouldBlock(code) || AcceptErrorIsTransient(code))
        {
            return AcceptStatus::None;
        }
        SetError(error, SocketErrorText(code));
        return AcceptStatus::Failed;
    }
    if (!ConfigureStream(handle))
    {
        CloseSocket(handle);
        return AcceptStatus::None;
    }
    out->Close();
    out->_socket = static_cast<std::intptr_t>(handle);
    out->_peer =
        FormatEndpoint(reinterpret_cast<const sockaddr*>(&from), static_cast<std::size_t>(length));
    return AcceptStatus::Accepted;
}

} // namespace openstrata::connectors::websocket::internal
