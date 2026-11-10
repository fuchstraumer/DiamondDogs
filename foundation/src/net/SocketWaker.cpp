#include "net/Socket.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace
{
    constexpr std::chrono::seconds k_AcceptDeadline{ 5 };
    constexpr int32_t k_AcceptPollMilliseconds = 100;
    constexpr size_t k_DrainChunkBytes = 256u;
}

namespace foundation::net
{
    std::expected<SocketWaker, SocketResult> SocketWaker::Create() noexcept
    {
        const std::expected<TcpSocket, SocketResult> listener = OpenLoopbackListener(0);

        if (!listener.has_value())
        {
            return std::unexpected(listener.error());
        }

        std::expected<TcpSocket, SocketResult> writer = ConnectLoopback(LocalPort(*listener));

        if (!writer.has_value())
        {
            return std::unexpected(writer.error());
        }

        const int32_t writerPort = LocalPort(*writer);
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_AcceptDeadline;

        while (std::chrono::steady_clock::now() < deadline)
        {
            std::array<PollEntry, 1u> entries{ PollEntry{ .Socket = &*listener, .WantRead = true } };

            Poll(entries, k_AcceptPollMilliseconds);

            std::optional<TcpSocket> accepted = AcceptClient(*listener);

            // the port is open to this whole machine for a moment, so another process can connect first
            if (accepted.has_value() && PeerPort(*accepted) == writerPort)
            {
                SocketWaker waker{};

                waker.writer = std::move(*writer);
                waker.reader = std::move(*accepted);

                return waker;
            }
        }

        return std::unexpected(SocketResult::SocketFailed);
    }

    void SocketWaker::Wake() noexcept
    {
        const std::byte signal{ 1u };

        // a full send buffer means that many wakes wait already, so this one has nothing to add
        static_cast<void>(writer.Send(std::span{ &signal, 1u }));
    }

    void SocketWaker::Drain() noexcept
    {
        std::array<std::byte, k_DrainChunkBytes> bytes{};

        while (reader.Receive(bytes).Kind == SocketIoKind::Transferred)
        {
        }
    }

    const TcpSocket& SocketWaker::WaitSocket() const noexcept
    {
        return reader;
    }
}
