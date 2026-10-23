#include "SceneCommands.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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
        { "capture", { SceneCommand::Kind::Capture, 0u, 0u, true } },
        { "wait", { SceneCommand::Kind::Wait, 1u, 1u, false } },
        { "print", { SceneCommand::Kind::Print, 0u, 0u, false } },
        { "quit", { SceneCommand::Kind::Quit, 0u, 0u, false } },
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
    SceneCommand command{};
    command.Type = shape.Kind;
    if (shape.TakesText)
    {
        if (words.size() > 2u)
        {
            return std::unexpected(std::string{ words[0] } + " takes at most one word");
        }

        command.Text = words.size() == 2u ? std::string{ words[1] } : std::string{};
        if (command.Type == Kind::Pause && command.Text != "on" && command.Text != "off")
        {
            return std::unexpected(std::string{ "pause takes on or off" });
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
