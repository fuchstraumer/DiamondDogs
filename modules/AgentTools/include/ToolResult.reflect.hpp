#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_REFLECT_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_REFLECT_HPP
#include "ToolResult.hpp"
#include "reflect/Describe.hpp"
#include <array>
#include <string_view>
#include <tuple>

template<>
struct foundation::reflect::Describe<agent::ToolResultCode>
{
    using ToolResultCode = agent::ToolResultCode;

    static constexpr std::string_view Name{ "agent.ToolResultCode" };
    static constexpr std::array Cases
    {
        EnumCase<ToolResultCode>{ "Invalid", ToolResultCode::Invalid, {} },
        EnumCase<ToolResultCode>{ "Success", ToolResultCode::Success, {} },
        EnumCase<ToolResultCode>{ "RegistryFrozen", ToolResultCode::RegistryFrozen, {} },
        EnumCase<ToolResultCode>{ "DuplicateObject", ToolResultCode::DuplicateObject, {} },
        EnumCase<ToolResultCode>{ "OverlappingObject", ToolResultCode::OverlappingObject, {} },
        EnumCase<ToolResultCode>{ "DuplicateCommand", ToolResultCode::DuplicateCommand, {} },
        EnumCase<ToolResultCode>{ "UnknownObject", ToolResultCode::UnknownObject, {} },
        EnumCase<ToolResultCode>{ "UnknownCommand", ToolResultCode::UnknownCommand, {} },
        EnumCase<ToolResultCode>{ "ValueRefused", ToolResultCode::ValueRefused, {} },
        EnumCase<ToolResultCode>{ "MissingArgument", ToolResultCode::MissingArgument, {} },
        EnumCase<ToolResultCode>{ "DuplicateArgument", ToolResultCode::DuplicateArgument, {} },
        EnumCase<ToolResultCode>{ "MalformedLine", ToolResultCode::MalformedLine, {} },
        EnumCase<ToolResultCode>{ "CommandFailed", ToolResultCode::CommandFailed, {} },
        EnumCase<ToolResultCode>{ "NoReply", ToolResultCode::NoReply, {} },
    };
};

template<>
struct foundation::reflect::Describe<agent::ToolResult>
{
    using ToolResult = agent::ToolResult;

    static constexpr std::string_view Name{ "agent.ToolResult" };
    static constexpr std::tuple Fields
    {
        Field<ToolResult, agent::ToolResultCode>{ "Code", &ToolResult::Code },
        Field<ToolResult, std::string>{ "Message", &ToolResult::Message },
        Field<ToolResult, std::string>{ "Path", &ToolResult::Path },
        Field<ToolResult, int32_t>{ "Offset", &ToolResult::Offset },
        Field<ToolResult, std::vector<std::string>>{ "Suggestions", &ToolResult::Suggestions },
    };
};

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_RESULT_REFLECT_HPP
