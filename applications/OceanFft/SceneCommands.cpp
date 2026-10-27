#include "SceneCommands.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "OceanSimulation.hpp"
#include "SeaState.hpp"

namespace
{
    struct CommandShape
    {
        SceneCommand::Kind Kind;
        uint32_t MinValues;
        uint32_t MaxValues;
        bool TakesText;
    };

    const std::unordered_map<std::string_view, CommandShape> k_Commands
    {
        { "camera", { SceneCommand::Kind::Camera, 3u, 6u, false } },
        { "orbit", { SceneCommand::Kind::Orbit, 2u, 2u, false } },
        { "pan", { SceneCommand::Kind::Pan, 2u, 2u, false } },
        { "move", { SceneCommand::Kind::Move, 3u, 3u, false } },
        { "zoom", { SceneCommand::Kind::Zoom, 1u, 1u, false } },
        { "time", { SceneCommand::Kind::Time, 1u, 1u, false } },
        { "dt", { SceneCommand::Kind::TimeStep, 1u, 1u, false } },
        { "pause", { SceneCommand::Kind::Pause, 0u, 0u, true } },
        { "choppiness", { SceneCommand::Kind::Choppiness, 1u, 1u, false } },
        { "ui", { SceneCommand::Kind::Ui, 0u, 0u, true } },
        { "preset", { SceneCommand::Kind::Preset, 0u, 0u, false } },
        { "sea", { SceneCommand::Kind::Sea, 0u, 0u, false } },
        { "cascade", { SceneCommand::Kind::Cascade, 0u, 0u, false } },
        { "foamweight", { SceneCommand::Kind::FoamWeight, 0u, 0u, false } },
        { "foam", { SceneCommand::Kind::Foam, 0u, 0u, false } },
        { "shading", { SceneCommand::Kind::Shading, 0u, 0u, false } },
        { "foamreset", { SceneCommand::Kind::FoamReset, 0u, 0u, false } },
        { "rotate", { SceneCommand::Kind::Rotate, 0u, 0u, true } },
        { "capture", { SceneCommand::Kind::Capture, 0u, 0u, true } },
        { "wait", { SceneCommand::Kind::Wait, 1u, 1u, false } },
        { "print", { SceneCommand::Kind::Print, 0u, 0u, false } },
        { "quit", { SceneCommand::Kind::Quit, 0u, 0u, false } },
    };

    const std::array<SeaStateField, 15> k_SeaStateFields
    {
        SeaStateField{ "WindForce", &ocean::SeaState::WindForce },
        SeaStateField{ "Openness", &ocean::SeaState::Openness },
        SeaStateField{ "WindDirection", &ocean::SeaState::WindDirection },
        SeaStateField{ "Spread", &ocean::SeaState::Spread },
        SeaStateField{ "Regularity", &ocean::SeaState::Regularity },
        SeaStateField{ "RippleDetail", &ocean::SeaState::RippleDetail },
        SeaStateField{ "Depth", &ocean::SeaState::Depth },
        SeaStateField{ "SwellHeight", &ocean::SeaState::SwellHeight },
        SeaStateField{ "SwellPeriod", &ocean::SeaState::SwellPeriod },
        SeaStateField{ "SwellDirection", &ocean::SeaState::SwellDirection },
        SeaStateField{ "SwellOrder", &ocean::SeaState::SwellOrder },
        SeaStateField{ "HeightScale", &ocean::SeaState::HeightScale },
        SeaStateField{ "WhitecapThreshold", &ocean::SeaState::WhitecapThreshold },
        SeaStateField{ "FoamAmount", &ocean::SeaState::FoamAmount },
        SeaStateField{ "CrestSharpness", &ocean::SeaState::CrestSharpness },
    };

    const std::array<std::string_view, 8> k_FoamFields
    {
        "enable", "raise", "decay", "gain", "drift", "residual", "softness", "scale"
    };

    const std::array<std::string_view, 4> k_ShadingFields
    {
        "underwater", "specular", "albedo", "normal"
    };

    std::vector<std::string_view> SplitWords(std::string_view line)
    {
        std::vector<std::string_view> words;

        size_t start = 0;
        while (start < line.size())
        {
            while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || line[start] == ','))
            {
                ++start;
            }

