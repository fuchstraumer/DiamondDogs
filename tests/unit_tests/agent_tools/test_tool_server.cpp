#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolServer.hpp"
#include "net/Socket.hpp"
#include "nlohmann/json.hpp"
#include "reflect/Describe.hpp"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    using Json = nlohmann::json;

    constexpr std::chrono::seconds k_ReplyDeadline{ 20 };
    constexpr std::chrono::milliseconds k_QuietPeriod{ 60 };
    constexpr int32_t k_LargeBudget = 100000;
    constexpr size_t k_BlobBytes = 512u * 1024u;

    struct Weather
    {
        float WindForce{ 5.0f };
    };

    struct AimLens
    {
        float Fov{ 0.0f };
    };

    struct AimParams
    {
        std::array<float, 2> Eye{};
        AimLens Lens{};
    };

    struct AimReply
    {
        float Sum{ 0.0f };
    };

    struct BlobReply
    {
        std::string Text;
    };

    struct AimCommand
    {
        static constexpr std::string_view Name{ "test.aim" };
        static constexpr std::string_view Doc{ "Returns the sum of its arguments." };
        using Params = AimParams;
        using Reply = AimReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);
    };

    struct HoldCommand
    {
        static constexpr std::string_view Name{ "test.hold" };
        static constexpr std::string_view Doc{ "Keeps the token until the test answers." };
        using Params = agent::NoParams;
        using Reply = agent::NoReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        std::vector<agent::ReplyToken<Reply>> Held;
    };

    struct BlobCommand
    {
        static constexpr std::string_view Name{ "test.blob" };
        static constexpr std::string_view Doc{ "Returns a large block of text." };
        using Params = agent::NoParams;
        using Reply = BlobReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        int32_t RunCount{ 0 };
    };

    /** A blocking client, as the Python sidecar is. Every read has a deadline, so that a bug fails a test
     *  instead of hanging it. */
    class TestClient
    {
    public:
        [[nodiscard]] bool Connect(int32_t port);
        void Close();
        void Send(std::string_view bytes);
        void SendOneByteAtATime(std::string_view bytes);
        /** @brief Returns one line if one arrives within the wait. */
        [[nodiscard]] std::optional<std::string> TryReadLine(std::chrono::milliseconds wait);
        [[nodiscard]] bool PeerClosed() const noexcept;

    private:
        foundation::net::TcpSocket socket;
        std::string received;
        bool peerClosed{ false };
    };

    class ToolServerTest : public ::testing::Test
    {
    protected:
        void SetUp() override;

        /** @brief Pumps until the client has one line, as a frame loop would. */
        [[nodiscard]] Json PumpUntilReply(TestClient& reader);
        /** @brief Pumps for a short time, and returns true if no line arrived. */
        [[nodiscard]] bool PumpAndExpectQuiet(TestClient& reader);

        // first, so that every socket below closes before it ends
        std::optional<foundation::net::NetworkSession> network;
        Weather weather{};
        AimCommand aim;
        HoldCommand hold;
        BlobCommand blob;
        agent::ToolRegistry registry;
        agent::ToolServer server;
        TestClient client;
    };

    [[nodiscard]] std::string MakeRequest(int64_t id, std::string_view method, std::string_view params_json);
    [[nodiscard]] std::string MakeSet(int64_t id, float wind_force);
    [[nodiscard]] std::string MakeHold(int64_t id);
    [[nodiscard]] std::string MakePing(int64_t id);
}

