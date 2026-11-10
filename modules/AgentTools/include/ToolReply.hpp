#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_REPLY_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_REPLY_HPP
#include "ToolResult.hpp"
#include "reflect/TypeInfo.hpp"
#include <cstdint>
#include <string>
#include <utility>

namespace agent
{
    /** Receives the answer to each request. The transport implements this. A sink must outlive every reply
     *  that points to it. */
    class ReplySink
    {
    public:
        ReplySink() = default;
        ReplySink(const ReplySink&) = delete;
        ReplySink& operator=(const ReplySink&) = delete;
        virtual ~ReplySink() = default;

        virtual void Finish(int64_t request_id, std::string result_json) = 0;
        virtual void Fail(int64_t request_id, ToolResult failure) = 0;
    };

    /** The right and the duty to answer one request, exactly once. A reply can wait for later frames. A reply
     *  that is destroyed without an answer fails with NoReply, so that a lost command never hangs its caller.
     *  */
    class PendingReply
    {
    public:
        PendingReply(ReplySink& _sink, int64_t request_id) noexcept;
        PendingReply(const PendingReply&) = delete;
        PendingReply& operator=(const PendingReply&) = delete;
        PendingReply(PendingReply&& _other) noexcept;
        PendingReply& operator=(PendingReply&& _other) noexcept;
        ~PendingReply();

        /** @brief Answers with a value, encoded as JSON now, so that the value can go out of scope. */
        void Complete(const foundation::reflect::TypeInfo& type, const void* value);
        void CompleteWithJson(std::string result_json);
        void Fail(ToolResult failure);

    private:
        ReplySink* sink{ nullptr };
        int64_t requestId{ -1 };
    };

    /** The reply of one command, with the reply type that the command declares. */
    template<typename Reply>
    class ReplyToken
    {
    public:
        explicit ReplyToken(PendingReply _pending) noexcept :
            pending{ std::move(_pending) }
        {}

        void Complete(const Reply& reply)
        {
            pending.Complete(foundation::reflect::GetTypeInfo<Reply>(), &reply);
        }

        void Fail(std::string message)
        {
            pending.Fail(ToolResult{ .Code = ToolResultCode::CommandFailed, .Message = std::move(message) });
        }

    private:
        PendingReply pending;
    };
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_REPLY_HPP
