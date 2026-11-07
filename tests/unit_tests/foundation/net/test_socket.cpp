#include "net/Socket.hpp"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using foundation::net::AcceptClient;
    using foundation::net::ConnectLoopback;
    using foundation::net::LocalPort;
    using foundation::net::NetworkSession;
    using foundation::net::OpenLoopbackListener;
    using foundation::net::PollEntry;
    using foundation::net::SocketIo;
    using foundation::net::SocketIoKind;
    using foundation::net::SocketResult;
    using foundation::net::TcpSocket;

    constexpr std::chrono::seconds k_Deadline{ 10 };

    class SocketTest : public ::testing::Test
    {
    protected:
        void SetUp() override;

        /** @brief Connects server to client over a new listener's port. */
        void MakeConnectedPair();

        std::optional<NetworkSession> session;
        TcpSocket client;
        TcpSocket server;
    };

    [[nodiscard]] std::string ReceiveText(TcpSocket& socket, size_t byte_count);
    [[nodiscard]] SocketIoKind WaitForEnd(TcpSocket& socket);
}

TEST_F(SocketTest, BytesCrossInBothDirections)
{
    MakeConnectedPair();

    const std::string_view request{ "request" };
    const std::string_view reply{ "the reply" };

    const SocketIo sentRequest = client.Send(std::as_bytes(std::span{ request }));
    const SocketIo sentReply = server.Send(std::as_bytes(std::span{ reply }));

    ASSERT_EQ(sentRequest.Kind, SocketIoKind::Transferred);
    ASSERT_EQ(sentRequest.Bytes, 7);
    ASSERT_EQ(sentReply.Kind, SocketIoKind::Transferred);

    EXPECT_EQ(ReceiveText(server, request.size()), request);
    EXPECT_EQ(ReceiveText(client, reply.size()), reply);
}

TEST_F(SocketTest, ReceiveWithNoDataWouldBlock)
{
    MakeConnectedPair();

    std::array<std::byte, 16u> bytes{};

    EXPECT_EQ(client.Receive(bytes).Kind, SocketIoKind::WouldBlock);
}

TEST_F(SocketTest, ReceiveReportsThePeerClosing)
{
    MakeConnectedPair();

    server.Close();

    EXPECT_EQ(WaitForEnd(client), SocketIoKind::Closed);
}

TEST_F(SocketTest, PollReportsReadableOnlyWhenDataWaits)
{
    MakeConnectedPair();

    std::array<PollEntry, 1u> entries{ PollEntry{ .Socket = &client, .WantRead = true } };

    foundation::net::Poll(entries, 1);
    EXPECT_FALSE(entries[0].Readable);

    const std::string_view text{ "x" };

    ASSERT_EQ(server.Send(std::as_bytes(std::span{ text })).Kind, SocketIoKind::Transferred);

    foundation::net::Poll(entries, static_cast<int32_t>(std::chrono::milliseconds{ k_Deadline }.count()));
    EXPECT_TRUE(entries[0].Readable);
}

TEST_F(SocketTest, PollWithNoInterestStillReportsAHangup)
{
    MakeConnectedPair();

    server.Close();

    std::array<PollEntry, 1u> entries{ PollEntry{ .Socket = &client } };

    foundation::net::Poll(entries, static_cast<int32_t>(std::chrono::milliseconds{ k_Deadline }.count()));

    EXPECT_TRUE(entries[0].Failed);
    EXPECT_FALSE(entries[0].Readable);
}

TEST_F(SocketTest, AcceptWithNoClientReturnsNothing)
{
    const std::expected<TcpSocket, SocketResult> listener = OpenLoopbackListener(0);

    ASSERT_TRUE(listener.has_value());
    EXPECT_FALSE(AcceptClient(*listener).has_value());
}

TEST_F(SocketTest, TheSystemChoosesAPortAndNoOtherListenerCanShareIt)
{
    const std::expected<TcpSocket, SocketResult> first = OpenLoopbackListener(0);

    ASSERT_TRUE(first.has_value());

    const int32_t port = LocalPort(*first);

    EXPECT_GT(port, 0);

    const std::expected<TcpSocket, SocketResult> second = OpenLoopbackListener(port);

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), SocketResult::PortInUse);
}

TEST_F(SocketTest, RefusesAPortOutOfRange)
{
    EXPECT_EQ(OpenLoopbackListener(-1).error(), SocketResult::InvalidPort);
    EXPECT_EQ(OpenLoopbackListener(65536).error(), SocketResult::InvalidPort);
    EXPECT_EQ(ConnectLoopback(0).error(), SocketResult::InvalidPort);
}

TEST_F(SocketTest, ConnectWithNoListenerFails)
{
    std::optional<int32_t> freePort;

    {
        const std::expected<TcpSocket, SocketResult> listener = OpenLoopbackListener(0);

        ASSERT_TRUE(listener.has_value());
        freePort = LocalPort(*listener);
    }

    const std::expected<TcpSocket, SocketResult> connected = ConnectLoopback(*freePort);

    ASSERT_FALSE(connected.has_value());
    EXPECT_EQ(connected.error(), SocketResult::ConnectFailed);
}

TEST_F(SocketTest, MovingASocketLeavesTheSourceClosed)
{
    MakeConnectedPair();

    TcpSocket moved{ std::move(client) };

    EXPECT_FALSE(client.IsOpen());
    EXPECT_TRUE(moved.IsOpen());
}

namespace
{
    void SocketTest::SetUp()
    {
        std::expected<NetworkSession, SocketResult> started = NetworkSession::Start();

        ASSERT_TRUE(started.has_value());
        session.emplace(std::move(*started));
    }

    void SocketTest::MakeConnectedPair()
    {
        const std::expected<TcpSocket, SocketResult> listener = OpenLoopbackListener(0);

        ASSERT_TRUE(listener.has_value());

        std::expected<TcpSocket, SocketResult> connected = ConnectLoopback(LocalPort(*listener));

        ASSERT_TRUE(connected.has_value());
        client = std::move(*connected);

        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_Deadline;

        while (std::chrono::steady_clock::now() < deadline)
        {
            std::optional<TcpSocket> accepted = AcceptClient(*listener);

            if (accepted.has_value())
            {
                server = std::move(*accepted);
                return;
            }

            std::this_thread::yield();
        }

        FAIL() << "the listener never saw the client";
    }

    std::string ReceiveText(TcpSocket& socket, size_t byte_count)
    {
        std::string text;
        std::array<std::byte, 64u> bytes{};
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_Deadline;

        while (text.size() < byte_count && std::chrono::steady_clock::now() < deadline)
        {
            const SocketIo received = socket.Receive(bytes);

            if (received.Kind == SocketIoKind::Transferred)
            {
                text.append(reinterpret_cast<const char*>(bytes.data()), static_cast<size_t>(received.Bytes));
            }
        }

        return text;
    }

    SocketIoKind WaitForEnd(TcpSocket& socket)
    {
        std::array<std::byte, 16u> bytes{};
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_Deadline;

        while (std::chrono::steady_clock::now() < deadline)
        {
            const SocketIo received = socket.Receive(bytes);

            if (received.Kind != SocketIoKind::WouldBlock)
            {
                return received.Kind;
            }
        }

        return SocketIoKind::WouldBlock;
    }
}