            size_t end = start;
            while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != ',')
            {
                ++end;
            }

            if (end > start)
            {
                words.push_back(line.substr(start, end - start));
            }

            start = end;
        }

        return words;
    }

    bool EqualsIgnoreCase(std::string_view left, std::string_view right);
    bool ParseFloat(std::string_view word, float& value);
    std::expected<SceneCommand, std::string> ParsePreset(const std::vector<std::string_view>& words);
    std::expected<SceneCommand, std::string> ParseSea(const std::vector<std::string_view>& words);
    std::expected<SceneCommand, std::string> ParseCascade(const std::vector<std::string_view>& words);
    std::expected<SceneCommand, std::string> ParseFoamWeight(const std::vector<std::string_view>& words);
    std::expected<SceneCommand, std::string> ParseFoam(const std::vector<std::string_view>& words);
    std::expected<SceneCommand, std::string> ParseShading(const std::vector<std::string_view>& words);
}

std::expected<SceneCommand, std::string> SceneCommand::Parse(std::string_view line)
{
    if (const size_t comment = line.find('#'); comment != std::string_view::npos)
    {
        line = line.substr(0, comment);
    }

    const std::vector<std::string_view> words = SplitWords(line);
    if (words.empty())
    {
        return std::unexpected(std::string{});
    }

    const auto found = k_Commands.find(words[0]);
    if (found == k_Commands.end())
    {
        return std::unexpected(std::string{ "unknown command '" } + std::string{ words[0] } + "'");
    }

    const CommandShape& shape = found->second;
    if (shape.Kind == Kind::Preset)
    {
        return ParsePreset(words);
    }

    if (shape.Kind == Kind::Sea)
    {
        return ParseSea(words);
    }

    if (shape.Kind == Kind::Cascade)
    {
        return ParseCascade(words);
    }

    if (shape.Kind == Kind::FoamWeight)
    {
        return ParseFoamWeight(words);
    }

    if (shape.Kind == Kind::Foam)
    {
        return ParseFoam(words);
    }

    if (shape.Kind == Kind::Shading)
    {
        return ParseShading(words);
    }

    SceneCommand command{};
    command.Type = shape.Kind;
    if (shape.TakesText)
    {
        if (words.size() > 2u)
        {
            return std::unexpected(std::string{ words[0] } + " takes at most one word");
        }

        command.Text = words.size() == 2u ? std::string{ words[1] } : std::string{};
        const bool takesOnOff = command.Type == Kind::Pause || command.Type == Kind::Ui || command.Type == Kind::Rotate;
        if (takesOnOff && command.Text != "on" && command.Text != "off")
        {
            return std::unexpected(std::string{ words[0] } + " takes on or off");
        }

        return command;
    }

    const size_t valueCount = words.size() - 1u;
    if (valueCount < shape.MinValues || valueCount > shape.MaxValues || (command.Type == Kind::Camera && valueCount != 3u && valueCount != 6u))
    {
        return std::unexpected(std::string{ words[0] } + ": wrong number of values");
    }

    for (size_t i = 0; i < valueCount; ++i)
    {
        const std::string_view word = words[i + 1u];
        float value = 0.0f;
        const std::from_chars_result result = std::from_chars(word.data(), word.data() + word.size(), value);
        if (result.ec != std::errc{} || result.ptr != word.data() + word.size())
        {
            return std::unexpected(std::string{ words[0] } + ": '" + std::string{ word } + "' is not a number");
        }

        command.Values[i] = value;
    }

    command.ValueCount = static_cast<uint32_t>(valueCount);
    if (command.Type == Kind::Wait && command.Values[0] < 0.0f)
    {
        return std::unexpected(std::string{ "wait takes a frame count of 0 or more" });
    }

    return command;
}

const SeaStateField* FindSeaStateField(std::string_view name)
{
    const auto found = std::ranges::find_if(k_SeaStateFields, [name](const SeaStateField& field)
    {
        return EqualsIgnoreCase(field.Name, name);
    });

    return found != k_SeaStateFields.end() ? &*found : nullptr;
}

const ocean::SeaStatePreset* FindSeaStatePreset(std::string_view name)
{
    const std::array<ocean::SeaStatePreset, 6>& presets = ocean::SeaStatePresets();
    const auto found = std::ranges::find_if(presets, [name](const ocean::SeaStatePreset& preset)
    {
        return EqualsIgnoreCase(preset.Name, name);
    });

    return found != presets.end() ? &*found : nullptr;
}

