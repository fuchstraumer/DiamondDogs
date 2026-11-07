#include "ToolServer.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolRequests.hpp"
#include "ToolResult.hpp"
#include "ToolRpc.hpp"
#include "containers/mwsrQueue.hpp"
#include "net/Socket.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// outside the anonymous namespace: mwsrQueue has a thread_local member, which needs a type with linkage
namespace agent::detail
{
    struct QueuedRequest
    {
        int64_t RequestId{ -1 };
        rpc::RpcRequest Body;
    };

    struct QueuedReply
    {
        int64_t RequestId{ -1 };
        bool Succeeded{ false };
        // the result of a success, or the encoded ToolResult of a failure
        std::string Json;
        std::string FailureMessage;
    };
}

namespace
{
    using agent::detail::QueuedReply;
    using agent::detail::QueuedRequest;

    constexpr int32_t k_PollMilliseconds = 5;
    constexpr int64_t k_MaxLineBytes = 1024 * 1024;
    constexpr size_t k_ReceiveChunkBytes = 64u * 1024u;
    // a client that stops reading stops being read, which bounds this buffer and every queue behind it
    constexpr size_t k_MaxUnsentBytes = 16u * 1024u * 1024u;
    // Winsock accepts a non-blocking send of any size while its send buffer has room, which moves the whole backlog
    // into kernel memory and hides it from the limit above
    constexpr size_t k_MaxBytesPerSend = 64u * 1024u;
    constexpr std::string_view k_PongJson{ "{\"Pong\":true}" };

    // the route of one request that the frame thread has not answered
    struct ReplyRoute
    {
        int64_t ClientId{ -1 };
        int64_t ConnectionGeneration{ -1 };
    };

    // the state that only the socket thread touches
    struct SocketSide
    {
        foundation::net::TcpSocket Listener;
        foundation::net::TcpSocket Connection;
        agent::rpc::LineFramer Framer{ k_MaxLineBytes };
        std::vector<std::byte> ReceiveChunk;
        std::string Unsent;
        size_t UnsentStart{ 0u };
        // a request that the full queue refused. While this exists, the socket is not read, so TCP slows the client
        std::unique_ptr<QueuedRequest> Blocked;
        std::unordered_map<int64_t, ReplyRoute> Routes;
        // a client can reuse an id, and a reply can outlive its client. Routes use ids that this server assigns,
        // and a route from an older generation is dropped
        int64_t ConnectionGeneration{ 0 };
        int64_t NextRequestId{ 1 };
    };

    [[nodiscard]] agent::ToolServerResult MapSocketResult(foundation::net::SocketResult result) noexcept;
    void RunRequest(agent::ToolRegistry& registry, const agent::rpc::RpcRequest& request, agent::PendingReply reply);
    void RunCall(agent::ToolRegistry& registry, const agent::rpc::RpcRequest& request, agent::PendingReply reply);
    [[nodiscard]] bool WantsToRead(const SocketSide& side) noexcept;
    void CloseConnection(SocketSide& side);
    void AcceptOrRefuseClient(SocketSide& side);
    void ReceiveBytes(SocketSide& side);
    void SendUnsent(SocketSide& side);
    void QueueLine(SocketSide& side, std::string_view line);
}

namespace agent
{
    // try_push moves its argument into a by-value parameter, and destroys the item when the queue is full. So
    // both queues store raw pointers: the sender keeps the unique_ptr until a push succeeds
    struct ToolServer::State final : ReplySink
    {
        void Finish(int64_t request_id, std::string result_json) final;
        void Fail(int64_t request_id, ToolResult failure) final;

        void SendReply(std::unique_ptr<QueuedReply> reply);
        void FlushReplyOverflow();
        void DiscardQueuedMessages();
        void SocketThreadMain(foundation::net::TcpSocket listener);
        void DispatchLines(SocketSide& side);
        void DispatchLine(SocketSide& side, const rpc::FramedLine& line);
        void DispatchRequest(SocketSide& side, rpc::RpcRequest request);
        [[nodiscard]] bool RetryBlockedRequest(SocketSide& side);
        void DrainReplies(SocketSide& side);

        mwsrQueue<QueuedRequest*> Requests;
        mwsrQueue<QueuedReply*> Replies;
        std::atomic<bool> StopRequested{ false };

        // the frame thread alone touches the members below
        std::vector<std::unique_ptr<QueuedReply>> ReplyOverflow;
        std::optional<foundation::net::NetworkSession> Network;
        std::thread SocketThread;
        int32_t OpenPort{ -1 };
        bool Running{ false };
    };

