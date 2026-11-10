#include "ToolResult.hpp"
#include "ToolResult.reflect.hpp"
#include "content/EncodeErased.hpp"
#include "content/JsonSink.hpp"
#include "reflect/TypeInfo.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace agent
{
    std::string_view ToString(ToolResultCode error) noexcept
    {
        switch (error)
        {
        case ToolResultCode::Invalid: return "Invalid";
        case ToolResultCode::Success: return "Success";
        case ToolResultCode::RegistryFrozen: return "RegistryFrozen";
        case ToolResultCode::DuplicateObject: return "DuplicateObject";
        case ToolResultCode::OverlappingObject: return "OverlappingObject";
        case ToolResultCode::DuplicateCommand: return "DuplicateCommand";
        case ToolResultCode::UnknownObject: return "UnknownObject";
        case ToolResultCode::UnknownCommand: return "UnknownCommand";
        case ToolResultCode::ValueRefused: return "ValueRefused";
        case ToolResultCode::MissingArgument: return "MissingArgument";
        case ToolResultCode::DuplicateArgument: return "DuplicateArgument";
        case ToolResultCode::MalformedLine: return "MalformedLine";
        case ToolResultCode::CommandFailed: return "CommandFailed";
        case ToolResultCode::NoReply: return "NoReply";
        }

        return "Invalid";
    }

    std::string ResultToJson(const ToolResult& failure)
    {
        foundation::content::JsonSink sink{ foundation::content::JsonStyle::Compact };

        foundation::content::EncodeErased(foundation::reflect::GetTypeInfo<ToolResult>(), &failure, sink);

        foundation::content::JsonResult<std::string> document = sink.Finish();

        return std::move(*document);
    }
}
