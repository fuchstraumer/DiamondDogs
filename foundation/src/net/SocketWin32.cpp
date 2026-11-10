#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "net/Socket.hpp"
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    constexpr uintptr_t k_ClosedSocket = static_cast<uintptr_t>(INVALID_SOCKET);
    constexpr int32_t k_MaxPort = 65535;

    [[nodiscard]] bool MakeNonBlocking(SOCKET socket) noexcept;
    [[nodiscard]] bool DisableNagle(SOCKET socket) noexcept;
    [[nodiscard]] sockaddr_in MakeLoopbackAddress(int32_t port) noexcept;
    [[nodiscard]] foundation::net::SocketIo ReadIoResult(int count) noexcept;
}

namespace foundation::net
{
    std::string_view ToString(SocketResult result) noexcept
    {
        switch (result)
        {
        case SocketResult::Invalid: return "Invalid";
        case SocketResult::Success: return "Success";
        case SocketResult::NetworkStartFailed: return "NetworkStartFailed";
        case SocketResult::InvalidPort: return "InvalidPort";
        case SocketResult::SocketFailed: return "SocketFailed";
        case SocketResult::PortInUse: return "PortInUse";
        case SocketResult::ConnectFailed: return "ConnectFailed";
        }

        return "Invalid";
    }

    std::expected<NetworkSession, SocketResult> NetworkSession::Start() noexcept
    {
        WSADATA data{};

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return std::unexpected(SocketResult::NetworkStartFailed);
        }

        NetworkSession session{};

        session.active = true;

