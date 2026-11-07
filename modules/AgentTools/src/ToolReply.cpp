#include "ToolReply.hpp"
#include "content/EncodeErased.hpp"
#include "content/JsonSink.hpp"
#include <cassert>
#include <string>
#include <utility>

namespace agent
{
    PendingReply::PendingReply(ReplySink& _sink, int64_t request_id) noexcept :
        sink{ &_sink },
        requestId{ request_id }
    {}

    PendingReply::PendingReply(PendingReply&& _other) noexcept :
        sink{ std::exchange(_other.sink, nullptr) },
        requestId{ _other.requestId }
    {}

    PendingReply& PendingReply::operator=(PendingReply&& _other) noexcept
    {
        if (this != &_other)
        {
            if (sink != nullptr)
            {
                Fail(ToolResult{ .Code = ToolResultCode::NoReply, .Message = "the command gave no reply" });
            }

            sink = std::exchange(_other.sink, nullptr);
            requestId = _other.requestId;
        }

        return *this;
    }

    PendingReply::~PendingReply()
    {
        if (sink != nullptr)
        {
            Fail(ToolResult{ .Code = ToolResultCode::NoReply, .Message = "the command gave no reply" });
        }
    }

    void PendingReply::Complete(const foundation::reflect::TypeInfo& type, const void* value)
    {
        foundation::content::JsonSink json{ foundation::content::JsonStyle::Compact };

        foundation::content::EncodeErased(type, value, json);

        foundation::content::JsonResult<std::string> document = json.Finish();

        CompleteWithJson(std::move(*document));
    }

    void PendingReply::CompleteWithJson(std::string result_json)
    {
        assert(sink != nullptr && "a reply can answer only once");

        std::exchange(sink, nullptr)->Finish(requestId, std::move(result_json));
    }

    void PendingReply::Fail(ToolResult failure)
    {
        assert(sink != nullptr && "a reply can answer only once");

        std::exchange(sink, nullptr)->Fail(requestId, std::move(failure));
    }
}
