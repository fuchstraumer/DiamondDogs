#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_NET_SOCKET_HPP
#define DIAMOND_DOGS_FOUNDATION_NET_SOCKET_HPP
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace foundation::net
{
    enum class SocketResult : uint8_t
    {
        Invalid = 0,
        Success = 1,
        NetworkStartFailed,
        InvalidPort,
        SocketFailed,
        PortInUse,
        ConnectFailed,
    };

    [[nodiscard]] std::string_view ToString(SocketResult result) noexcept;

    enum class SocketIoKind : uint8_t
    {
        Invalid = 0,
        Transferred,
        WouldBlock,
        Closed,
    };

    struct SocketIo
    {
        SocketIoKind Kind{ SocketIoKind::Invalid };
        int32_t Bytes{ 0 };
    };

    /** Keeps the system's network layer started. Every socket must close before the last session ends. */
    class NetworkSession
    {
    public:
        [[nodiscard]] static std::expected<NetworkSession, SocketResult> Start() noexcept;

        NetworkSession(const NetworkSession&) = delete;
        NetworkSession& operator=(const NetworkSession&) = delete;
        NetworkSession(NetworkSession&& _other) noexcept;
        NetworkSession& operator=(NetworkSession&& _other) noexcept;
        ~NetworkSession();

    private:
        NetworkSession() noexcept = default;

        bool active{ false };
    };

    /** A non-blocking TCP socket. Send and Receive return at once, with WouldBlock when no progress is possible. */
    class TcpSocket
    {
    public:
        TcpSocket() noexcept = default;
        explicit TcpSocket(uintptr_t _handle) noexcept;
        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;
        TcpSocket(TcpSocket&& _other) noexcept;
        TcpSocket& operator=(TcpSocket&& _other) noexcept;
        ~TcpSocket();

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] uintptr_t Handle() const noexcept;
        void Close() noexcept;
        /** @brief Returns Closed when the peer closed the connection or the connection failed. */
        [[nodiscard]] SocketIo Receive(std::span<std::byte> bytes) noexcept;
        /** @brief Winsock accepts a send of any size while its send buffer has room, which moves the bytes into
         *  kernel memory. A caller that limits its backlog must also limit the size of each send. */
        [[nodiscard]] SocketIo Send(std::span<const std::byte> bytes) noexcept;

    private:
        // the value of INVALID_SOCKET, which this header cannot name
        uintptr_t handle{ ~uintptr_t{ 0u } };
    };

    /** One socket to wait on. Poll writes the last three fields. */
    struct PollEntry
    {
        const TcpSocket* Socket{ nullptr };
        bool WantRead{ false };
        bool WantWrite{ false };
        bool Readable{ false };
        bool Writable{ false };
        bool Failed{ false };
    };

    inline constexpr int32_t k_MaxPollEntries = 8;

    /** @brief Binds 127.0.0.1 and listens. Port 0 lets the system choose. No other process can share the port.
     *  Only the loopback address exists here, so that a tool port cannot reach the network by accident. */
    [[nodiscard]] std::expected<TcpSocket, SocketResult> OpenLoopbackListener(int32_t port) noexcept;
    /** @brief Connects to 127.0.0.1, and blocks until the connection exists or fails. */
    [[nodiscard]] std::expected<TcpSocket, SocketResult> ConnectLoopback(int32_t port) noexcept;
    /** @brief Returns the port that the socket is bound to, or -1 when it has none. */
    [[nodiscard]] int32_t LocalPort(const TcpSocket& socket) noexcept;
    /** @brief Returns the next client, or nothing when no client waits. */
    [[nodiscard]] std::optional<TcpSocket> AcceptClient(const TcpSocket& listener) noexcept;
    /** @brief Waits until one entry is ready, or until the timeout ends. Takes up to k_MaxPollEntries entries.
     *  An entry can want neither read nor write, and it still reports Failed. */
    void Poll(std::span<PollEntry> entries, int32_t timeout_milliseconds) noexcept;
}

#endif // !DIAMOND_DOGS_FOUNDATION_NET_SOCKET_HPP
