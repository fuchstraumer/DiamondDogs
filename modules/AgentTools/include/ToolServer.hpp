#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_SERVER_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_SERVER_HPP
#include <cstdint>
#include <memory>
#include <string_view>

namespace agent
{
    class ToolRegistry;

    enum class ToolServerResult : uint8_t
    {
        Invalid = 0,
        Success = 1,
        AlreadyStarted,
        InvalidPort,
        NetworkStartFailed,
        SocketFailed,
        PortInUse,
    };

    [[nodiscard]] std::string_view ToString(ToolServerResult result) noexcept;

    /** JSON-RPC 2.0 over TCP on 127.0.0.1, one JSON message per line. A private thread owns the socket and
     *  parses the requests. The registry runs only inside Pump, on the thread that owns the objects. Call
     *  Start, Pump and Stop on that thread. Any local process can connect while the server runs, so an
     *  application must start it only on request. */
    class ToolServer
    {
    public:
        ToolServer();
        ToolServer(const ToolServer&) = delete;
        ToolServer& operator=(const ToolServer&) = delete;
        ~ToolServer();

        /** @brief Opens the port and starts the thread. Port 0 lets the system choose: Port returns the choice.
         *  */
        [[nodiscard]] ToolServerResult Start(int32_t port);
        /** @brief Returns the open port, or -1 when the server is not running. */
        [[nodiscard]] int32_t Port() const noexcept;
        /** @brief Runs up to max_requests requests, which keeps a flood of requests from stretching a frame. */
        void Pump(ToolRegistry& registry, int32_t max_requests);
        /** @brief Sends the replies that exist now, and closes the port. A reply that answers after this call
         *  is dropped. The object must still outlive every reply. */
        void Stop();

    private:
        struct State;

        std::unique_ptr<State> state;
    };
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_SERVER_HPP
