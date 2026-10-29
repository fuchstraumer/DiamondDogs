#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
#define DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
#include <array>
#include <cstdint>
#include <deque>
#include "containers/mwsrQueue.hpp"
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "SeaState.hpp"

/** One line of the demo's command language. Angles are degrees, lengths meters. One command per line, '#' starts a
 *  comment. The same commands come from --script=file and, with --stdin, from standard input between frames.
 *
 *    camera yaw pitch distance [tx ty tz]   set the orbit camera (absolute)
 *    camera_reset                          reset the orbit camera to its default view
 *    lookat ex ey ez tx ty tz              set the camera from eye and target world coordinates
 *    orbit dyaw dpitch                     turn the camera around its target
 *    pan dx dy                             move the target in the view plane, in viewport heights
 *    move dx dy dz                         move the target in world space
 *    zoom steps                            positive moves closer (a mouse wheel step each)
 *    time seconds                          set the simulation time
 *    dt seconds                            fixed simulation step per frame (0 goes back to real time)
 *    pause on|off
 *    choppiness value                      the horizontal displacement multiplier (lambda), as the UI slider
 *    ui on|off                             show or hide the panel, for clean captures
 *    preset name                           apply a sea state preset by name (Glassy, Breezy, Fresh, Gale, GaleFoam, Long swell), as the preset buttons do
 *    sea field value                       set one sea state field by its C++ name (WindForce, Openness, ...), as its
 *                                          slider does. print writes the same names
 *    cascade index on|off                  enable or disable one cascade, index 0 to 4
 *    foamweight index value                set one cascade's foam weight (0 to 1.5 in the UI, 1 is the whitecap
 *                                          model as computed), index 0 to 4
 *    foam field value                      set one persistent foam field: enable (0 or 1), raise (threshold raise, 0 to 0.5),
 *                                          decay (seconds), gain (persistence gain, 1 = averaged coverage), drift
 *                                          (fraction of wind speed), residual (amount), softness, scale (meters per tile of the foam texture)
 *    shading field value                   set one lookdev field: underwater (0 to 2, tints the water under the foam and
 *                                          removes its specular), specular (0 to 2, the sun highlight scale, 0 is off),
 *                                          albedo (0 to 4, the brightness gain of the foam texture), normal (0 to 2, its normal strength)
 *    foamreset                             rebuild the persistent foam by replaying its history
 *    rotate on|off                         rotate and offset each cascade's lattice when sampled (tiling A/B)
 *    sky preset                            set the sky preset (preetham, aurora, day, night, overcast, twilight)
 *    sky_res 8k|16k                        set sky texture resolution (8k downsampled or 16k full)
 *    sky_intensity value                   set sky radiance multiplier
 *    tonemap name                          set tonemapper (agx, neutral, passthrough, falsecolor)
 *    exposure ev                           set exposure in EV stops
 *    bloom on|off                          enable or disable bloom
 *    bloom intensity [scatter]             set bloom intensity and optional scatter
 *    cdlod on|off                          enable or disable CDLOD terrain tessellation
 *    cdlod_scale value                     set CDLOD LOD distance multiplier
 *    cdlod_morph value                     set CDLOD morph start ratio (0.1 to 0.95)
 *    cdlod_max_level value                 set CDLOD maximum subdivision level (1 to 14, 0 for auto)
 *    wireframe on|off                      enable or disable wireframe polygon mode
 *    brdf field value                      set BRDF parameter: sss, leadr, foam_roughness, seabed_roughness
 *    async_compute on|off                  enable or disable async compute execution
 *    capture [name]                        write this frame to <capture dir>/<name>.png. The commands after it
 *                                          run on the next frame, so the capture sees only the ones before it
 *    sync [token]                          flush device idle, flush captures, and print READY <token> frame=<frame>
 *    wait frames                           render this many frames before the next command
 *    print                                 print the camera, sky, tonemap, exposure, bloom, CDLOD, sea state, and cascades
 *    quit
 */
struct SceneCommand
{
    enum class Kind : uint8_t
    {
        Camera,
        CameraReset,
        LookAt,
        Orbit,
        Pan,
        Move,
        Zoom,
        Time,
        TimeStep,
        Pause,
        Choppiness,
        Ui,
        Preset,
        Sea,
        Cascade,
        FoamWeight,
        Foam,
        Shading,
        FoamReset,
        Rotate,
        Sky,
        SkyRes,
        SkyIntensity,
        Tonemap,
        Exposure,
        Bloom,
        Cdlod,
        CdlodScale,
        CdlodMorph,
        CdlodMaxLevel,
        Wireframe,
        Brdf,
        AsyncCompute,
        Tab,
        Deferred,
        SpecAa,
        Taa,
        Ssr,
        AutoExposure,
        Capture,
        Sync,
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

/** One float field of the sea state. Name is the C++ member name, which the sea command accepts in any case */
struct SeaStateField
{
    std::string_view Name;
    float ocean::SeaState::* Member;
};

/** @brief Returns the field whose name matches, ignoring case, or null when no field matches */
[[nodiscard]] const SeaStateField* FindSeaStateField(std::string_view name);
/** @brief Returns the preset whose name matches, ignoring case, or null when no preset matches */
[[nodiscard]] const ocean::SeaStatePreset* FindSeaStatePreset(std::string_view name);
/** @brief Returns every float field of the sea state, in declaration order */
[[nodiscard]] const std::array<SeaStateField, 15>& SeaStateFields() noexcept;

/** Holds the lines from the script and stdin, and hands out the commands for each frame. Push may be called from any
 *  thread. The stdin reader pushes through the queue. Every other method runs on the frame thread. */
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
    void drainIncoming() const;

    std::shared_ptr<mwsrQueue<std::string>> incoming{ std::make_shared<mwsrQueue<std::string>>() };
    mutable std::deque<std::string> lines;
    uint32_t waitFrames{ 0u };
};

#endif //!DIAMOND_DOGS_OCEAN_SCENE_COMMANDS_HPP