        return session;
    }

    NetworkSession::NetworkSession(NetworkSession&& _other) noexcept :
        active{ std::exchange(_other.active, false) }
    {}

    NetworkSession& NetworkSession::operator=(NetworkSession&& _other) noexcept
    {
        if (this != &_other)
        {
            if (active)
            {
                WSACleanup();
            }

            active = std::exchange(_other.active, false);
        }

        return *this;
    }

    NetworkSession::~NetworkSession()
    {
        if (active)
        {
            WSACleanup();
        }
    }

    TcpSocket::TcpSocket(uintptr_t _handle) noexcept :
        handle{ _handle }
    {}

    TcpSocket::TcpSocket(TcpSocket&& _other) noexcept :
        handle{ std::exchange(_other.handle, k_ClosedSocket) }
    {}

    TcpSocket& TcpSocket::operator=(TcpSocket&& _other) noexcept
    {
        if (this != &_other)
        {
            Close();
            handle = std::exchange(_other.handle, k_ClosedSocket);
        }

        return *this;
    }

    TcpSocket::~TcpSocket()
    {
        Close();
    }

    bool TcpSocket::IsOpen() const noexcept
    {
        return handle != k_ClosedSocket;
    }

    uintptr_t TcpSocket::Handle() const noexcept
    {
        return handle;
    }

    void TcpSocket::Close() noexcept
    {
        if (handle != k_ClosedSocket)
        {
            closesocket(static_cast<SOCKET>(handle));
            handle = k_ClosedSocket;
        }
    }

    SocketIo TcpSocket::Receive(std::span<std::byte> bytes) noexcept
    {
        const int count = recv(static_cast<SOCKET>(handle),
                               reinterpret_cast<char*>(bytes.data()),
                               static_cast<int>(bytes.size()),
                               0);

        if (count == 0)
        {
            return SocketIo{ .Kind = SocketIoKind::Closed };
        }

        return ReadIoResult(count);
    }

    SocketIo TcpSocket::Send(std::span<const std::byte> bytes) noexcept
    {
        const int count = send(static_cast<SOCKET>(handle),
                               reinterpret_cast<const char*>(bytes.data()),
                               static_cast<int>(bytes.size()),
                               0);

        return ReadIoResult(count);
    }

    std::expected<TcpSocket, SocketResult> OpenLoopbackListener(int32_t port) noexcept
    {
        if (port < 0 || port > k_MaxPort)
        {
            return std::unexpected(SocketResult::InvalidPort);
        }

        TcpSocket listener{ static_cast<uintptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) };

        if (!listener.IsOpen())
        {
            return std::unexpected(SocketResult::SocketFailed);
        }

        const SOCKET handle = static_cast<SOCKET>(listener.Handle());
        const BOOL exclusive = TRUE;

        // without this flag, another process on this machine can bind the same port and steal the connections
        if (setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0)
        {
            return std::unexpected(SocketResult::SocketFailed);
        }

        const sockaddr_in address = MakeLoopbackAddress(port);

        if (bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            const int error = WSAGetLastError();
            const bool portTaken = error == WSAEADDRINUSE || error == WSAEACCES;

            return std::unexpected(portTaken ? SocketResult::PortInUse : SocketResult::SocketFailed);
        }

        if (listen(handle, SOMAXCONN) != 0 || !MakeNonBlocking(handle))
        {
            return std::unexpected(SocketResult::SocketFailed);
        }

        return listener;
    }

    std::expected<TcpSocket, SocketResult> ConnectLoopback(int32_t port) noexcept
    {
        if (port <= 0 || port > k_MaxPort)
        {
            return std::unexpected(SocketResult::InvalidPort);
        }

        TcpSocket client{ static_cast<uintptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) };

        if (!client.IsOpen())
        {
            return std::unexpected(SocketResult::SocketFailed);
        }

        const SOCKET handle = static_cast<SOCKET>(client.Handle());
        const sockaddr_in address = MakeLoopbackAddress(port);

        if (connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            return std::unexpected(SocketResult::ConnectFailed);
        }

        if (!DisableNagle(handle) || !MakeNonBlocking(handle))
        {
            return std::unexpected(SocketResult::SocketFailed);
        }

        return client;
    }

    int32_t LocalPort(const TcpSocket& socket) noexcept
    {
        sockaddr_in address{};
        int addressLength = sizeof(address);

        if (getsockname(static_cast<SOCKET>(socket.Handle()),
                        reinterpret_cast<sockaddr*>(&address),
                        &addressLength) != 0)
        {
            return -1;
        }

        return static_cast<int32_t>(ntohs(address.sin_port));
    }

    std::optional<TcpSocket> AcceptClient(const TcpSocket& listener) noexcept
    {
        const SOCKET client = accept(static_cast<SOCKET>(listener.Handle()), nullptr, nullptr);

        if (client == INVALID_SOCKET)
        {
            return std::nullopt;
        }

        TcpSocket accepted{ static_cast<uintptr_t>(client) };

        if (!DisableNagle(client) || !MakeNonBlocking(client))
        {
            return std::nullopt;
        }

        return accepted;
    }

    int32_t PeerPort(const TcpSocket& socket) noexcept
    {
        sockaddr_in address{};
        int addressLength = sizeof(address);

        if (getpeername(static_cast<SOCKET>(socket.Handle()),
                        reinterpret_cast<sockaddr*>(&address),
                        &addressLength) != 0)
        {
            return -1;
        }

        return static_cast<int32_t>(ntohs(address.sin_port));
    }

    void Poll(std::span<PollEntry> entries, int32_t timeout_milliseconds) noexcept
    {
        assert(std::ssize(entries) <= k_MaxPollEntries);

        std::array<WSAPOLLFD, k_MaxPollEntries> sockets{};
        ULONG socketCount = 0u;

        for (const PollEntry& entry : entries)
        {
            const SHORT read = entry.WantRead ? POLLRDNORM : SHORT{ 0 };
            const SHORT write = entry.WantWrite ? POLLWRNORM : SHORT{ 0 };

            sockets[socketCount].fd = static_cast<SOCKET>(entry.Socket->Handle());
            sockets[socketCount].events = static_cast<SHORT>(read | write);
            ++socketCount;
        }

        if (WSAPoll(sockets.data(), socketCount, timeout_milliseconds) == SOCKET_ERROR)
        {
            // keeps the caller's loop from spinning. Every socket call reports its own failure next
            std::this_thread::sleep_for(std::chrono::milliseconds{ timeout_milliseconds });
            return;
        }

        for (ULONG index = 0u; index < socketCount; ++index)
        {
            PollEntry& entry = entries[index];
            const SHORT ready = sockets[index].revents;

            entry.Readable = (ready & POLLRDNORM) != 0;
            entry.Writable = (ready & POLLWRNORM) != 0;
            entry.Failed = (ready & (POLLHUP | POLLERR | POLLNVAL)) != 0;
        }
    }
}

namespace
{
    bool MakeNonBlocking(SOCKET socket) noexcept
    {
        u_long enabled = 1u;

        return ioctlsocket(socket, FIONBIO, &enabled) == 0;
    }

    bool DisableNagle(SOCKET socket) noexcept
    {
        const BOOL noDelay = TRUE;

        // Nagle's algorithm and delayed acknowledgement together add about 40 ms to a small reply
        return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                          reinterpret_cast<const char*>(&noDelay), sizeof(noDelay)) == 0;
    }

    sockaddr_in MakeLoopbackAddress(int32_t port) noexcept
    {
        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<u_short>(port));

        return address;
    }

    foundation::net::SocketIo ReadIoResult(int count) noexcept
    {
        if (count != SOCKET_ERROR)
        {
            return foundation::net::SocketIo{ .Kind = foundation::net::SocketIoKind::Transferred, .Bytes = count };
        }

        if (WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return foundation::net::SocketIo{ .Kind = foundation::net::SocketIoKind::WouldBlock };
        }

        return foundation::net::SocketIo{ .Kind = foundation::net::SocketIoKind::Closed };
    }
}
