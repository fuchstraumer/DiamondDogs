#include "ToolRegistry.hpp"
#include "ToolSchema.hpp"
#include "content/EncodeErased.hpp"
#include "content/JsonWriter.hpp"
#include "reflect/ValueAccess.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using foundation::content::JsonStyle;
    using foundation::content::JsonWriter;
    using foundation::reflect::FieldInfo;
    using foundation::reflect::FieldMetadata;
    using foundation::reflect::PathResult;
    using foundation::reflect::ResolvedValue;
    using foundation::reflect::TypeInfo;
    using foundation::reflect::TypeKind;
    using foundation::reflect::ValueResult;

    constexpr int32_t k_MaxSuggestions = 3;

    [[nodiscard]] bool PathsOverlap(std::string_view shorter, std::string_view longer) noexcept;
    // the first path step of an argument, without its index or its child fields
    [[nodiscard]] std::string_view FirstStep(std::string_view path) noexcept;
    [[nodiscard]] int32_t EditDistance(std::string_view first, std::string_view second);
    [[nodiscard]] std::vector<std::string> NearestNames(std::string_view word,
                                                        std::span<const std::string_view> names);
    // the leading part of a path with as many dots as the name, so that a wrong object name compares equal in
    // length
    [[nodiscard]] std::string_view LeadingStepsLike(std::string_view path, std::string_view name) noexcept;
    [[nodiscard]] std::vector<std::string> SuggestFields(const TypeInfo& root,
                                                         const void* object,
                                                         std::string_view field_path,
                                                         int32_t offset);
    [[nodiscard]] std::string DescribeValueError(const TypeInfo& root,
                                                 const void* object,
                                                 std::string_view field_path,
                                                 ValueResult error);
    [[nodiscard]] agent::ToolResult MakeValueFailure(const TypeInfo& root,
                                                      const void* object,
                                                      std::string_view whole_path,
                                                      std::string_view field_path,
                                                      int32_t field_path_start,
                                                      const PathResult& error);
    // the number of arguments that a required field needs: one per element of a fixed array of scalars, one
    // otherwise
    [[nodiscard]] int32_t RequiredArgumentCount(const FieldInfo& field);
    [[nodiscard]] bool IsScalarKind(TypeKind kind) noexcept;
    [[nodiscard]] agent::ToolResult CheckArgumentCounts(const TypeInfo& type,
                                                         std::span<const agent::NamedValue> arguments);
    void WriteObjectEntry(JsonWriter& writer, std::string_view path, const TypeInfo& type);
    void WriteCommandEntry(JsonWriter& writer,
                           std::string_view name,
                           std::string_view doc,
                           const TypeInfo& params,
                           const TypeInfo& reply);
    [[nodiscard]] std::string FinishDocument(JsonWriter& writer);
}

namespace agent
{
    namespace detail
    {
        ToolResult ApplyArguments(const TypeInfo& type,
                                   void* params,
                                   std::span<const NamedValue> arguments)
        {
            ToolResult counts = CheckArgumentCounts(type, arguments);

            if (!counts.Succeeded())
            {
                return counts;
            }

            for (const NamedValue& argument : arguments)
            {
                const PathResult written =
                    foundation::reflect::SetValue(type, params, argument.Path, argument.Value);

                if (!written.Succeeded())
                {
                    return MakeValueFailure(type, params, argument.Path, argument.Path, 0, written);
                }
            }

            return ToolResult{ .Code = ToolResultCode::Success };
        }
    }

    void ToolRegistry::Freeze() noexcept
    {
        frozen = true;
    }

    std::string ToolRegistry::DescribeAll() const
    {
        JsonWriter writer{ JsonStyle::Compact };

        writer.BeginObject();
        writer.Key("objects");
        writer.BeginArray();

        for (const ObjectEntry& object : objects)
        {
            WriteObjectEntry(writer, object.Path, *object.Type);
        }

        writer.EndArray();
        writer.Key("commands");
        writer.BeginArray();

        for (const CommandEntry& command : commands)
        {
            WriteCommandEntry(writer, command.Name, command.Doc, *command.Params, *command.Reply);
        }

        writer.EndArray();
        writer.EndObject();

        return FinishDocument(writer);
    }