const std::array<SeaStateField, 15>& SeaStateFields() noexcept
{
    return k_SeaStateFields;
}

std::expected<void, std::string> SceneCommands::LoadScript(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        return std::unexpected("cannot open " + path.string());
    }

    std::string line;
    while (std::getline(file, line))
    {
        lines.push_back(std::move(line));
    }

    return {};
}

void SceneCommands::StartStdin()
{
    // detached: getline blocks until a line or end of file, and the process can exit while it waits. The thread
    // holds its own copy of the queue pointer, so a line that arrives after this object is destroyed still has a queue.
    std::thread([queue = incoming]
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            queue->push(std::move(line));
        }
    }).detach();
}

void SceneCommands::Push(std::string line)
{
    incoming->push(std::move(line));
}

void SceneCommands::drainIncoming() const
{
    std::optional<std::string> line = incoming->try_pop();
    while (line.has_value())
    {
        lines.push_back(std::move(*line));
        line = incoming->try_pop();
    }
}

std::vector<SceneCommand> SceneCommands::TakeForFrame()
{
    drainIncoming();

    std::vector<SceneCommand> commands;

    if (waitFrames > 0u)
    {
        --waitFrames;
        return commands;
    }

    while (!lines.empty())
    {
        const std::string line = std::move(lines.front());
        lines.pop_front();

        std::expected<SceneCommand, std::string> parsed = SceneCommand::Parse(line);
        if (!parsed.has_value())
        {
            if (!parsed.error().empty())
            {
                std::cerr << "command '" << line << "': " << parsed.error() << "\n";
            }

            continue;
        }

        if (parsed->Type == SceneCommand::Kind::Wait)
        {
            // this frame is the first of the wait
            const uint32_t frames = static_cast<uint32_t>(parsed->Values[0]);
            waitFrames = frames > 0u ? frames - 1u : 0u;
            if (frames > 0u)
            {
                break;
            }

            continue;
        }

        // a capture takes this frame as the commands so far left it, so the ones after it wait for the next frame
        const bool endsFrame = parsed->Type == SceneCommand::Kind::Capture;
        commands.push_back(std::move(*parsed));
        if (endsFrame)
        {
            break;
        }
    }

    return commands;
}

bool SceneCommands::Busy() const
{
    drainIncoming();

    return waitFrames > 0u || !lines.empty();
}

