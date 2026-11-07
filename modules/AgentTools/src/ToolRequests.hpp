#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_REQUESTS_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_REQUESTS_HPP
#include "ToolReply.hpp"
#include "reflect/ValueAccess.hpp"
#include <string_view>

namespace agent
{
    class ToolRegistry;
}

namespace agent::detail
{
    /** @brief Answers with the schema at the path, or with every schema when the path is empty. */
    void AnswerDescribe(const ToolRegistry& registry, std::string_view path, PendingReply reply);
    void AnswerGet(const ToolRegistry& registry, std::string_view path, PendingReply reply);
    /** @brief Writes the value, and answers with the value that the field now contains. */
    void AnswerSet(ToolRegistry& registry,
                   std::string_view path,
                   const foundation::reflect::ScalarValue& value,
                   PendingReply reply);
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_REQUESTS_HPP
