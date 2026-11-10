#pragma once
#ifndef DIAMOND_DOGS_AGENT_TOOLS_TOOL_REGISTRY_HPP
#define DIAMOND_DOGS_AGENT_TOOLS_TOOL_REGISTRY_HPP
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include "reflect/ValueAccess.hpp"
#include "utility/Delegate.hpp"
#include <concepts>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace foundation::content
{
    class CanonicalSink;
}

namespace agent
{
    /** One argument of a command call: a path inside the command's Params, and a scalar. An array argument
     *  arrives as one value per element ("Eye[0]", "Eye[1]"). */
    struct NamedValue
    {
        std::string_view Path;
        foundation::reflect::ScalarValue Value;
    };

    /** The Params or the Reply of a command that has none. */
    struct NoParams
    {};

    struct NoReply
    {};
}

template<>
struct foundation::reflect::Describe<agent::NoParams>
{
    static constexpr std::string_view Name{ "agent.NoParams" };
    static constexpr std::tuple Fields{};
};

template<>
struct foundation::reflect::Describe<agent::NoReply>
{
    static constexpr std::string_view Name{ "agent.NoReply" };
    static constexpr std::tuple Fields{};
};

namespace agent
{
    /** A verb that a tool can call. Params and Reply need descriptors, which provide the schemas. Run answers
     *  through the token, now or on a later frame. */
    template<typename C>
    concept ToolCommand =
        foundation::reflect::DescribedStruct<typename C::Params> &&
        foundation::reflect::DescribedStruct<typename C::Reply> &&
        std::default_initializable<typename C::Params> &&
        requires(C& command, const typename C::Params& params, ReplyToken<typename C::Reply> token)
    {
        { C::Name } -> std::convertible_to<std::string_view>;
        { C::Doc } -> std::convertible_to<std::string_view>;
        command.Run(params, std::move(token));
    };

    // the path of the field that changed, relative to the object
    using ObjectChanged = Delegate<void(std::string_view)>;

    namespace detail
    {
        /** Writes the arguments into params. Every check runs first, and a required field needs an argument.
         *  */
        [[nodiscard]] ToolResult ApplyArguments(const foundation::reflect::TypeInfo& type,
                                                 void* params,
                                                 std::span<const NamedValue> arguments);

        template<ToolCommand C>
        void InvokeCommand(void* command, std::span<const NamedValue> arguments, PendingReply reply)
        {
            typename C::Params params{};

            ToolResult applied =
                ApplyArguments(foundation::reflect::GetTypeInfo<typename C::Params>(), &params, arguments);

            if (!applied.Succeeded())
            {
                reply.Fail(std::move(applied));
                return;
            }

            const typename C::Params& constParams = params;

            static_cast<C*>(command)->Run(constParams, ReplyToken<typename C::Reply>{ std::move(reply) });
        }
    }

    /** The tree of objects and the list of commands that tools can see. Objects and commands exist here only
     *  when an application adds them, so a described type stays private by default. Add everything on one
     *  thread, and then call Freeze. After that the tables never change. Every request runs on the thread
     *  that owns the objects. */
    class ToolRegistry
    {
    public:
        /** @brief Exposes a live object at a dotted path. The object must outlive the registry. */
        template<foundation::reflect::DescribedStruct T>
        [[nodiscard]] ToolResultCode AddObject(std::string_view path, T& object)
        {
            return addObject(path, foundation::reflect::GetTypeInfo<T>(), &object, std::nullopt);
        }

        /** @brief Exposes a live object, and calls on_changed after each write through the registry. */
        template<foundation::reflect::DescribedStruct T>
        [[nodiscard]] ToolResultCode AddObject(std::string_view path, T& object, ObjectChanged on_changed)
        {
            return addObject(path, foundation::reflect::GetTypeInfo<T>(), &object, on_changed);
        }

        /** @brief Exposes a command. The command must outlive the registry. */
        template<ToolCommand C>
        [[nodiscard]] ToolResultCode AddCommand(C& command)
        {
            static_assert(!std::string_view{ C::Doc }.empty(), "a tool command needs a Doc");

            return addCommand(CommandEntry
            {
                .Name = C::Name,
                .Doc = C::Doc,
                .Params = &foundation::reflect::GetTypeInfo<typename C::Params>(),
                .Reply = &foundation::reflect::GetTypeInfo<typename C::Reply>(),
                .Command = &command,
                .Invoke = &detail::InvokeCommand<C>,
            });
        }

        void Freeze() noexcept;

        /** @brief Returns one JSON document with every object and command, and their schemas. */
        [[nodiscard]] std::string DescribeAll() const;
        /** @brief Returns the schema of one command, object or field as JSON. */
        [[nodiscard]] std::expected<std::string, ToolResult> Describe(std::string_view path) const;
        /** @brief Writes the value at the path to the sink. */
        [[nodiscard]] ToolResult GetValue(std::string_view path,
                                           foundation::content::CanonicalSink& sink) const;
        /** @brief Writes one scalar at the path. A refused write changes nothing and calls no callback. */
        [[nodiscard]] ToolResult SetValue(std::string_view path,
                                           const foundation::reflect::ScalarValue& value);
        /** @brief Runs a command. The answer arrives through the reply, now or later. */
        void Call(std::string_view name, std::span<const NamedValue> arguments, PendingReply reply);

    private:
        struct ObjectEntry
        {
            std::string Path;
            const foundation::reflect::TypeInfo* Type{ nullptr };
            void* Address{ nullptr };
            std::optional<ObjectChanged> OnChanged;
        };

        struct CommandEntry
        {
            std::string_view Name;
            std::string_view Doc;
            const foundation::reflect::TypeInfo* Params{ nullptr };
            const foundation::reflect::TypeInfo* Reply{ nullptr };
            void* Command{ nullptr };
            void (*Invoke)(void* command, std::span<const NamedValue> arguments,
                           PendingReply reply){ nullptr };
        };

        struct ObjectMatch
        {
            const ObjectEntry* Object{ nullptr };
            // the path inside the object. Empty for the object itself
            std::string_view FieldPath;
            // the position of FieldPath in the whole path
            int32_t FieldPathStart{ 0 };
        };

        [[nodiscard]] ToolResultCode addObject(std::string_view path,
                                          const foundation::reflect::TypeInfo& type,
                                          void* address,
                                          std::optional<ObjectChanged> on_changed);
        [[nodiscard]] ToolResultCode addCommand(const CommandEntry& entry);
        [[nodiscard]] const CommandEntry* findCommand(std::string_view name) const noexcept;
        [[nodiscard]] std::expected<ObjectMatch, ToolResult> matchObject(std::string_view path) const;

        std::vector<ObjectEntry> objects;
        std::vector<CommandEntry> commands;
        bool frozen{ false };
    };
}

#endif // !DIAMOND_DOGS_AGENT_TOOLS_TOOL_REGISTRY_HPP