    std::string_view ToString(ToolServerResult result) noexcept
    {
        switch (result)
        {
        case ToolServerResult::Invalid: return "Invalid";
        case ToolServerResult::Success: return "Success";
        case ToolServerResult::AlreadyStarted: return "AlreadyStarted";
        case ToolServerResult::InvalidPort: return "InvalidPort";
        case ToolServerResult::NetworkStartFailed: return "NetworkStartFailed";
        case ToolServerResult::SocketFailed: return "SocketFailed";
        case ToolServerResult::PortInUse: return "PortInUse";
        }

        return "Invalid";
    }

    ToolServer::ToolServer() :
        state{ std::make_unique<State>() }
    {}

    ToolServer::~ToolServer()
    {
        Stop();
    }

    ToolServerResult ToolServer::Start(int32_t port)
    {
        if (state->Running)
        {
            return ToolServerResult::AlreadyStarted;
        }

        std::expected<foundation::net::NetworkSession, foundation::net::SocketResult> network =
            foundation::net::NetworkSession::Start();

        if (!network.has_value())
        {
            return MapSocketResult(network.error());
        }

        std::expected<foundation::net::TcpSocket, foundation::net::SocketResult> listener =
            foundation::net::OpenLoopbackListener(port);

        if (!listener.has_value())
        {
            return MapSocketResult(listener.error());
        }

        state->Network.emplace(std::move(*network));
        state->OpenPort = foundation::net::LocalPort(*listener);
        state->StopRequested.store(false, std::memory_order_relaxed);
        state->Running = true;
        state->SocketThread = std::thread{ &State::SocketThreadMain, state.get(), std::move(*listener) };

        return ToolServerResult::Success;
    }

    int32_t ToolServer::Port() const noexcept
    {
        return state->OpenPort;
    }

    void ToolServer::Pump(ToolRegistry& registry, int32_t max_requests)
    {
        state->FlushReplyOverflow();

        for (int32_t count = 0; count < max_requests; ++count)
        {
            const std::optional<QueuedRequest*> popped = state->Requests.try_pop();

            if (!popped.has_value())
            {
                return;
            }

            const std::unique_ptr<QueuedRequest> request{ *popped };

            RunRequest(registry, request->Body, PendingReply{ *state, request->RequestId });
        }
    }

    void ToolServer::Stop()
    {
        if (!state->Running)
        {
            return;
        }

        state->FlushReplyOverflow();
        state->StopRequested.store(true, std::memory_order_release);
        state->SocketThread.join();
        state->DiscardQueuedMessages();
        state->OpenPort = -1;
        state->Running = false;
        state->Network.reset();
    }

    void ToolServer::State::Finish(int64_t request_id, std::string result_json)
    {
        SendReply(std::make_unique<QueuedReply>(QueuedReply
        {
            .RequestId = request_id,
            .Succeeded = true,
            .Json = std::move(result_json),
        }));
    }

    void ToolServer::State::Fail(int64_t request_id, ToolResult failure)
    {
        SendReply(std::make_unique<QueuedReply>(QueuedReply
        {
            .RequestId = request_id,
            .Succeeded = false,
            .Json = ResultToJson(failure),
            .FailureMessage = std::move(failure.Message),
        }));
    }

    void ToolServer::State::SendReply(std::unique_ptr<QueuedReply> reply)
    {
        if (!Running)
        {
            return;
        }

        // the frame thread never blocks. Anything that does not fit waits here for the next Pump, in order
        ReplyOverflow.push_back(std::move(reply));
        FlushReplyOverflow();
    }

    void ToolServer::State::FlushReplyOverflow()
    {
        size_t sent = 0u;

        while (sent < ReplyOverflow.size() && Replies.try_push(ReplyOverflow[sent].get()))
        {
            static_cast<void>(ReplyOverflow[sent].release());
            ++sent;
        }

        ReplyOverflow.erase(ReplyOverflow.begin(), ReplyOverflow.begin() + static_cast<ptrdiff_t>(sent));
    }

    void ToolServer::State::DiscardQueuedMessages()
    {
        ReplyOverflow.clear();

        while (const std::optional<QueuedRequest*> request = Requests.try_pop())
        {
            delete *request;
        }

        while (const std::optional<QueuedReply*> reply = Replies.try_pop())
        {
            delete *reply;
        }
    }

