#include "ToolLine.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolRequests.hpp"
#include "ToolResult.hpp"
#include "reflect/ValueAccess.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using foundation::reflect::ScalarValue;

    constexpr std::string_view k_Usage{
        "describe [path], get path, set path value, or call command [argument=value ...]" };

    [[nodiscard]] std::vector<std::string_view> SplitWords(std::string_view line);
    [[nodiscard]] ScalarValue ParseValue(std::string_view word);
    template<typename Number>
    [[nodiscard]] bool ParseWholeWord(std::string_view word, Number& number);
    [[nodiscard]] agent::ToolResult MakeLineFailure(std::string message, std::string_view line);
    void RunDescribe(const agent::ToolRegistry& registry,
                     const std::vector<std::string_view>& words,
                     std::string_view line,
                     agent::PendingReply reply);
    void RunCall(agent::ToolRegistry& registry,
                 const std::vector<std::string_view>& words,
                 agent::PendingReply reply);
}

namespace agent
{
    void RunToolLine(ToolRegistry& registry, std::string_view line, ReplySink& sink, int64_t request_id)
    {
        PendingReply reply{ sink, request_id };
        const std::vector<std::string_view> words = SplitWords(line);
        const std::string_view verb = words.empty() ? std::string_view{} : words[0];

        if (verb == "describe")
        {
            RunDescribe(registry, words, line, std::move(reply));
        }
        else if (verb == "get" && words.size() == 2u)
        {
            detail::AnswerGet(registry, words[1], std::move(reply));
        }
        else if (verb == "set" && words.size() == 3u)
        {
            detail::AnswerSet(registry, words[1], ParseValue(words[2]), std::move(reply));
        }
        else if (verb == "call" && words.size() >= 2u)
        {
            RunCall(registry, words, std::move(reply));
        }
        else
        {
            reply.Fail(MakeLineFailure(std::string{ k_Usage }, line));
        }
    }
}

namespace
{
    std::vector<std::string_view> SplitWords(std::string_view line)
    {
        std::vector<std::string_view> words;
        size_t position = line.find_first_not_of(' ');

        while (position != std::string_view::npos)
        {
            const size_t end = line.find(' ', position);

            words.push_back(line.substr(position, end == std::string_view::npos ? end : end - position));
            position = line.find_first_not_of(' ', end);
        }

        return words;
    }

    ScalarValue ParseValue(std::string_view word)
    {
        if (word == "true")
        {
            return ScalarValue{ true };
        }

        if (word == "false")
        {
            return ScalarValue{ false };
        }

        int64_t signedValue = 0;

        if (ParseWholeWord(word, signedValue))
        {
            return ScalarValue{ signedValue };
        }

        uint64_t unsignedValue = 0u;

        if (ParseWholeWord(word, unsignedValue))
        {
            return ScalarValue{ unsignedValue };
        }

        double floatingValue = 0.0;

        if (ParseWholeWord(word, floatingValue))
        {
            return ScalarValue{ floatingValue };
        }

        return ScalarValue{ word };
    }

    template<typename Number>
    bool ParseWholeWord(std::string_view word, Number& number)
    {
        const char* end = word.data() + word.size();
        const std::from_chars_result result = std::from_chars(word.data(), end, number);

        return result.ec == std::errc{} && result.ptr == end;
    }

    agent::ToolResult MakeLineFailure(std::string message, std::string_view line)
    {
        return agent::ToolResult
        {
            .Code = agent::ToolResultCode::MalformedLine,
            .Message = std::move(message),
            .Path = std::string{ line },
        };
    }

    void RunDescribe(const agent::ToolRegistry& registry,
                     const std::vector<std::string_view>& words,
                     std::string_view line,
                     agent::PendingReply reply)
    {
        if (words.size() > 2u)
        {
            reply.Fail(MakeLineFailure(std::string{ k_Usage }, line));
            return;
        }

        const std::string_view path = words.size() == 2u ? words[1] : std::string_view{};

        agent::detail::AnswerDescribe(registry, path, std::move(reply));
    }

    void RunCall(agent::ToolRegistry& registry,
                 const std::vector<std::string_view>& words,
                 agent::PendingReply reply)
    {
        std::vector<agent::NamedValue> arguments;

        arguments.reserve(words.size() - 2u);

        for (size_t index = 2u; index < words.size(); ++index)
        {
            const std::string_view word = words[index];
            const size_t equals = word.find('=');

            if (equals == std::string_view::npos)
            {
                reply.Fail(MakeLineFailure(std::format("the argument '{}' needs the form name=value", word),
                           word));
                return;
            }

            arguments.push_back(agent::NamedValue
            {
                .Path = word.substr(0u, equals),
                .Value = ParseValue(word.substr(equals + 1u)),
            });
        }

        registry.Call(words[1], arguments, std::move(reply));
    }
}
