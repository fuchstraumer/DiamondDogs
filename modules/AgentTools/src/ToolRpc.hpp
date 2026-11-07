#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_RPC_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_RPC_HPP
#include "reflect/ValueAccess.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace agent::rpc
{
    enum class RpcCode : uint8_t
    {
        Invalid = 0,
        Success = 1,
        ParseError,
        InvalidRequest,
        MethodNotFound,
        InvalidParams,
        ServerBusy,
    };

    /** The status of one protocol step. ClientId is empty when the line has no usable id. */
    struct RpcResult
    {
        RpcCode Code{ RpcCode::Invalid };
        std::string Message;
        std::optional<int64_t> ClientId;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Code == RpcCode::Success;
        }
    };

    enum class RpcMethod : uint8_t
    {
        Invalid = 0,
        Describe,
        Get,
        Set,
        Call,
        Ping,
    };

    // ScalarValue stores a string_view. Moving a short std::string moves its bytes, so a view cannot cross the
    // queue. Requests store this form, and the frame thread makes the views just before the call
    using OwnedScalar = std::variant<bool, int64_t, uint64_t, double, std::string>;

    struct OwnedArgument
    {
        std::string Path;
        OwnedScalar Value;
    };

    struct RpcRequest
    {
        RpcMethod Method{ RpcMethod::Invalid };
        int64_t ClientId{ -1 };
        // the path of Describe, Get and Set, or the command name of Call. Empty for Describe of everything
        std::string Target;
        OwnedScalar Value;
        std::vector<OwnedArgument> Arguments;
    };

    enum class LineKind : uint8_t
    {
        Invalid = 0,
        Complete,
        Oversize,
    };

    struct FramedLine
    {
        LineKind Kind{ LineKind::Invalid };
        std::string Text;
    };

    /** Splits a byte stream into newline-terminated lines. A line above the limit returns Oversize once, and
     *  its bytes are discarded up to the next newline, so that the next line still parses. */
    class LineFramer
    {
    public:
        explicit LineFramer(int64_t max_line_bytes) noexcept;

        void Append(std::span<const std::byte> bytes);
        [[nodiscard]] std::optional<FramedLine> Next();

    private:
        void discardConsumedBytes();

        std::string buffer;
        size_t consumed{ 0u };
        size_t scanned{ 0u };
        int64_t maxLineBytes;
        bool discarding{ false };
    };

    /** @brief Returns the request on one line, or the reason to refuse it. */
    [[nodiscard]] std::expected<RpcRequest, RpcResult> ParseRequest(std::string_view line);
    /** @brief Returns the value as a ScalarValue. A string result points into value. */
    [[nodiscard]] foundation::reflect::ScalarValue ViewOf(const OwnedScalar& value) noexcept;

    [[nodiscard]] std::string FormatResult(int64_t client_id, std::string_view result_json);
    /** @brief failure_json is the encoded ToolResult. It becomes the data member of the error. */
    [[nodiscard]] std::string FormatToolFailure(int64_t client_id,
                                                std::string_view message,
                                                std::string_view failure_json);
    [[nodiscard]] std::string FormatRefusal(const RpcResult& refusal);
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_RPC_HPP