    void ToolServer::State::SocketThreadMain(foundation::net::TcpSocket listener)
    {
        SocketSide side{};

        side.Listener = std::move(listener);
        side.ReceiveChunk.resize(k_ReceiveChunkBytes);

        bool stopping = false;

        // the stop flag is read at the top, so the last pass sends every reply that existed before the stop
        while (!stopping)
        {
            stopping = StopRequested.load(std::memory_order_acquire);

            std::array<foundation::net::PollEntry, 2u> entries{};

            entries[0] = foundation::net::PollEntry{ .Socket = &side.Listener, .WantRead = true };

            const bool connected = side.Connection.IsOpen();

            if (connected)
            {
                entries[1] = foundation::net::PollEntry
                {
                    .Socket = &side.Connection,
                    .WantRead = WantsToRead(side),
                    .WantWrite = side.UnsentStart < side.Unsent.size(),
                };
            }

            foundation::net::Poll(std::span{ entries.data(), connected ? 2u : 1u }, k_PollMilliseconds);

            if (connected && entries[1].Readable)
            {
                ReceiveBytes(side);
            }
            else if (connected && entries[1].Failed)
            {
                // nothing else reports a hangup while the socket is not being read, and poll would spin on it
                CloseConnection(side);
            }

            // after the receive, so that a client that closes and reconnects finds its old connection gone
            if (entries[0].Readable)
            {
                AcceptOrRefuseClient(side);
            }

            DispatchLines(side);
            DrainReplies(side);
            SendUnsent(side);
        }
    }

    void ToolServer::State::DispatchLines(SocketSide& side)
    {
        if (!RetryBlockedRequest(side))
        {
            return;
        }

        while (side.Connection.IsOpen() && side.Unsent.size() - side.UnsentStart <= k_MaxUnsentBytes)
        {
            const std::optional<rpc::FramedLine> line = side.Framer.Next();

            if (!line.has_value())
            {
                return;
            }

            DispatchLine(side, *line);

            if (side.Blocked != nullptr)
            {
                return;
            }
        }
    }

    void ToolServer::State::DispatchLine(SocketSide& side, const rpc::FramedLine& line)
    {
        if (line.Kind == rpc::LineKind::Oversize)
        {
            QueueLine(side, rpc::FormatRefusal(rpc::RpcResult
            {
                .Code = rpc::RpcCode::InvalidRequest,
                .Message = "the line is above the limit of 1 MiB",
            }));
            return;
        }

        std::expected<rpc::RpcRequest, rpc::RpcResult> request = rpc::ParseRequest(line.Text);

        if (!request.has_value())
        {
            QueueLine(side, rpc::FormatRefusal(request.error()));
            return;
        }

        if (request->Method == rpc::RpcMethod::Ping)
        {
            QueueLine(side, rpc::FormatResult(request->ClientId, k_PongJson));
            return;
        }

        DispatchRequest(side, std::move(*request));
    }

    void ToolServer::State::DispatchRequest(SocketSide& side, rpc::RpcRequest request)
    {
        const int64_t requestId = side.NextRequestId++;

        side.Routes.emplace(requestId, ReplyRoute
        {
            .ClientId = request.ClientId,
            .ConnectionGeneration = side.ConnectionGeneration,
        });

        side.Blocked = std::make_unique<QueuedRequest>(QueuedRequest
        {
            .RequestId = requestId,
            .Body = std::move(request),
        });

        static_cast<void>(RetryBlockedRequest(side));
    }

    bool ToolServer::State::RetryBlockedRequest(SocketSide& side)
    {
        if (side.Blocked == nullptr)
        {
            return true;
        }

        if (!Requests.try_push(side.Blocked.get()))
        {
            return false;
        }

        static_cast<void>(side.Blocked.release());

        return true;
    }

    void ToolServer::State::DrainReplies(SocketSide& side)
    {
        while (const std::optional<QueuedReply*> popped = Replies.try_pop())
        {
            const std::unique_ptr<QueuedReply> reply{ *popped };
            const auto routeEntry = side.Routes.find(reply->RequestId);
            const ReplyRoute route = routeEntry->second;

            side.Routes.erase(routeEntry);

            if (route.ConnectionGeneration != side.ConnectionGeneration || !side.Connection.IsOpen())
            {
                continue;
            }

            if (reply->Succeeded)
            {
                QueueLine(side, rpc::FormatResult(route.ClientId, reply->Json));
            }
            else
            {
                QueueLine(side, rpc::FormatToolFailure(route.ClientId, reply->FailureMessage, reply->Json));
            }
        }
    }
}

namespace
{
    agent::ToolServerResult MapSocketResult(foundation::net::SocketResult result) noexcept
    {
        switch (result)
        {
        case foundation::net::SocketResult::Success: return agent::ToolServerResult::Success;
        case foundation::net::SocketResult::NetworkStartFailed: return agent::ToolServerResult::NetworkStartFailed;
        case foundation::net::SocketResult::InvalidPort: return agent::ToolServerResult::InvalidPort;
        case foundation::net::SocketResult::PortInUse: return agent::ToolServerResult::PortInUse;
        case foundation::net::SocketResult::SocketFailed: return agent::ToolServerResult::SocketFailed;
        case foundation::net::SocketResult::ConnectFailed: return agent::ToolServerResult::SocketFailed;
        case foundation::net::SocketResult::Invalid: return agent::ToolServerResult::Invalid;
        }

        return agent::ToolServerResult::Invalid;
    }