    std::expected<std::string, ToolResult> ToolRegistry::Describe(std::string_view path) const
    {
        JsonWriter writer{ JsonStyle::Compact };
        const CommandEntry* command = findCommand(path);

        if (command != nullptr)
        {
            WriteCommandEntry(writer, command->Name, command->Doc, *command->Params, *command->Reply);

            return FinishDocument(writer);
        }

        std::expected<ObjectMatch, ToolResult> match = matchObject(path);

        if (!match.has_value())
        {
            return std::unexpected(std::move(match.error()));
        }

        const ObjectEntry& object = *match->Object;
        const std::expected<ResolvedValue, PathResult> resolved =
            foundation::reflect::ResolvePath(*object.Type, object.Address, match->FieldPath);

        if (!resolved.has_value())
        {
            return std::unexpected(MakeValueFailure(*object.Type, object.Address, path, match->FieldPath,
                                                    match->FieldPathStart, resolved.error()));
        }

        writer.BeginObject();
        writer.KeyString("path", path);
        writer.Key("schema");
        detail::WriteTypeSchema(writer, *resolved->Type, resolved->Metadata);
        writer.EndObject();

        return FinishDocument(writer);
    }

    ToolResult ToolRegistry::GetValue(std::string_view path, foundation::content::CanonicalSink& sink) const
    {
        std::expected<ObjectMatch, ToolResult> match = matchObject(path);

        if (!match.has_value())
        {
            return std::move(match.error());
        }

        const ObjectEntry& object = *match->Object;
        const std::expected<ResolvedValue, PathResult> resolved =
            foundation::reflect::ResolvePath(*object.Type, object.Address, match->FieldPath);

        if (!resolved.has_value())
        {
            return MakeValueFailure(*object.Type,
                                    object.Address,
                                    path,
                                    match->FieldPath,
                                    match->FieldPathStart,
                                    resolved.error());
        }

        foundation::content::EncodeErased(*resolved->Type, resolved->Address, sink);

        return ToolResult{ .Code = ToolResultCode::Success };
    }

    ToolResult ToolRegistry::SetValue(std::string_view path, const foundation::reflect::ScalarValue& value)
    {
        std::expected<ObjectMatch, ToolResult> match = matchObject(path);

        if (!match.has_value())
        {
            return std::move(match.error());
        }

        const ObjectEntry& object = *match->Object;
        const PathResult written =
            foundation::reflect::SetValue(*object.Type, object.Address, match->FieldPath, value);

        if (!written.Succeeded())
        {
            return MakeValueFailure(*object.Type,
                                    object.Address,
                                    path,
                                    match->FieldPath,
                                    match->FieldPathStart,
                                    written);
        }

        if (object.OnChanged.has_value())
        {
            (*object.OnChanged)(match->FieldPath);
        }

        return ToolResult{ .Code = ToolResultCode::Success };
    }

    void ToolRegistry::Call(std::string_view name, std::span<const NamedValue> arguments, PendingReply reply)
    {
        const CommandEntry* command = findCommand(name);

        if (command != nullptr)
        {
            command->Invoke(command->Command, arguments, std::move(reply));
            return;
        }

        const auto commandNames = commands | std::views::transform(&CommandEntry::Name);
        const std::vector<std::string_view> names(commandNames.begin(), commandNames.end());

        reply.Fail(ToolResult
        {
            .Code = ToolResultCode::UnknownCommand,
            .Message = std::format("no command has the name '{}'", name),
            .Path = std::string{ name },
            .Suggestions = NearestNames(name, names),
        });
    }

    ToolResultCode ToolRegistry::addObject(std::string_view path,
                                      const TypeInfo& type,
                                      void* address,
                                      std::optional<ObjectChanged> on_changed)
    {
        if (frozen)
        {
            return ToolResultCode::RegistryFrozen;
        }

        for (const ObjectEntry& object : objects)
        {
            if (object.Path == path)
            {
                return ToolResultCode::DuplicateObject;
            }

            if (PathsOverlap(object.Path, path) || PathsOverlap(path, object.Path))
            {
                return ToolResultCode::OverlappingObject;
            }
        }

        objects.push_back(ObjectEntry
        {
            .Path = std::string{ path },
            .Type = &type,
            .Address = address,
            .OnChanged = on_changed,
        });

        return ToolResultCode::Success;
    }

    ToolResultCode ToolRegistry::addCommand(const CommandEntry& entry)
    {
        if (frozen)
        {
            return ToolResultCode::RegistryFrozen;
        }

        if (findCommand(entry.Name) != nullptr)
        {
            return ToolResultCode::DuplicateCommand;
        }

        commands.push_back(entry);

        return ToolResultCode::Success;
    }

    const ToolRegistry::CommandEntry* ToolRegistry::findCommand(std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(commands, name, &CommandEntry::Name);

        return found != commands.end() ? &*found : nullptr;
    }