template<>
struct foundation::reflect::Describe<Weather>
{
    static constexpr std::string_view Name{ "test.Weather" };
    static constexpr std::tuple Fields
    {
        Field<Weather, float>{ "WindForce", &Weather::WindForce, { .Range = FieldRange{ 0.0, 12.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<AimLens>
{
    static constexpr std::string_view Name{ "test.AimLens" };
    static constexpr std::tuple Fields
    {
        Field<AimLens, float>{ "Fov", &AimLens::Fov },
    };
};

template<>
struct foundation::reflect::Describe<AimParams>
{
    static constexpr std::string_view Name{ "test.AimParams" };
    static constexpr std::tuple Fields
    {
        Field<AimParams, std::array<float, 2>>{ "Eye", &AimParams::Eye, { .Required = true } },
        Field<AimParams, AimLens>{ "Lens", &AimParams::Lens },
    };
};

template<>
struct foundation::reflect::Describe<AimReply>
{
    static constexpr std::string_view Name{ "test.AimReply" };
    static constexpr std::tuple Fields
    {
        Field<AimReply, float>{ "Sum", &AimReply::Sum },
    };
};

template<>
struct foundation::reflect::Describe<BlobReply>
{
    static constexpr std::string_view Name{ "test.BlobReply" };
    static constexpr std::tuple Fields
    {
        Field<BlobReply, std::string>{ "Text", &BlobReply::Text },
    };
};

TEST_F(ToolServerTest, PingAnswersWithoutAPump)
{
    client.Send(MakePing(1));

    const std::optional<std::string> line = client.TryReadLine(k_ReplyDeadline);

    ASSERT_TRUE(line.has_value());

    Json reply = Json::parse(*line);

    EXPECT_EQ(reply["id"], 1);
    EXPECT_EQ(reply["result"]["Pong"], true);
}

TEST_F(ToolServerTest, TheRegistryRunsOnlyInsidePump)
{
    client.Send(MakeSet(2, 7.0f));

    std::this_thread::sleep_for(k_QuietPeriod);
    EXPECT_EQ(weather.WindForce, 5.0f);

    Json reply = PumpUntilReply(client);

    EXPECT_EQ(reply["id"], 2);
    EXPECT_EQ(reply["result"], 7.0);
    EXPECT_EQ(weather.WindForce, 7.0f);
}

TEST_F(ToolServerTest, GetAndDescribeReachTheRegistry)
{
    client.Send(MakeRequest(3, "tool.get", R"({"path":"test.weather.WindForce"})"));

    EXPECT_EQ(PumpUntilReply(client)["result"], 5.0);

    client.Send(MakeRequest(4, "tool.describe", R"({"path":"test.aim"})"));

    EXPECT_TRUE(PumpUntilReply(client).contains("result"));

    client.Send(MakeRequest(5, "tool.describe", "{}"));

    Json all = PumpUntilReply(client);

    EXPECT_EQ(all["id"], 5);
    EXPECT_TRUE(all["result"].is_object());
}

TEST_F(ToolServerTest, CallFlattensItsArguments)
{
    client.Send(MakeRequest(6, "tool.call", R"({"name":"test.aim","arguments":{"Eye":[1,2],"Lens":{"Fov":4}}})"));

    Json reply = PumpUntilReply(client);

    EXPECT_EQ(reply["id"], 6);
    EXPECT_EQ(reply["result"]["Sum"], 7.0);
}

TEST_F(ToolServerTest, AToolFailureCarriesItsRecordInData)
{
    client.Send(MakeRequest(7, "tool.set", R"({"path":"test.weather.WindForse","value":1})"));

    Json reply = PumpUntilReply(client);

    EXPECT_EQ(reply["id"], 7);
    EXPECT_EQ(reply["error"]["code"], -32000);
    EXPECT_EQ(reply["error"]["data"]["Code"], "ValueRefused");
    EXPECT_EQ(reply["error"]["data"]["Suggestions"][0], "WindForce");
    EXPECT_FALSE(reply["error"]["message"].get<std::string>().empty());
}

TEST_F(ToolServerTest, ArgumentChecksComeBackAsToolFailures)
{
    client.Send(MakeRequest(8, "tool.call", R"({"name":"test.aim","arguments":{"Eye":[1]}})"));

    Json reply = PumpUntilReply(client);

    EXPECT_EQ(reply["error"]["code"], -32000);
    EXPECT_EQ(reply["error"]["data"]["Code"], "MissingArgument");
}

TEST_F(ToolServerTest, AMessageCanArriveOneByteAtATime)
{
    client.SendOneByteAtATime(MakeSet(9, 3.0f));

    EXPECT_EQ(PumpUntilReply(client)["id"], 9);
    EXPECT_EQ(weather.WindForce, 3.0f);
}

TEST_F(ToolServerTest, TwoMessagesCanArriveInOnePacket)
{
    client.Send(MakeSet(10, 1.0f) + MakeSet(11, 2.0f));

    EXPECT_EQ(PumpUntilReply(client)["id"], 10);
    EXPECT_EQ(PumpUntilReply(client)["id"], 11);
    EXPECT_EQ(weather.WindForce, 2.0f);
}

TEST_F(ToolServerTest, AMalformedLineDoesNotEndTheConnection)
{
    client.Send("this is not json\n");

    Json refusal = PumpUntilReply(client);

    EXPECT_TRUE(refusal["id"].is_null());
    EXPECT_EQ(refusal["error"]["code"], -32700);

    client.Send(MakePing(12));

    EXPECT_EQ(PumpUntilReply(client)["id"], 12);
}

TEST_F(ToolServerTest, AnOversizeLineIsRefusedAndTheNextLineWorks)
{
    const std::string oversize = std::string(2u * 1024u * 1024u, 'x') + "\n";

    client.Send(oversize);

    Json refusal = PumpUntilReply(client);

    EXPECT_EQ(refusal["error"]["code"], -32600);

    client.Send(MakePing(13));

    EXPECT_EQ(PumpUntilReply(client)["id"], 13);
}

TEST_F(ToolServerTest, ASecondClientIsRefusedAndTheFirstKeepsWorking)
{
    client.Send(MakePing(14));
    ASSERT_EQ(PumpUntilReply(client)["id"], 14);

    TestClient second;

    ASSERT_TRUE(second.Connect(server.Port()));

    Json refusal = PumpUntilReply(second);

    EXPECT_EQ(refusal["error"]["code"], -32001);

    client.Send(MakePing(15));

    EXPECT_EQ(PumpUntilReply(client)["id"], 15);
}

// a client disconnects during a wait, and the next client uses the same id. The late reply must not reach it
TEST_F(ToolServerTest, AReplyForAClosedClientIsNotDeliveredToTheNextClient)
{
    client.Send(MakeHold(16));

    while (hold.Held.empty())
    {
        server.Pump(registry, k_LargeBudget);
    }

    client.Close();
    std::this_thread::sleep_for(k_QuietPeriod);

    TestClient next;

    ASSERT_TRUE(next.Connect(server.Port()));

    agent::ReplyToken<agent::NoReply> late = std::move(hold.Held.back());

    hold.Held.pop_back();
    late.Complete(agent::NoReply{});

    next.Send(MakeRequest(16, "tool.get", R"({"path":"test.weather.WindForce"})"));

    Json reply = PumpUntilReply(next);

    EXPECT_EQ(reply["id"], 16);
    EXPECT_EQ(reply["result"], 5.0);
    EXPECT_TRUE(PumpAndExpectQuiet(next));
}

TEST_F(ToolServerTest, PumpHonorsItsBudget)
{
    client.Send(MakeHold(17) + MakeHold(18) + MakeHold(19));
    std::this_thread::sleep_for(k_QuietPeriod);

    server.Pump(registry, 1);
    EXPECT_EQ(hold.Held.size(), 1u);

    server.Pump(registry, 1);
    EXPECT_EQ(hold.Held.size(), 2u);

    server.Pump(registry, k_LargeBudget);
    EXPECT_EQ(hold.Held.size(), 3u);

    hold.Held.clear();
}

// the request queue holds 63 items, so most of these wait in the socket until the frame thread drains them
TEST_F(ToolServerTest, RequestsBeyondTheQueueSizeWaitAndNoneIsLost)
{
    constexpr int32_t k_RequestCount = 500;

    std::string requests;

    for (int32_t index = 0; index < k_RequestCount; ++index)
    {
        requests += MakeSet(index, static_cast<float>(index % 12));
    }

    client.Send(requests);
    std::this_thread::sleep_for(k_QuietPeriod);

    for (int32_t index = 0; index < k_RequestCount; ++index)
    {
        Json reply = PumpUntilReply(client);

        ASSERT_EQ(reply["id"], index);
    }

    EXPECT_EQ(weather.WindForce, static_cast<float>((k_RequestCount - 1) % 12));
}

// one burst larger than the reply queue, in one call on the frame thread, which must not block
TEST_F(ToolServerTest, RepliesBeyondTheQueueSizeWaitAndKeepTheirOrder)
{
    constexpr int32_t k_ReplyCount = 400;

    std::string requests;

    for (int32_t index = 0; index < k_ReplyCount; ++index)
    {
        requests += MakeHold(index);
    }

    client.Send(requests);

    while (std::ssize(hold.Held) < k_ReplyCount)
    {
        server.Pump(registry, k_LargeBudget);
    }

    for (agent::ReplyToken<agent::NoReply>& token : hold.Held)
    {
        token.Complete(agent::NoReply{});
    }

    hold.Held.clear();

    for (int32_t index = 0; index < k_ReplyCount; ++index)
    {
        Json reply = PumpUntilReply(client);

        ASSERT_EQ(reply["id"], index);
    }
}

// this client asks for far more than the 16 MiB limit on unsent bytes, and reads nothing at first. The server
// must stop reading requests, so that only about 32 replies plus the 63 slot queue run past the limit
TEST_F(ToolServerTest, AClientThatStopsReadingStopsBeingReadAndLosesNothing)
{
    constexpr int32_t k_ReplyCount = 200;
    constexpr int32_t k_MostRunsWhileNotReading = 150;
    constexpr std::chrono::seconds k_StallPeriod{ 1 };

    std::string requests;

    for (int32_t index = 0; index < k_ReplyCount; ++index)
    {
        requests += MakeRequest(index, "tool.call", R"({"name":"test.blob"})");
    }

    client.Send(requests);

    int32_t lastRunCount = -1;
    std::chrono::steady_clock::time_point lastChange = std::chrono::steady_clock::now();

    while (blob.RunCount < k_ReplyCount && std::chrono::steady_clock::now() - lastChange < k_StallPeriod)
    {
        server.Pump(registry, 1);

        if (blob.RunCount != lastRunCount)
        {
            lastRunCount = blob.RunCount;
            lastChange = std::chrono::steady_clock::now();
        }
    }

    EXPECT_LT(blob.RunCount, k_MostRunsWhileNotReading);

    // the replies are large, and only their ids and sizes matter here. A JSON parse of each takes most of a minute
    // in Debug
    for (int32_t index = 0; index < k_ReplyCount; ++index)
    {
        std::optional<std::string> line;
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_ReplyDeadline;

        while (!line.has_value() && std::chrono::steady_clock::now() < deadline)
        {
            server.Pump(registry, k_LargeBudget);
            line = client.TryReadLine(std::chrono::milliseconds{ 1 });
        }

        ASSERT_TRUE(line.has_value()) << "reply " << index;
        ASSERT_TRUE(line->starts_with(std::format(R"({{"jsonrpc":"2.0","id":{},"result":)", index))) << "reply " << index;
        ASSERT_GT(line->size(), k_BlobBytes) << "reply " << index;
    }
}

TEST_F(ToolServerTest, StopDeliversAReplyThatExistsAndThenCloses)
{
    client.Send(MakeHold(20));

    while (hold.Held.empty())
    {
        server.Pump(registry, k_LargeBudget);
    }

    // a dropped token answers with NoReply
    hold.Held.clear();
    server.Stop();

    const std::optional<std::string> line = client.TryReadLine(k_ReplyDeadline);

    ASSERT_TRUE(line.has_value());

    Json reply = Json::parse(*line);

    EXPECT_EQ(reply["id"], 20);
    EXPECT_EQ(reply["error"]["data"]["Code"], "NoReply");
    EXPECT_FALSE(client.TryReadLine(k_ReplyDeadline).has_value());
    EXPECT_TRUE(client.PeerClosed());
}

TEST_F(ToolServerTest, AReplyAfterStopIsDropped)
{
    client.Send(MakeHold(21));

    while (hold.Held.empty())
    {
        server.Pump(registry, k_LargeBudget);
    }

    server.Stop();
    hold.Held.clear();

    EXPECT_FALSE(client.TryReadLine(k_ReplyDeadline).has_value());
    EXPECT_TRUE(client.PeerClosed());
}

TEST_F(ToolServerTest, StopDiscardsRequestsThatNeverRan)
{
    client.Send(MakeSet(22, 9.0f));
    std::this_thread::sleep_for(k_QuietPeriod);
    server.Stop();
    server.Pump(registry, k_LargeBudget);

    EXPECT_EQ(weather.WindForce, 5.0f);
}

TEST(ToolServerStart, ChoosesAPortAndReportsIt)
{
    agent::ToolServer server;

    EXPECT_EQ(server.Port(), -1);
    ASSERT_EQ(server.Start(0), agent::ToolServerResult::Success);
    EXPECT_GT(server.Port(), 0);
    EXPECT_EQ(server.Start(0), agent::ToolServerResult::AlreadyStarted);

    server.Stop();

    EXPECT_EQ(server.Port(), -1);
}

TEST(ToolServerStart, RefusesAPortOutOfRange)
{
    agent::ToolServer server;

    EXPECT_EQ(server.Start(-1), agent::ToolServerResult::InvalidPort);
    EXPECT_EQ(server.Start(65536), agent::ToolServerResult::InvalidPort);
}

TEST(ToolServerStart, RefusesAPortInUseAndCanStartAgainAfterStop)
{
    agent::ToolServer first;
    agent::ToolServer second;

    ASSERT_EQ(first.Start(0), agent::ToolServerResult::Success);
    EXPECT_EQ(second.Start(first.Port()), agent::ToolServerResult::PortInUse);

    first.Stop();

    EXPECT_EQ(first.Start(0), agent::ToolServerResult::Success);
}

namespace
{
    void AimCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        token.Complete(AimReply{ .Sum = params.Eye[0] + params.Eye[1] + params.Lens.Fov });
    }

    void HoldCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        static_cast<void>(params);
        Held.push_back(std::move(token));
    }

    void BlobCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        static_cast<void>(params);
        ++RunCount;
        token.Complete(BlobReply{ .Text = std::string(k_BlobBytes, 'b') });
    }

    bool TestClient::Connect(int32_t port)
    {
        std::expected<foundation::net::TcpSocket, foundation::net::SocketResult> connected =
            foundation::net::ConnectLoopback(port);

        if (!connected.has_value())
        {
            return false;
        }

        socket = std::move(*connected);

        return true;
    }

    void TestClient::Close()
    {
        socket.Close();
    }

    void TestClient::Send(std::string_view bytes)
    {
        size_t sentTotal = 0u;

        while (sentTotal < bytes.size())
        {
            const std::string_view unsent = bytes.substr(sentTotal);
            const foundation::net::SocketIo sent = socket.Send(std::as_bytes(std::span{ unsent }));

            ASSERT_NE(sent.Kind, foundation::net::SocketIoKind::Closed);

            if (sent.Kind == foundation::net::SocketIoKind::WouldBlock)
            {
                std::array<foundation::net::PollEntry, 1u> entries
                {
                    foundation::net::PollEntry{ .Socket = &socket, .WantWrite = true },
                };

                foundation::net::Poll(entries, 10);
                continue;
            }

            sentTotal += static_cast<size_t>(sent.Bytes);
        }
    }

    void TestClient::SendOneByteAtATime(std::string_view bytes)
    {
        for (const char& byte : bytes)
        {
            Send(std::string_view{ &byte, 1u });
        }
    }

    std::optional<std::string> TestClient::TryReadLine(std::chrono::milliseconds wait)
    {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + wait;

        while (true)
        {
            const size_t newline = received.find('\n');

            if (newline != std::string::npos)
            {
                std::string line = received.substr(0u, newline);

                received.erase(0u, newline + 1u);

                return line;
            }

            if (peerClosed)
            {
                return std::nullopt;
            }

            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

            if (now >= deadline)
            {
                return std::nullopt;
            }

            const std::chrono::milliseconds left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

            std::array<foundation::net::PollEntry, 1u> entries
            {
                foundation::net::PollEntry{ .Socket = &socket, .WantRead = true },
            };

            foundation::net::Poll(entries, static_cast<int32_t>(left.count()) + 1);

            if (!entries[0].Readable && !entries[0].Failed)
            {
                continue;
            }

            std::array<std::byte, 65536u> chunk{};

            const foundation::net::SocketIo read = socket.Receive(chunk);

            if (read.Kind == foundation::net::SocketIoKind::Closed)
            {
                peerClosed = true;
            }
            else if (read.Kind == foundation::net::SocketIoKind::Transferred)
            {
                received.append(reinterpret_cast<const char*>(chunk.data()), static_cast<size_t>(read.Bytes));
            }
        }
    }

    bool TestClient::PeerClosed() const noexcept
    {
        return peerClosed;
    }

    void ToolServerTest::SetUp()
    {
        std::expected<foundation::net::NetworkSession, foundation::net::SocketResult> started =
            foundation::net::NetworkSession::Start();

        ASSERT_TRUE(started.has_value());
        network.emplace(std::move(*started));

        ASSERT_EQ(registry.AddObject("test.weather", weather), agent::ToolResultCode::Success);
        ASSERT_EQ(registry.AddCommand(aim), agent::ToolResultCode::Success);
        ASSERT_EQ(registry.AddCommand(hold), agent::ToolResultCode::Success);
        ASSERT_EQ(registry.AddCommand(blob), agent::ToolResultCode::Success);
        registry.Freeze();

        ASSERT_EQ(server.Start(0), agent::ToolServerResult::Success);
        ASSERT_TRUE(client.Connect(server.Port()));
    }

    Json ToolServerTest::PumpUntilReply(TestClient& reader)
    {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + k_ReplyDeadline;

        while (std::chrono::steady_clock::now() < deadline)
        {
            server.Pump(registry, k_LargeBudget);

            const std::optional<std::string> line = reader.TryReadLine(std::chrono::milliseconds{ 1 });

            if (line.has_value())
            {
                return Json::parse(*line);
            }
        }

        ADD_FAILURE() << "no reply arrived before the deadline";

        return Json::object();
    }

    bool ToolServerTest::PumpAndExpectQuiet(TestClient& reader)
    {
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + k_QuietPeriod;

        while (std::chrono::steady_clock::now() < end)
        {
            server.Pump(registry, k_LargeBudget);

            if (reader.TryReadLine(std::chrono::milliseconds{ 1 }).has_value())
            {
                return false;
            }
        }

        return true;
    }

    std::string MakeRequest(int64_t id, std::string_view method, std::string_view params_json)
    {
        return std::format(R"({{"jsonrpc":"2.0","id":{},"method":"{}","params":{}}})"
                           "\n",
                           id,
                           method,
                           params_json);
    }

    std::string MakeSet(int64_t id, float wind_force)
    {
        return MakeRequest(id, "tool.set", std::format(R"({{"path":"test.weather.WindForce","value":{}}})", wind_force));
    }

    std::string MakeHold(int64_t id)
    {
        return MakeRequest(id, "tool.call", R"({"name":"test.hold"})");
    }

    std::string MakePing(int64_t id)
    {
        return MakeRequest(id, "server.ping", "{}");
    }
}