    void RunRequest(agent::ToolRegistry& registry, const agent::rpc::RpcRequest& request, agent::PendingReply reply)
    {
        switch (request.Method)
        {
        case agent::rpc::RpcMethod::Describe:
            agent::detail::AnswerDescribe(registry, request.Target, std::move(reply));
            break;
        case agent::rpc::RpcMethod::Get:
            agent::detail::AnswerGet(registry, request.Target, std::move(reply));
            break;
        case agent::rpc::RpcMethod::Set:
            agent::detail::AnswerSet(registry, request.Target, agent::rpc::ViewOf(request.Value), std::move(reply));
            break;
        case agent::rpc::RpcMethod::Call:
            RunCall(registry, request, std::move(reply));
            break;
        case agent::rpc::RpcMethod::Ping:
        case agent::rpc::RpcMethod::Invalid:
            break;
        }
    }

    void RunCall(agent::ToolRegistry& registry, const agent::rpc::RpcRequest& request, agent::PendingReply reply)
    {
        std::vector<agent::NamedValue> arguments;

        arguments.reserve(request.Arguments.size());

        for (const agent::rpc::OwnedArgument& argument : request.Arguments)
        {
            arguments.push_back(agent::NamedValue
            {
                .Path = argument.Path,
                .Value = agent::rpc::ViewOf(argument.Value),
            });
        }

        registry.Call(request.Target, arguments, std::move(reply));
    }

    bool WantsToRead(const SocketSide& side) noexcept
    {
        return side.Blocked == nullptr && side.Unsent.size() - side.UnsentStart <= k_MaxUnsentBytes;
    }

    void CloseConnection(SocketSide& side)
    {
        side.Connection.Close();
        side.Framer = agent::rpc::LineFramer{ k_MaxLineBytes };
        side.Unsent.clear();
        side.UnsentStart = 0u;
        ++side.ConnectionGeneration;

        if (side.Blocked != nullptr)
        {
            side.Routes.erase(side.Blocked->RequestId);
            side.Blocked.reset();
        }
    }

    void AcceptOrRefuseClient(SocketSide& side)
    {
        std::optional<foundation::net::TcpSocket> client = foundation::net::AcceptClient(side.Listener);

        if (!client.has_value())
        {
            return;
        }

        if (side.Connection.IsOpen())
        {
            const std::string refusal = agent::rpc::FormatRefusal(agent::rpc::RpcResult
            {
                .Code = agent::rpc::RpcCode::ServerBusy,
                .Message = "another client is connected, and the server accepts one client at a time",
            });

            static_cast<void>(client->Send(std::as_bytes(std::span{ refusal })));
            return;
        }

        side.Connection = std::move(*client);
    }

    void ReceiveBytes(SocketSide& side)
    {
        const foundation::net::SocketIo received = side.Connection.Receive(side.ReceiveChunk);

        if (received.Kind == foundation::net::SocketIoKind::Closed)
        {
            CloseConnection(side);
        }
        else if (received.Kind == foundation::net::SocketIoKind::Transferred)
        {
            const std::span<const std::byte> bytes{ side.ReceiveChunk.data(), static_cast<size_t>(received.Bytes) };

            side.Framer.Append(bytes);
        }
    }

    void SendUnsent(SocketSide& side)
    {
        while (side.Connection.IsOpen() && side.UnsentStart < side.Unsent.size())
        {
            const size_t unsentBytes = side.Unsent.size() - side.UnsentStart;
            const std::span<const char> text{ side.Unsent.data() + side.UnsentStart,
                                              std::min(unsentBytes, k_MaxBytesPerSend) };
            const foundation::net::SocketIo sent = side.Connection.Send(std::as_bytes(text));

            if (sent.Kind == foundation::net::SocketIoKind::Closed)
            {
                CloseConnection(side);
                return;
            }

            if (sent.Kind == foundation::net::SocketIoKind::WouldBlock)
            {
                return;
            }

            side.UnsentStart += static_cast<size_t>(sent.Bytes);
        }

        side.Unsent.clear();
        side.UnsentStart = 0u;
    }

    void QueueLine(SocketSide& side, std::string_view line)
    {
        side.Unsent.append(line);
    }
}