    std::expected<ToolRegistry::ObjectMatch,
        ToolResult> ToolRegistry::matchObject(std::string_view path) const
    {
        for (const ObjectEntry& object : objects)
        {
            if (path == object.Path)
            {
                return ObjectMatch{ .Object = &object, .FieldPathStart = static_cast<int32_t>(path.size()) };
            }

            if (PathsOverlap(object.Path, path))
            {
                const bool startsWithIndex = path[object.Path.size()] == '[';
                const size_t fieldPathStart = object.Path.size() + (startsWithIndex ? 0u : 1u);

                return ObjectMatch
                {
                    .Object = &object,
                    .FieldPath = path.substr(fieldPathStart),
                    .FieldPathStart = static_cast<int32_t>(fieldPathStart),
                };
            }
        }

        std::vector<std::string> suggestions;

        for (const ObjectEntry& object : objects)
        {
            const std::string_view leading = LeadingStepsLike(path, object.Path);
            const std::string_view name{ object.Path };
            std::vector<std::string> nearest = NearestNames(leading,
                                                            std::span<const std::string_view>{ &name,
                                                            1u });

            suggestions.insert(suggestions.end(),
                               std::make_move_iterator(nearest.begin()),
                               std::make_move_iterator(nearest.end()));
        }

        return std::unexpected(ToolResult
        {
            .Code = ToolResultCode::UnknownObject,
            .Message = std::format("no object contains the path '{}'", path),
            .Path = std::string{ path },
            .Offset = 0,
            .Suggestions = std::move(suggestions),
        });
    }
}

namespace
{
    bool PathsOverlap(std::string_view shorter, std::string_view longer) noexcept
    {
        if (longer.size() <= shorter.size() || !longer.starts_with(shorter))
        {
            return false;
        }

        const char next = longer[shorter.size()];

        return next == '.' || next == '[';
    }

    std::string_view FirstStep(std::string_view path) noexcept
    {
        return path.substr(0u, path.find_first_of(".["));
    }

    int32_t EditDistance(std::string_view first, std::string_view second)
    {
        std::vector<int32_t> previous(second.size() + 1u);
        std::vector<int32_t> current(second.size() + 1u);

        for (size_t column = 0u; column < previous.size(); ++column)
        {
            previous[column] = static_cast<int32_t>(column);
        }

        for (size_t row = 0u; row < first.size(); ++row)
        {
            current[0] = static_cast<int32_t>(row) + 1;

            for (size_t column = 0u; column < second.size(); ++column)
            {
                const unsigned char firstChar = static_cast<unsigned char>(first[row]);
                const unsigned char secondChar = static_cast<unsigned char>(second[column]);
                const bool same = std::tolower(firstChar) == std::tolower(secondChar);
                const int32_t replaced = previous[column] + (same ? 0 : 1);
                const int32_t removed = previous[column + 1u] + 1;
                const int32_t inserted = current[column] + 1;

                current[column + 1u] = std::min({ replaced, removed, inserted });
            }

            std::swap(previous, current);
        }

        return previous.back();
    }

    std::vector<std::string> NearestNames(std::string_view word, std::span<const std::string_view> names)
    {
        struct Candidate
        {
            int32_t Distance{ 0 };
            std::string_view Name;
        };

        const int32_t limit = std::max(2, static_cast<int32_t>(word.size()) / 4);
        std::vector<Candidate> candidates;

        for (const std::string_view name : names)
        {
            const int32_t distance = EditDistance(word, name);

            if (distance <= limit)
            {
                candidates.push_back(Candidate{ .Distance = distance, .Name = name });
            }
        }

        std::ranges::stable_sort(candidates, {}, &Candidate::Distance);

        const int64_t keptCount = std::min<int64_t>(std::ssize(candidates), k_MaxSuggestions);
        std::vector<std::string> nearest;

        nearest.reserve(static_cast<size_t>(keptCount));

        for (const Candidate& candidate : candidates | std::views::take(keptCount))
        {
            nearest.emplace_back(candidate.Name);
        }

        return nearest;
    }

    std::string_view LeadingStepsLike(std::string_view path, std::string_view name) noexcept
    {
        const auto dotCount = std::ranges::count(name, '.');
        size_t position = 0u;

        for (int64_t step = 0; step <= dotCount; ++step)
        {
            position = path.find('.', position);

            if (position == std::string_view::npos)
            {
                return path;
            }

            if (step < dotCount)
            {
                ++position;
            }
        }

        return path.substr(0u, position);
    }

    std::vector<std::string> SuggestFields(const TypeInfo& root,
                                           const void* object,
                                           std::string_view field_path,
                                           int32_t offset)
    {
        std::string_view owner = field_path.substr(0u, static_cast<size_t>(offset));

        if (owner.ends_with('.'))
        {
            owner.remove_suffix(1u);
        }

        const std::expected<ResolvedValue, PathResult> resolved = foundation::reflect::ResolvePath(root,
            object, owner);

        if (!resolved.has_value() || resolved->Type->Kind != TypeKind::Struct)
        {
            return {};
        }

        std::vector<std::string_view> names;

        for (const FieldInfo& field : resolved->Type->Fields)
        {
            if (!field.Metadata.Hidden)
            {
                names.push_back(field.Name);
            }
        }

        // the walk stops at the dot in front of a nested name
        std::string_view rest = field_path.substr(static_cast<size_t>(offset));

        if (rest.starts_with('.'))
        {
            rest.remove_prefix(1u);
        }

        const std::string_view word = FirstStep(rest);

        return NearestNames(word, names);
    }

