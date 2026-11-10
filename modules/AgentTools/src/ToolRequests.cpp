#include "ToolRequests.hpp"
#include "ToolRegistry.hpp"
#include "ToolResult.hpp"
#include "content/JsonSink.hpp"
#include "content/JsonWriter.hpp"
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace agent::detail
{
    void AnswerDescribe(const ToolRegistry& registry, std::string_view path, PendingReply reply)
    {
        if (path.empty())
        {
            reply.CompleteWithJson(registry.DescribeAll());
            return;
        }

        std::expected<std::string, ToolResult> schema = registry.Describe(path);

        if (!schema.has_value())
        {
            reply.Fail(std::move(schema.error()));
            return;
        }

        reply.CompleteWithJson(std::move(*schema));
    }

    void AnswerGet(const ToolRegistry& registry, std::string_view path, PendingReply reply)
    {
        foundation::content::JsonSink json{ foundation::content::JsonStyle::Compact };
        ToolResult value = registry.GetValue(path, json);

        if (!value.Succeeded())
        {
            reply.Fail(std::move(value));
            return;
        }

        foundation::content::JsonResult<std::string> document = json.Finish();

        reply.CompleteWithJson(std::move(*document));
    }

    void AnswerSet(ToolRegistry& registry,
                   std::string_view path,
                   const foundation::reflect::ScalarValue& value,
                   PendingReply reply)
    {
        ToolResult written = registry.SetValue(path, value);

        if (!written.Succeeded())
        {
            reply.Fail(std::move(written));
            return;
        }

        AnswerGet(registry, path, std::move(reply));
    }
}
