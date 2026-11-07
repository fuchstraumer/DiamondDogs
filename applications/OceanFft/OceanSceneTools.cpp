#include "OceanScene.hpp"
#include "SceneCommands.hpp"
#include "SeaState.reflect.hpp"
#include "Swapchain.hpp"
#include "ToolLine.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "ToolServer.hpp"
#include "reflect/Describe.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace ocean_tools
{
    // frames that a held time renders before the reply. SSR reflects the previous frame, so the first frame at a new
    // time still shows some of the old one. Measured: with frame.repeatable on, the second frame is already exact
    constexpr int32_t k_DefaultSettleFrames = 2;

    struct CameraReply
    {
        float Yaw{ 0.0f };
        float Pitch{ 0.0f };
        float Distance{ 0.0f };
        std::array<float, 3> Target{};
    };

    struct CameraSetParams
    {
        float Yaw{ 0.0f };
        float Pitch{ 0.0f };
        float Distance{ 0.0f };
    };

    struct CameraLookAtParams
    {
        std::array<float, 3> Eye{};
        std::array<float, 3> Target{};
    };

    struct FrameReply
    {
        uint64_t Frame{ 0u };
        float SimulationTime{ 0.0f };
        bool Paused{ false };
        bool Repeatable{ false };
        int32_t ScreenWidth{ 0 };
        int32_t ScreenHeight{ 0 };
    };

    struct FrameWaitParams
    {
        int32_t Frames{ 0 };
    };

    struct FramePauseParams
    {
        bool Paused{ false };
    };

    struct FrameTimeParams
    {
        double Time{ 0.0 };
        int32_t SettleFrames{ k_DefaultSettleFrames };
    };

    struct FrameStepParams
    {
        int32_t Frames{ 0 };
        double Seconds{ 0.0 };
        int32_t SettleFrames{ k_DefaultSettleFrames };
    };

    struct FrameRepeatableParams
    {
        bool Enabled{ false };
    };

    struct CaptureScreenParams
    {
        int32_t Downsample{ 4 };
        bool HideUi{ true };
        std::string Name;
    };

    struct CaptureRegionParams
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        bool HideUi{ true };
        std::string Name;
    };

    struct CapturePixelParams
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        bool HideUi{ true };
    };

    struct CaptureImageReply
    {
        std::string Path;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t ScreenWidth{ 0 };
        int32_t ScreenHeight{ 0 };
        uint64_t Frame{ 0u };
    };

    struct DebugViewParams
    {
        std::string Surface;
        std::string Atmosphere;
        std::string Ssr;
    };

    struct DebugViewReply
    {
        std::string Surface;
        std::string Atmosphere;
        std::string Ssr;
        std::string Choices;
    };

    struct CapturePixelReply
    {
        uint64_t Frame{ 0u };
        std::array<float, 3> LinearRgb{};
        std::array<int32_t, 3> Srgb8{};
        std::string Raw;
        std::string Encoding;
    };
}

template<>
struct foundation::reflect::Describe<ocean_tools::CameraReply>
{
    using CameraReply = ocean_tools::CameraReply;

