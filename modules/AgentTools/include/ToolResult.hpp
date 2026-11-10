#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_HPP
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agent
{
    enum class ToolResultCode : uint8_t
    {
        Invalid = 0,
        Success = 1,
        RegistryFrozen,
        DuplicateObject,
        OverlappingObject,
        DuplicateCommand,
        UnknownObject,
        UnknownCommand,
        ValueRefused,
        MissingArgument,
        DuplicateArgument,
        MalformedLine,
        CommandFailed,
        // a command dropped its reply token without an answer
        NoReply,
    };

    /** The status of a request. A function that yields no value returns this record, with Code set to Success
     *  when the request succeeded. Suggestions contains the nearest valid names. The registry never applies a
     *  suggestion, because that would hide the mistake from the caller. */
    struct ToolResult
    {
        ToolResultCode Code{ ToolResultCode::Invalid };
        std::string Message;
        // the path, command or argument that failed
        std::string Path;
        // the position in Path where the walk stopped. -1 when no position applies
        int32_t Offset{ -1 };
        std::vector<std::string> Suggestions;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Code == ToolResultCode::Success;
        }
    };

    [[nodiscard]] std::string_view ToString(ToolResultCode error) noexcept;
    /** @brief Returns the failure as one line of JSON. */
    [[nodiscard]] std::string ResultToJson(const ToolResult& failure);
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_HPP