    std::string DescribeValueError(const TypeInfo& root,
                                   const void* object,
                                   std::string_view field_path,
                                   ValueResult error)
    {
        std::string message{ foundation::reflect::ValueResultText(error) };

        if (error != ValueResult::OutOfRange)
        {
            return message;
        }

        const std::expected<ResolvedValue, PathResult> resolved =
            foundation::reflect::ResolvePath(root, object, field_path);

        if (resolved.has_value() && resolved->Metadata != nullptr && resolved->Metadata->Range.has_value())
        {
            message += std::format(" ({} to {})", resolved->Metadata->Range->Min,
                                   resolved->Metadata->Range->Max);
        }

        return message;
    }

    agent::ToolResult MakeValueFailure(const TypeInfo& root,
                                        const void* object,
                                        std::string_view whole_path,
                                        std::string_view field_path,
                                        int32_t field_path_start,
                                        const PathResult& error)
    {
        agent::ToolResult failure
        {
            .Code = agent::ToolResultCode::ValueRefused,
            .Message = DescribeValueError(root, object, field_path, error.Code),
            .Path = std::string{ whole_path },
            .Offset = field_path_start + error.Offset,
        };

        if (error.Code == ValueResult::UnknownField)
        {
            failure.Suggestions = SuggestFields(root, object, field_path, error.Offset);
        }

        return failure;
    }

    int32_t RequiredArgumentCount(const FieldInfo& field)
    {
        const TypeInfo& type = field.Type();

        const bool isFixedSequence = type.Kind == TypeKind::Sequence && type.FixedElementCount > 0;

        if (isFixedSequence && IsScalarKind(type.Element().Kind))
        {
            return static_cast<int32_t>(type.FixedElementCount);
        }

        return 1;
    }

    bool IsScalarKind(TypeKind kind) noexcept
    {
        return kind == TypeKind::Bool || kind == TypeKind::Int || kind == TypeKind::Float32 ||
            kind == TypeKind::Float64 || kind == TypeKind::String || kind == TypeKind::Enum;
    }

    agent::ToolResult CheckArgumentCounts(const TypeInfo& type, std::span<const agent::NamedValue> arguments)
    {
        for (auto&& [index, argument] : std::views::enumerate(arguments))
        {
            const auto earlier = arguments.first(static_cast<size_t>(index));

            if (std::ranges::find(earlier, argument.Path, &agent::NamedValue::Path) != earlier.end())
            {
                return agent::ToolResult
                {
                    .Code = agent::ToolResultCode::DuplicateArgument,
                    .Message = std::format("the argument '{}' appears more than once", argument.Path),
                    .Path = std::string{ argument.Path },
                };
            }
        }

        for (const FieldInfo& field : type.Fields)
        {
            if (!field.Metadata.Required)
            {
                continue;
            }

            const std::string_view fieldName = field.Name;
            const auto namesThisField = [fieldName](const agent::NamedValue& argument)
            {
                return FirstStep(argument.Path) == fieldName;
            };
            const auto givenCount = std::ranges::count_if(arguments, namesThisField);
            const int32_t neededCount = RequiredArgumentCount(field);

            if (givenCount < neededCount)
            {
                return agent::ToolResult
                {
                    .Code = agent::ToolResultCode::MissingArgument,
                    .Message = std::format("the required argument '{}' has {} of {} values", fieldName,
                                           givenCount,
                                           neededCount),
                    .Path = std::string{ fieldName },
                };
            }
        }

        return agent::ToolResult{ .Code = agent::ToolResultCode::Success };
    }

    void WriteObjectEntry(JsonWriter& writer, std::string_view path, const TypeInfo& type)
    {
        writer.BeginObject();
        writer.KeyString("path", path);
        writer.Key("schema");
        agent::detail::WriteTypeSchema(writer, type, nullptr);
        writer.EndObject();
    }

    void WriteCommandEntry(JsonWriter& writer,
                           std::string_view name,
                           std::string_view doc,
                           const TypeInfo& params,
                           const TypeInfo& reply)
    {
        writer.BeginObject();
        writer.KeyString("name", name);
        writer.KeyString("doc", doc);
        writer.Key("params");
        agent::detail::WriteTypeSchema(writer, params, nullptr);
        writer.Key("reply");
        agent::detail::WriteTypeSchema(writer, reply, nullptr);
        writer.EndObject();
    }

    std::string FinishDocument(JsonWriter& writer)
    {
        foundation::content::JsonResult<std::string> document = writer.Finish();

        return std::move(*document);
    }
}