namespace
{
    bool EqualsIgnoreCase(std::string_view left, std::string_view right)
    {
        return std::ranges::equal(left, right, [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    }

    bool ParseFloat(std::string_view word, float& value)
    {
        const std::from_chars_result result = std::from_chars(word.data(), word.data() + word.size(), value);
        return result.ec == std::errc{} && result.ptr == word.data() + word.size();
    }

    std::expected<SceneCommand, std::string> ParsePreset(const std::vector<std::string_view>& words)
    {
        if (words.size() < 2u)
        {
            return std::unexpected(std::string{ "preset takes a name" });
        }

        // a preset name can have spaces ("Long swell"), so the words after the command are joined back together
        std::string name;
        for (size_t i = 1; i < words.size(); ++i)
        {
            if (i > 1u)
            {
                name += ' ';
            }

            name += words[i];
        }

        if (FindSeaStatePreset(name) == nullptr)
        {
            return std::unexpected(std::string{ "preset: unknown preset '" } + name + "'");
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::Preset;
        command.Text = std::move(name);
        return command;
    }

    std::expected<SceneCommand, std::string> ParseSea(const std::vector<std::string_view>& words)
    {
        if (words.size() != 3u)
        {
            return std::unexpected(std::string{ "sea takes a field name and a value" });
        }

        const SeaStateField* field = FindSeaStateField(words[1]);
        if (field == nullptr)
        {
            return std::unexpected(std::string{ "sea: unknown field '" } + std::string{ words[1] } + "'");
        }

        float value = 0.0f;
        if (!ParseFloat(words[2], value))
        {
            return std::unexpected(std::string{ "sea: '" } + std::string{ words[2] } + "' is not a number");
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::Sea;
        command.Text = std::string{ field->Name };
        command.Values[0] = value;
        command.ValueCount = 1u;
        return command;
    }

    std::expected<SceneCommand, std::string> ParseCascade(const std::vector<std::string_view>& words)
    {
        if (words.size() != 3u)
        {
            return std::unexpected(std::string{ "cascade takes an index and on or off" });
        }

        const std::string_view indexWord = words[1];
        int32_t index = -1;
        const std::from_chars_result result = std::from_chars(indexWord.data(), indexWord.data() + indexWord.size(), index);
        if (result.ec != std::errc{} || result.ptr != indexWord.data() + indexWord.size())
        {
            return std::unexpected(std::string{ "cascade: '" } + std::string{ indexWord } + "' is not a whole number");
        }

        if (index < 0 || index >= static_cast<int32_t>(ocean::k_CascadeCount))
        {
            return std::unexpected("cascade: index must be 0 to " + std::to_string(ocean::k_CascadeCount - 1u));
        }

        const std::string_view state = words[2];
        if (state != "on" && state != "off")
        {
            return std::unexpected(std::string{ "cascade takes on or off" });
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::Cascade;
        command.Text = std::string{ state };
        command.Values[0] = static_cast<float>(index);
        command.ValueCount = 1u;
        return command;
    }

    std::expected<SceneCommand, std::string> ParseFoamWeight(const std::vector<std::string_view>& words)
    {
        if (words.size() != 3u)
        {
            return std::unexpected(std::string{ "foamweight takes a cascade index and a value" });
        }

        const std::string_view indexWord = words[1];
        int32_t index = -1;
        const std::from_chars_result result = std::from_chars(indexWord.data(), indexWord.data() + indexWord.size(), index);
        if (result.ec != std::errc{} || result.ptr != indexWord.data() + indexWord.size())
        {
            return std::unexpected(std::string{ "foamweight: '" } + std::string{ indexWord } + "' is not a whole number");
        }

        if (index < 0 || index >= static_cast<int32_t>(ocean::k_CascadeCount))
        {
            return std::unexpected("foamweight: index must be 0 to " + std::to_string(ocean::k_CascadeCount - 1u));
        }

        float value = 0.0f;
        if (!ParseFloat(words[2], value))
        {
            return std::unexpected(std::string{ "foamweight: '" } + std::string{ words[2] } + "' is not a number");
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::FoamWeight;
        command.Values[0] = static_cast<float>(index);
        command.Values[1] = value;
        command.ValueCount = 2u;
        return command;
    }

    std::expected<SceneCommand, std::string> ParseFoam(const std::vector<std::string_view>& words)
    {
        if (words.size() != 3u)
        {
            return std::unexpected(std::string{ "foam takes a field and a value" });
        }

        const auto field = std::ranges::find_if(k_FoamFields, [&words](std::string_view name)
        {
            return EqualsIgnoreCase(name, words[1]);
        });

        if (field == k_FoamFields.end())
        {
            return std::unexpected(std::string{ "foam: unknown field '" } + std::string{ words[1] } + "'");
        }

        float value = 0.0f;
        if (!ParseFloat(words[2], value))
        {
            return std::unexpected(std::string{ "foam: '" } + std::string{ words[2] } + "' is not a number");
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::Foam;
        command.Text = std::string{ *field };
        command.Values[0] = value;
        command.ValueCount = 1u;
        return command;
    }

    std::expected<SceneCommand, std::string> ParseShading(const std::vector<std::string_view>& words)
    {
        if (words.size() != 3u)
        {
            return std::unexpected(std::string{ "shading takes a field and a value" });
        }

        const auto field = std::ranges::find_if(k_ShadingFields, [&words](std::string_view name)
        {
            return EqualsIgnoreCase(name, words[1]);
        });

        if (field == k_ShadingFields.end())
        {
            return std::unexpected(std::string{ "shading: unknown field '" } + std::string{ words[1] } + "'");
        }

        float value = 0.0f;
        if (!ParseFloat(words[2], value))
        {
            return std::unexpected(std::string{ "shading: '" } + std::string{ words[2] } + "' is not a number");
        }

        SceneCommand command{};
        command.Type = SceneCommand::Kind::Shading;
        command.Text = std::string{ *field };
        command.Values[0] = value;
        command.ValueCount = 1u;
        return command;
    }
}
