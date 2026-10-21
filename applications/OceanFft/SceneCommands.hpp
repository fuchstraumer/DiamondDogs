#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
#define DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
#include <array>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/** One line of the demo's command language. Angles are degrees, lengths meters. One command per line, '#' starts a
 *  comment. The same commands come from --script=file and, with --stdin, from standard input between frames.
 *
 *    camera yaw pitch distance [tx ty tz]   set the orbit camera (absolute)
 *    orbit dyaw dpitch                     turn the camera around its target
 *    pan dx dy                             move the target in the view plane, in viewport heights
 *    move dx dy dz                         move the target in world space
 *    zoom steps                            positive moves closer (a mouse wheel step each)
 *    time seconds                          set the simulation time
 *    dt seconds                            fixed simulation step per frame (0 goes back to real time)
 *    pause on|off
 *    capture [name]                        write this frame to <capture dir>/<name>.png
 *    wait frames                           render this many frames before the next command
 *    print                                 print the camera and the time to stdout
 *    quit
 */
struct SceneCommand
{
    enum class Kind : uint8_t
    {
        Camera,
        Orbit,
        Pan,
        Move,
        Zoom,
        Time,
        TimeStep,
        Pause,
        Capture,
        Wait,
        Print,
        Quit,
    };

    Kind Type{ Kind::Print };
    std::array<float, 6> Values{};
    uint32_t ValueCount{ 0u };
    std::string Text;

    [[nodiscard]] static std::expected<SceneCommand, std::string> Parse(std::string_view line);
};

/** Holds the lines from the script and stdin, and hands out the commands for each frame. Thread safe: the stdin
 *  reader pushes lines from its own thread. */
class SceneCommands
{
public:
    /** @brief Appends the file's lines. Returns an error when it cannot be read */
    [[nodiscard]] std::expected<void, std::string> LoadScript(const std::filesystem::path& path);
    /** @brief Starts a detached thread that reads stdin until end of file */
    void StartStdin();
    void Push(std::string line);

    /** @brief The commands to run before this frame: everything up to a wait, which then holds the rest back for its
     *  frame count. Lines that do not parse are reported on stderr and skipped */
    [[nodiscard]] std::vector<SceneCommand> TakeForFrame();
    /** @brief True while a wait is counting down, or lines are queued */
    [[nodiscard]] bool Busy() const;

private:
    mutable std::mutex mutex;
    std::deque<std::string> lines;
    uint32_t waitFrames{ 0u };
};

#endif //!DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