    static constexpr std::string_view Name{ "ocean.CameraReply" };
    static constexpr std::tuple Fields
    {
        Field<CameraReply, float>{ "Yaw", &CameraReply::Yaw, { .Unit = "deg" } },
        Field<CameraReply, float>{ "Pitch", &CameraReply::Pitch, { .Unit = "deg" } },
        Field<CameraReply, float>{ "Distance", &CameraReply::Distance, { .Unit = "m" } },
        Field<CameraReply, std::array<float, 3>>{ "Target", &CameraReply::Target, { .Unit = "m" } },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CameraSetParams>
{
    using CameraSetParams = ocean_tools::CameraSetParams;

    static constexpr std::string_view Name{ "ocean.CameraSetParams" };
    static constexpr std::tuple Fields
    {
        Field<CameraSetParams, float>{ "Yaw", &CameraSetParams::Yaw, { .Unit = "deg", .Required = true } },
        Field<CameraSetParams, float>
        {
            "Pitch",
            &CameraSetParams::Pitch,
            {
                .Unit = "deg",
                .Range = FieldRange{ -90.0, 90.0 },
                .Doc = "The camera stops just short of the poles, so the reply can differ near the limits.",
                .Required = true,
            },
        },
        Field<CameraSetParams, float>
        {
            "Distance",
            &CameraSetParams::Distance,
            {
                .Unit = "m",
                .Range = FieldRange{ 2.0, 20000.0 },
                .Doc = "The distance from the camera to its target.",
                .Required = true,
            },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CameraLookAtParams>
{
    using CameraLookAtParams = ocean_tools::CameraLookAtParams;

    static constexpr std::string_view Name{ "ocean.CameraLookAtParams" };
    static constexpr std::tuple Fields
    {
        Field<CameraLookAtParams, std::array<float, 3>>
        {
            "Eye",
            &CameraLookAtParams::Eye,
            { .Unit = "m", .Required = true },
        },
        Field<CameraLookAtParams, std::array<float, 3>>
        {
            "Target",
            &CameraLookAtParams::Target,
            { .Unit = "m", .Required = true },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FrameReply>
{
    using FrameReply = ocean_tools::FrameReply;

    static constexpr std::string_view Name{ "ocean.FrameReply" };
    static constexpr std::tuple Fields
    {
        Field<FrameReply, uint64_t>{ "Frame", &FrameReply::Frame },
        Field<FrameReply, float>{ "SimulationTime", &FrameReply::SimulationTime, { .Unit = "s" } },
        Field<FrameReply, bool>{ "Paused", &FrameReply::Paused },
        Field<FrameReply, bool>
        {
            "Repeatable",
            &FrameReply::Repeatable,
            { .Doc = "True while frame.repeatable is on." },
        },
        Field<FrameReply, int32_t>{ "ScreenWidth", &FrameReply::ScreenWidth, { .Unit = "px" } },
        Field<FrameReply, int32_t>{ "ScreenHeight", &FrameReply::ScreenHeight, { .Unit = "px" } },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FrameTimeParams>
{
    using FrameTimeParams = ocean_tools::FrameTimeParams;

    static constexpr std::string_view Name{ "ocean.FrameTimeParams" };
    static constexpr std::tuple Fields
    {
        Field<FrameTimeParams, double>
        {
            "Time",
            &FrameTimeParams::Time,
            {
                .Unit = "s",
                .Range = FieldRange{ 0.0, 100000.0 },
                .Doc = "The simulation time. The compute shaders get it as a 32-bit float, and the reply returns "
                       "that float.",
                .Required = true,
            },
        },
        Field<FrameTimeParams, int32_t>
        {
            "SettleFrames",
            &FrameTimeParams::SettleFrames,
            {
                .Range = FieldRange{ 0.0, 1000.0 },
                .Doc = "Frames to render at this time before the reply.",
            },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FrameStepParams>
{
    using FrameStepParams = ocean_tools::FrameStepParams;

    static constexpr std::string_view Name{ "ocean.FrameStepParams" };
    static constexpr std::tuple Fields
    {
        Field<FrameStepParams, int32_t>
        {
            "Frames",
            &FrameStepParams::Frames,
            {
                .Range = FieldRange{ 1.0, 100000.0 },
                .Doc = "The number of steps. Each step is one frame.",
                .Required = true,
            },
        },
        Field<FrameStepParams, double>
        {
            "Seconds",
            &FrameStepParams::Seconds,
            {
                .Unit = "s",
                .Range = FieldRange{ 0.0001, 10.0 },
                .Doc = "The simulation time of each step. The time scale does not apply.",
                .Required = true,
            },
        },
        Field<FrameStepParams, int32_t>
        {
            "SettleFrames",
            &FrameStepParams::SettleFrames,
            {
                .Range = FieldRange{ 0.0, 1000.0 },
                .Doc = "Frames to render at the last step's time before the reply.",
            },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FrameRepeatableParams>
{
    using FrameRepeatableParams = ocean_tools::FrameRepeatableParams;

    static constexpr std::string_view Name{ "ocean.FrameRepeatableParams" };
    static constexpr std::tuple Fields
    {
        Field<FrameRepeatableParams, bool>
        {
            "Enabled",
            &FrameRepeatableParams::Enabled,
            { .Required = true },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FramePauseParams>
{
    using FramePauseParams = ocean_tools::FramePauseParams;

    static constexpr std::string_view Name{ "ocean.FramePauseParams" };
    static constexpr std::tuple Fields
    {
        Field<FramePauseParams, bool>
        {
            "Paused",
            &FramePauseParams::Paused,
            { .Doc = "True stops the simulation and the foam. Frames still render.", .Required = true },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CaptureScreenParams>
{
    using CaptureScreenParams = ocean_tools::CaptureScreenParams;

    static constexpr std::string_view Name{ "ocean.CaptureScreenParams" };
    static constexpr std::tuple Fields
    {
        Field<CaptureScreenParams, int32_t>
        {
            "Downsample",
            &CaptureScreenParams::Downsample,
            {
                .Range = FieldRange{ 1.0, 16.0 },
                .Doc = "Averages each N x N block in linear light. 1 keeps every pixel.",
            },
        },
        Field<CaptureScreenParams, bool>
        {
            "HideUi",
            &CaptureScreenParams::HideUi,
            { .Doc = "Hides the panel on the captured frame only." },
        },
        Field<CaptureScreenParams, std::string>
        {
            "Name",
            &CaptureScreenParams::Name,
            { .Doc = "The file name without .png: letters, digits, '_' and '-'. Empty chooses a name." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CaptureRegionParams>
{
    using CaptureRegionParams = ocean_tools::CaptureRegionParams;

    static constexpr std::string_view Name{ "ocean.CaptureRegionParams" };
    static constexpr std::tuple Fields
    {
        Field<CaptureRegionParams, int32_t>
        {
            "X",
            &CaptureRegionParams::X,
            { .Unit = "px", .Doc = "The left edge. 0 is the left of the screen.", .Required = true },
        },
        Field<CaptureRegionParams, int32_t>
        {
            "Y",
            &CaptureRegionParams::Y,
            { .Unit = "px", .Doc = "The top edge. 0 is the top of the screen.", .Required = true },
        },
        Field<CaptureRegionParams, int32_t>{ "Width", &CaptureRegionParams::Width, { .Unit = "px", .Required = true } },
        Field<CaptureRegionParams, int32_t>{ "Height", &CaptureRegionParams::Height, { .Unit = "px", .Required = true } },
        Field<CaptureRegionParams, bool>
        {
            "HideUi",
            &CaptureRegionParams::HideUi,
            { .Doc = "Hides the panel on the captured frame only." },
        },
        Field<CaptureRegionParams, std::string>
        {
            "Name",
            &CaptureRegionParams::Name,
            { .Doc = "The file name without .png: letters, digits, '_' and '-'. Empty chooses a name." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CapturePixelParams>
{
    using CapturePixelParams = ocean_tools::CapturePixelParams;

    static constexpr std::string_view Name{ "ocean.CapturePixelParams" };
    static constexpr std::tuple Fields
    {
        Field<CapturePixelParams, int32_t>{ "X", &CapturePixelParams::X, { .Unit = "px", .Required = true } },
        Field<CapturePixelParams, int32_t>{ "Y", &CapturePixelParams::Y, { .Unit = "px", .Required = true } },
        Field<CapturePixelParams, bool>
        {
            "HideUi",
            &CapturePixelParams::HideUi,
            { .Doc = "Hides the panel on the captured frame only." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::DebugViewParams>
{
    using DebugViewParams = ocean_tools::DebugViewParams;

    static constexpr std::string_view Name{ "ocean.DebugViewParams" };
    static constexpr std::tuple Fields
    {
        Field<DebugViewParams, std::string>
        {
            "Surface",
            &DebugViewParams::Surface,
            { .Doc = "A surface debug mode name. Empty keeps the current mode. The reply lists every name." },
        },
        Field<DebugViewParams, std::string>
        {
            "Atmosphere",
            &DebugViewParams::Atmosphere,
            { .Doc = "An atmosphere debug mode name. Empty keeps the current mode." },
        },
        Field<DebugViewParams, std::string>
        {
            "Ssr",
            &DebugViewParams::Ssr,
            { .Doc = "A screen-space reflection debug mode name. Empty keeps the current mode." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::DebugViewReply>
{
    using DebugViewReply = ocean_tools::DebugViewReply;

    static constexpr std::string_view Name{ "ocean.DebugViewReply" };
    static constexpr std::tuple Fields
    {
        Field<DebugViewReply, std::string>{ "Surface", &DebugViewReply::Surface },
        Field<DebugViewReply, std::string>{ "Atmosphere", &DebugViewReply::Atmosphere },
        Field<DebugViewReply, std::string>{ "Ssr", &DebugViewReply::Ssr },
        Field<DebugViewReply, std::string>
        {
            "Choices",
            &DebugViewReply::Choices,
            { .Doc = "Every mode name of each view. The first name of each list turns that view off." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CaptureImageReply>
{
    using CaptureImageReply = ocean_tools::CaptureImageReply;

    static constexpr std::string_view Name{ "ocean.CaptureImageReply" };
    static constexpr std::tuple Fields
    {
        Field<CaptureImageReply, std::string>{ "Path", &CaptureImageReply::Path, { .Doc = "The absolute path of the PNG." } },
        Field<CaptureImageReply, int32_t>{ "Width", &CaptureImageReply::Width, { .Unit = "px" } },
        Field<CaptureImageReply, int32_t>{ "Height", &CaptureImageReply::Height, { .Unit = "px" } },
        Field<CaptureImageReply, int32_t>{ "ScreenWidth", &CaptureImageReply::ScreenWidth, { .Unit = "px" } },
        Field<CaptureImageReply, int32_t>{ "ScreenHeight", &CaptureImageReply::ScreenHeight, { .Unit = "px" } },
        Field<CaptureImageReply, uint64_t>{ "Frame", &CaptureImageReply::Frame },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CapturePixelReply>
{
    using CapturePixelReply = ocean_tools::CapturePixelReply;

    static constexpr std::string_view Name{ "ocean.CapturePixelReply" };
    static constexpr std::tuple Fields
    {
        Field<CapturePixelReply, uint64_t>{ "Frame", &CapturePixelReply::Frame },
        Field<CapturePixelReply, std::array<float, 3>>
        {
            "LinearRgb",
            &CapturePixelReply::LinearRgb,
            { .Doc = "Linear BT.709. 1.0 is paper white on an HDR swapchain. Not clamped." },
        },
        Field<CapturePixelReply, std::array<int32_t, 3>>
        {
            "Srgb8",
            &CapturePixelReply::Srgb8,
            { .Doc = "The 8-bit sRGB value that a PNG capture of this pixel contains." },
        },
        Field<CapturePixelReply, std::string>
        {
            "Raw",
            &CapturePixelReply::Raw,
            { .Doc = "The stored bytes of the pixel in hexadecimal, in memory order." },
        },
        Field<CapturePixelReply, std::string>
        {
            "Encoding",
            &CapturePixelReply::Encoding,
            { .Doc = "How the swapchain stores color: sRGB, LinearExtendedSRGB or HDR10_PQ." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::FrameWaitParams>
{
    using FrameWaitParams = ocean_tools::FrameWaitParams;

    static constexpr std::string_view Name{ "ocean.FrameWaitParams" };
    static constexpr std::tuple Fields
    {
        Field<FrameWaitParams, int32_t>
        {
            "Frames",
            &FrameWaitParams::Frames,
            {
                .Range = FieldRange{ 0.0, 100000.0 },
                .Doc = "The number of frames to render before the reply.",
                .Required = true,
            },
        },
    };
};

namespace
{
    constexpr float k_RadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
    constexpr std::string_view k_SeaObject{ "ocean.sea" };
    constexpr int32_t k_MaxToolRequestsPerFrame = 32;
    constexpr size_t k_MaxCaptureNameLength = 64u;

    [[nodiscard]] bool IsSafeCaptureName(std::string_view name) noexcept;
    template<size_t Count>
    [[nodiscard]] std::string JoinNames(const std::array<std::string_view, Count>& names);
    [[nodiscard]] std::string DebugViewChoices();
    [[nodiscard]] std::string_view EncodingName(capture::CaptureEncoding encoding) noexcept;

    class StdoutReplies final : public agent::ReplySink
    {
    public:
        void Finish(int64_t request_id, std::string result_json) final;
        void Fail(int64_t request_id, agent::ToolResult failure) final;
    };
}

struct OceanScene::ToolSurface
{
    struct CameraGetCommand
    {
        static constexpr std::string_view Name{ "camera.get" };
        static constexpr std::string_view Doc{ "Returns the orbit camera." };
        using Params = agent::NoParams;
        using Reply = ocean_tools::CameraReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct CameraSetCommand
    {
        static constexpr std::string_view Name{ "camera.set" };
        static constexpr std::string_view Doc{
            "Sets the orbit around the current target, and returns the camera." };
        using Params = ocean_tools::CameraSetParams;
        using Reply = ocean_tools::CameraReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct CameraLookAtCommand
    {
        static constexpr std::string_view Name{ "camera.lookat" };
        static constexpr std::string_view Doc{
            "Places the camera at Eye, looking at Target, and returns the camera." };
        using Params = ocean_tools::CameraLookAtParams;
        using Reply = ocean_tools::CameraReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct FrameInfoCommand
    {
        static constexpr std::string_view Name{ "frame.info" };
        static constexpr std::string_view Doc{ "Returns the frame number and the simulation time." };
        using Params = agent::NoParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct PendingWait
    {
        int32_t FramesLeft{ 0 };
        agent::ReplyToken<ocean_tools::FrameReply> Token;
    };

    struct FrameWaitCommand
    {
        static constexpr std::string_view Name{ "frame.wait" };
        static constexpr std::string_view Doc{
            "Replies after this many frames. Other requests still run meanwhile." };
        using Params = ocean_tools::FrameWaitParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
        std::vector<PendingWait>& Waits;
    };

    struct FramePauseCommand
    {
        static constexpr std::string_view Name{ "frame.pause" };
        static constexpr std::string_view Doc{
            "Stops or starts the simulation and the foam. Frames still render, and TAA still jitters by up to a pixel, "
            "so two captures of a paused sea differ slightly unless frame.repeatable is on." };
        using Params = ocean_tools::FramePauseParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct FrameTimeCommand
    {
        static constexpr std::string_view Name{ "frame.time" };
        static constexpr std::string_view Doc{
            "Sets the simulation time, rebuilds the foam for it, and replies after SettleFrames frames. Paused, every "
            "frame then renders this time. Running, the sea continues from it. With frame.repeatable on, a paused time "
            "gives the same image each time, whatever came before." };
        using Params = ocean_tools::FrameTimeParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
        std::vector<PendingWait>& Waits;
    };

    struct FrameStepCommand
    {
        static constexpr std::string_view Name{ "frame.step" };
        static constexpr std::string_view Doc{
            "Pauses the sea, then advances it by Seconds on each of the next Frames frames, and replies after "
            "SettleFrames more frames. N steps from a time T that frame.time set store the same float as frame.time "
            "with T + N x Seconds, computed in double. The foam advances step by step, so with foam on, a stepped frame and a "
            "frame.time frame can differ." };
        using Params = ocean_tools::FrameStepParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
        std::vector<PendingWait>& Waits;
    };

    struct FrameRepeatableCommand
    {
        static constexpr std::string_view Name{ "frame.repeatable" };
        static constexpr std::string_view Doc{
            "Makes paused frames repeatable: TAA stops (no jitter and no resolve, so edges alias), and auto exposure "
            "goes to its target in one frame. A paused time then gives the same image byte for byte, whatever came "
            "before. Use it with frame.time or frame.step for A/B captures. Off restores TAA and the adaptation." };
        using Params = ocean_tools::FrameRepeatableParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct DebugViewCommand
    {
        static constexpr std::string_view Name{ "debug.view" };
        static constexpr std::string_view Doc{
            "Switches the shader debug views of the final image, by name. Capture after a frame to see one. A wrong "
            "name changes nothing, and the error lists the names." };
        using Params = ocean_tools::DebugViewParams;
        using Reply = ocean_tools::DebugViewReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    struct AppQuitCommand
    {
        static constexpr std::string_view Name{ "app.quit" };
        static constexpr std::string_view Doc{
            "Ends the demo after this frame. Captures are written first. The reply arrives before the exit." };
        using Params = agent::NoParams;
        using Reply = ocean_tools::FrameReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        OceanScene& Scene;
    };

    using CaptureToken = std::variant<agent::ReplyToken<ocean_tools::CaptureImageReply>,
                                      agent::ReplyToken<ocean_tools::CapturePixelReply>>;

    // a capture waits here until a frame has no other capture. One frame records one capture
    struct QueuedCapture
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        // 0 for the whole screen
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t Downsample{ 1 };
        bool HideUi{ true };
        bool Pixel{ false };
        std::string Name;
        CaptureToken Token;
    };

    // a capture that the GPU copied, which waits for the writer thread
    struct CaptureInFlight
    {
        uint64_t Ticket{ 0u };
        uint64_t Frame{ 0u };
        int32_t ScreenWidth{ 0 };
        int32_t ScreenHeight{ 0 };
        capture::CaptureEncoding Encoding{ capture::CaptureEncoding::sRGB };
        CaptureToken Token;
    };

    struct CaptureScreenCommand
    {
        static constexpr std::string_view Name{ "capture.screen" };
        static constexpr std::string_view Doc{
            "Writes the whole screen to a PNG and returns its path. Replies after the file exists, a few frames later." };
        using Params = ocean_tools::CaptureScreenParams;
        using Reply = ocean_tools::CaptureImageReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        ToolSurface& Surface;
    };

    struct CaptureRegionCommand
    {
        static constexpr std::string_view Name{ "capture.region" };
        static constexpr std::string_view Doc{
            "Writes one rectangle of the screen to a PNG at full resolution and returns its path." };
        using Params = ocean_tools::CaptureRegionParams;
        using Reply = ocean_tools::CaptureImageReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        ToolSurface& Surface;
    };

    struct CapturePixelCommand
    {
        static constexpr std::string_view Name{ "capture.pixel" };
        static constexpr std::string_view Doc{
            "Returns the value of one pixel of the final image: linear, 8-bit sRGB, and the stored bytes." };
        using Params = ocean_tools::CapturePixelParams;
        using Reply = ocean_tools::CapturePixelReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        ToolSurface& Surface;
    };

    explicit ToolSurface(OceanScene& scene);

    void TickWaits();
    void TickCaptures();
    /** @brief Checks the area against the screen and the name, and queues the capture. Fails the token on a refusal */
    void QueueCapture(QueuedCapture request);
    [[nodiscard]] bool RecordCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image);
    void FinishCapture(CaptureInFlight& flight, const capture::CaptureResult& result);
    void SeaStateChanged(std::string_view field_path);

    [[nodiscard]] static ocean_tools::CameraReply ReadCamera(const OceanScene& scene) noexcept;
    [[nodiscard]] static ocean_tools::FrameReply ReadFrame(const OceanScene& scene) noexcept;

    // the replies go first, so that they outlive every pending reply that points to them
    StdoutReplies Replies;
    OceanScene& Scene;
    std::vector<PendingWait> Waits;
    std::deque<QueuedCapture> CaptureQueue;
    std::vector<CaptureInFlight> CapturesInFlight;
    int64_t NextCaptureNumber{ 1 };
    CameraGetCommand CameraGet;
    CameraSetCommand CameraSet;
    CameraLookAtCommand CameraLookAt;
    FrameInfoCommand FrameInfo;
    FrameWaitCommand FrameWait;
    FramePauseCommand FramePause;
    FrameTimeCommand FrameTime;
    FrameStepCommand FrameStep;
    FrameRepeatableCommand FrameRepeatable;
    AppQuitCommand AppQuit;
    DebugViewCommand DebugView;
    CaptureScreenCommand CaptureScreen;
    CaptureRegionCommand CaptureRegion;
    CapturePixelCommand CapturePixel;
    agent::ToolRegistry Registry;
    int64_t NextRequestId{ 1 };
};

void OceanScene::ToolSurfaceDeleter::operator()(ToolSurface* surface) const noexcept
{
    delete surface;
}

void OceanScene::createToolSurface()
{
    toolSurface.reset(new ToolSurface{ *this });
}

void OceanScene::tickToolSurface()
{
    toolSurface->TickWaits();
    toolSurface->TickCaptures();

    if (settings.Tools != nullptr)
    {
        settings.Tools->Pump(toolSurface->Registry, k_MaxToolRequestsPerFrame);
    }
}

void OceanScene::runToolLine(std::string_view line)
{
    agent::RunToolLine(toolSurface->Registry, line, toolSurface->Replies, toolSurface->NextRequestId++);
}

bool OceanScene::toolCaptureWaiting() const noexcept
{
    return !toolSurface->CaptureQueue.empty();
}

bool OceanScene::toolCaptureHidesUi() const noexcept
{
    return !toolSurface->CaptureQueue.empty() && toolSurface->CaptureQueue.front().HideUi;
}

bool OceanScene::recordToolCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image)
{
    return toolSurface->RecordCapture(command_buffer, image);
}

void OceanScene::setSeaStateField(std::string_view _name, float _value)
{
    const std::string path = std::format("{}.{}", k_SeaObject, _name);
    const agent::ToolResult written =
        toolSurface->Registry.SetValue(path, foundation::reflect::ScalarValue{ static_cast<double>(_value) });

    if (!written.Succeeded())
    {
        std::cerr << std::format("sea {} {}: {}\n", _name, _value, written.Message);
    }
}

OceanScene::ToolSurface::ToolSurface(OceanScene& scene) :
    Scene{ scene },
    CameraGet{ scene },
    CameraSet{ scene },
    CameraLookAt{ scene },
    FrameInfo{ scene },
    FrameWait{ scene, Waits },
    FramePause{ scene },
    FrameTime{ scene, Waits },
    FrameStep{ scene, Waits },
    FrameRepeatable{ scene },
    AppQuit{ scene },
    DebugView{ scene },
    CaptureScreen{ *this },
    CaptureRegion{ *this },
    CapturePixel{ *this }
{
    const agent::ObjectChanged seaChanged = agent::ObjectChanged::Create<ToolSurface,
        &ToolSurface::SeaStateChanged>(this);

    const std::array errors
    {
        Registry.AddObject(k_SeaObject, scene.seaState, seaChanged),
        Registry.AddCommand(CameraGet),
        Registry.AddCommand(CameraSet),
        Registry.AddCommand(CameraLookAt),
        Registry.AddCommand(FrameInfo),
        Registry.AddCommand(FrameWait),
        Registry.AddCommand(FramePause),
        Registry.AddCommand(FrameTime),
        Registry.AddCommand(FrameStep),
        Registry.AddCommand(FrameRepeatable),
        Registry.AddCommand(AppQuit),
        Registry.AddCommand(DebugView),
        Registry.AddCommand(CaptureScreen),
        Registry.AddCommand(CaptureRegion),
        Registry.AddCommand(CapturePixel),
    };

    for (const agent::ToolResultCode error : errors)
    {
        if (error != agent::ToolResultCode::Success)
        {
            std::cerr << std::format("tools: a registration failed: {}\n", agent::ToString(error));
        }
    }

    Registry.Freeze();
}

void OceanScene::ToolSurface::TickWaits()
{
    for (PendingWait& wait : Waits)
    {
        --wait.FramesLeft;

        if (wait.FramesLeft <= 0)
        {
            wait.Token.Complete(ReadFrame(Scene));
        }
    }

    const auto isFinished = [](const PendingWait& wait) { return wait.FramesLeft <= 0; };

    std::erase_if(Waits, isFinished);
}

void OceanScene::ToolSurface::TickCaptures()
{
    if (Scene.frameCapture == nullptr || CapturesInFlight.empty())
    {
        return;
    }

    for (const capture::CaptureResult& result : Scene.frameCapture->TakeResults())
    {
        const auto matchesTicket = [&result](const CaptureInFlight& flight) { return flight.Ticket == result.Ticket; };
        const auto found = std::ranges::find_if(CapturesInFlight, matchesTicket);

        // a script capture never reports, so every result belongs to a tool capture
        if (found == CapturesInFlight.end())
        {
            continue;
        }

        FinishCapture(*found, result);
        CapturesInFlight.erase(found);
    }
}

void OceanScene::ToolSurface::QueueCapture(QueuedCapture request)
{
    const auto failToken = [](CaptureToken& token, std::string message)
    {
        const auto fail = [&message](auto& typed) { typed.Fail(std::move(message)); };

        std::visit(fail, token);
    };

    if (Scene.frameCapture == nullptr)
    {
        failToken(request.Token, "capture is off: the swapchain images cannot be a transfer source");
        return;
    }

    const math::Float2 extent = Scene.swapchain->GetExtent();
    const int32_t screenWidth = static_cast<int32_t>(extent.x);
    const int32_t screenHeight = static_cast<int32_t>(extent.y);
    const bool wholeScreen = request.Width == 0 && request.Height == 0 && !request.Pixel;
    const bool inside = request.X >= 0 && request.Y >= 0 && request.Width > 0 && request.Height > 0 &&
                        request.X + request.Width <= screenWidth && request.Y + request.Height <= screenHeight;

    if (!wholeScreen && !inside)
    {
        failToken(request.Token, std::format("the area x={} y={} width={} height={} is not inside the {}x{} screen",
                                             request.X, request.Y, request.Width, request.Height,
                                             screenWidth, screenHeight));
        return;
    }

    if (!IsSafeCaptureName(request.Name))
    {
        failToken(request.Token, std::format("the name '{}' is refused: use up to {} letters, digits, '_' and '-'",
                                             request.Name, k_MaxCaptureNameLength));
        return;
    }

    if (request.Name.empty())
    {
        request.Name = std::format("tool_{}_{}", request.Pixel ? "pixel" : "capture", NextCaptureNumber);
        ++NextCaptureNumber;
    }

    CaptureQueue.push_back(std::move(request));
}

bool OceanScene::ToolSurface::RecordCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image)
{
    QueuedCapture request = std::move(CaptureQueue.front());

    CaptureQueue.pop_front();

    std::optional<capture::CaptureSource> source = Scene.swapchainCaptureSource(image);
    const math::Float2 extent = Scene.swapchain->GetExtent();
    const int32_t screenWidth = static_cast<int32_t>(extent.x);
    const int32_t screenHeight = static_cast<int32_t>(extent.y);
    // the window can change size between the request and this frame
    const bool fits = request.X + request.Width <= screenWidth && request.Y + request.Height <= screenHeight;

    if (!source.has_value() || !fits)
    {
        const auto fail = [](auto& typed) { typed.Fail("the screen changed, or its format cannot be captured"); };

        std::visit(fail, request.Token);
        return false;
    }

    if (request.Width > 0)
    {
        source->OffsetX = request.X;
        source->OffsetY = request.Y;
        source->Width = static_cast<uint32_t>(request.Width);
        source->Height = static_cast<uint32_t>(request.Height);
    }

    source->Downsample = static_cast<uint32_t>(request.Downsample);

    const capture::CaptureOutput output = request.Pixel ? capture::CaptureOutput::PixelValues : capture::CaptureOutput::Png;
    const std::optional<uint64_t> ticket =
        Scene.frameCapture->RecordTrackedCopy(command_buffer, Scene.currentFrame, *source, request.Name, output);

    if (!ticket.has_value())
    {
        const auto fail = [](auto& typed) { typed.Fail("the capture could not be recorded"); };

        std::visit(fail, request.Token);
        return false;
    }

    CapturesInFlight.push_back(CaptureInFlight
    {
        .Ticket = *ticket,
        .Frame = Scene.frameNumber,
        .ScreenWidth = screenWidth,
        .ScreenHeight = screenHeight,
        .Encoding = source->Encoding,
        .Token = std::move(request.Token),
    });

    return true;
}

void OceanScene::ToolSurface::FinishCapture(CaptureInFlight& flight, const capture::CaptureResult& result)
{
    if (!result.Succeeded)
    {
        const auto fail = [](auto& typed) { typed.Fail("the capture could not be read back or written"); };

        std::visit(fail, flight.Token);
        return;
    }

    if (auto* image = std::get_if<agent::ReplyToken<ocean_tools::CaptureImageReply>>(&flight.Token))
    {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(result.Path, error);

        image->Complete(ocean_tools::CaptureImageReply
        {
            .Path = (error ? result.Path : absolute).string(),
            .Width = static_cast<int32_t>(result.Width),
            .Height = static_cast<int32_t>(result.Height),
            .ScreenWidth = flight.ScreenWidth,
            .ScreenHeight = flight.ScreenHeight,
            .Frame = flight.Frame,
        });
        return;
    }

    auto& pixel = std::get<agent::ReplyToken<ocean_tools::CapturePixelReply>>(flight.Token);

    pixel.Complete(ocean_tools::CapturePixelReply
    {
        .Frame = flight.Frame,
        .LinearRgb = result.LinearRgb,
        .Srgb8 = { result.Srgb8[0], result.Srgb8[1], result.Srgb8[2] },
        .Raw = result.RawHex,
        .Encoding = std::string{ EncodingName(flight.Encoding) },
    });
}

void OceanScene::ToolSurface::SeaStateChanged(std::string_view field_path)
{
    if (field_path == "Depth")
    {
        Scene.seabedDepth = Scene.seaState.Depth;
    }

    Scene.seaStateEdited();
}

ocean_tools::CameraReply OceanScene::ToolSurface::ReadCamera(const OceanScene& scene) noexcept
{
    const ArcballCamera::State& state = scene.camera.Current;

    return ocean_tools::CameraReply
    {
        .Yaw = state.Yaw * k_RadiansToDegrees,
        .Pitch = state.Pitch * k_RadiansToDegrees,
        .Distance = state.Distance,
        .Target = { state.Target[0], state.Target[1], state.Target[2] },
    };
}

ocean_tools::FrameReply OceanScene::ToolSurface::ReadFrame(const OceanScene& scene) noexcept
{
    const math::Float2 extent = scene.swapchain->GetExtent();

    return ocean_tools::FrameReply
    {
        .Frame = scene.frameNumber,
        .SimulationTime = scene.simulationTime,
        .Paused = scene.paused,
        .Repeatable = scene.repeatableFrames,
        .ScreenWidth = static_cast<int32_t>(extent.x),
        .ScreenHeight = static_cast<int32_t>(extent.y),
    };
}

void OceanScene::ToolSurface::CameraGetCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    static_cast<void>(params);
    token.Complete(ReadCamera(Scene));
}

void OceanScene::ToolSurface::CameraSetCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    SceneCommand command{};

    command.Type = SceneCommand::Kind::Camera;
    command.Values = { params.Yaw, params.Pitch, params.Distance };
    command.ValueCount = 3u;
    Scene.runCommand(command);
    token.Complete(ReadCamera(Scene));
}

void OceanScene::ToolSurface::CameraLookAtCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    SceneCommand command{};

    command.Type = SceneCommand::Kind::LookAt;
    command.Values = { params.Eye[0], params.Eye[1], params.Eye[2], params.Target[0], params.Target[1],
        params.Target[2] };
    command.ValueCount = 6u;
    Scene.runCommand(command);
    token.Complete(ReadCamera(Scene));
}

void OceanScene::ToolSurface::FrameInfoCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    static_cast<void>(params);
    token.Complete(ReadFrame(Scene));
}

void OceanScene::ToolSurface::FrameWaitCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    if (params.Frames == 0)
    {
        token.Complete(ReadFrame(Scene));
        return;
    }

    Waits.push_back(PendingWait{ .FramesLeft = params.Frames, .Token = std::move(token) });
}

void OceanScene::ToolSurface::FramePauseCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Scene.paused = params.Paused;
    token.Complete(ReadFrame(Scene));
}

void OceanScene::ToolSurface::FrameTimeCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    // a step in progress would move the time on from here
    Scene.stepFramesLeft = 0;
    Scene.commandedTime = params.Time;
    Scene.simulationTime = static_cast<float>(params.Time);
    // the foam is history: a replay rebuilds it for the new time, as if the sea had run up to it
    Scene.foamReplayPending = true;

    if (params.SettleFrames == 0)
    {
        token.Complete(ReadFrame(Scene));
        return;
    }

    Waits.push_back(PendingWait{ .FramesLeft = params.SettleFrames, .Token = std::move(token) });
}

void OceanScene::ToolSurface::FrameStepCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Scene.paused = true;
    Scene.stepFramesLeft = params.Frames;
    Scene.stepIndex = 0;
    Scene.stepSeconds = params.Seconds;

    // a step from a time that a tool set adds to that double, so that N steps from T store the same float as
    // frame.time with T + N x Seconds. After the sea ran, the float is the only time there is
    const bool timeIsCommanded = static_cast<float>(Scene.commandedTime) == Scene.simulationTime;

    Scene.stepBaseTime = timeIsCommanded ? Scene.commandedTime : static_cast<double>(Scene.simulationTime);

    // update() takes the first step later in this frame, so the last step is in the frame before the wait ends
    Waits.push_back(PendingWait{ .FramesLeft = params.Frames + params.SettleFrames, .Token = std::move(token) });
}

void OceanScene::ToolSurface::FrameRepeatableCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Scene.repeatableFrames = params.Enabled;
    token.Complete(ReadFrame(Scene));
}

void OceanScene::ToolSurface::DebugViewCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    const int32_t surface = params.Surface.empty() ? Scene.surfaceDebugMode : FindSurfaceDebugMode(params.Surface);
    const int32_t atmosphere = params.Atmosphere.empty() ? Scene.atmosphereDebugMode : FindAtmosphereDebugMode(params.Atmosphere);
    const int32_t ssr = params.Ssr.empty() ? Scene.ssrDebugMode : FindSsrDebugMode(params.Ssr);

    // every name is checked before any mode changes, so a refusal changes nothing
    if (surface < 0 || atmosphere < 0 || ssr < 0)
    {
        token.Fail(std::format("a debug view name is unknown. {}", DebugViewChoices()));
        return;
    }

    Scene.surfaceDebugMode = surface;
    Scene.atmosphereDebugMode = atmosphere;
    Scene.ssrDebugMode = ssr;

    const std::array<std::string_view, 11>& surfaceNames = SurfaceDebugModeNames();
    const std::array<std::string_view, 10>& atmosphereNames = AtmosphereDebugModeNames();
    const std::array<std::string_view, 6>& ssrNames = SsrDebugModeNames();

    token.Complete(ocean_tools::DebugViewReply
    {
        .Surface = std::string{ surfaceNames[static_cast<size_t>(surface)] },
        .Atmosphere = std::string{ atmosphereNames[static_cast<size_t>(atmosphere)] },
        .Ssr = std::string{ ssrNames[static_cast<size_t>(ssr)] },
        .Choices = DebugViewChoices(),
    });
}

void OceanScene::ToolSurface::AppQuitCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    static_cast<void>(params);
    Scene.quitRequested = true;
    token.Complete(ReadFrame(Scene));
}

void OceanScene::ToolSurface::CaptureScreenCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Surface.QueueCapture(QueuedCapture
    {
        .Downsample = params.Downsample,
        .HideUi = params.HideUi,
        .Name = params.Name,
        .Token = std::move(token),
    });
}

void OceanScene::ToolSurface::CaptureRegionCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Surface.QueueCapture(QueuedCapture
    {
        .X = params.X,
        .Y = params.Y,
        .Width = params.Width,
        .Height = params.Height,
        .HideUi = params.HideUi,
        .Name = params.Name,
        .Token = std::move(token),
    });
}

void OceanScene::ToolSurface::CapturePixelCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    Surface.QueueCapture(QueuedCapture
    {
        .X = params.X,
        .Y = params.Y,
        .Width = 1,
        .Height = 1,
        .HideUi = params.HideUi,
        .Pixel = true,
        .Token = std::move(token),
    });
}

namespace
{
    void StdoutReplies::Finish(int64_t request_id, std::string result_json)
    {
        std::cout << std::format("tool {} ok {}", request_id, result_json) << std::endl;
    }

    void StdoutReplies::Fail(int64_t request_id, agent::ToolResult failure)
    {
        std::cout << std::format("tool {} error {}", request_id, agent::ResultToJson(failure)) << std::endl;
    }

    bool IsSafeCaptureName(std::string_view name) noexcept
    {
        const auto isAllowed = [](char letter)
        {
            const bool alphanumeric = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                                      (letter >= '0' && letter <= '9');

            return alphanumeric || letter == '_' || letter == '-';
        };

        return name.size() <= k_MaxCaptureNameLength && std::ranges::all_of(name, isAllowed);
    }

    template<size_t Count>
    std::string JoinNames(const std::array<std::string_view, Count>& names)
    {
        std::string joined;

        for (const std::string_view name : names)
        {
            joined += joined.empty() ? std::string{ name } : std::format(", {}", name);
        }

        return joined;
    }

    std::string DebugViewChoices()
    {
        return std::format("Surface: {}. Atmosphere: {}. Ssr: {}.", JoinNames(SurfaceDebugModeNames()),
                           JoinNames(AtmosphereDebugModeNames()), JoinNames(SsrDebugModeNames()));
    }

    std::string_view EncodingName(capture::CaptureEncoding encoding) noexcept
    {
        switch (encoding)
        {
        case capture::CaptureEncoding::sRGB: return "sRGB";
        case capture::CaptureEncoding::LinearExtendedSRGB: return "LinearExtendedSRGB";
        case capture::CaptureEncoding::HDR10_PQ: return "HDR10_PQ";
        }

        return "Invalid";
    }
}
