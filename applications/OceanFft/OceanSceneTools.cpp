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

    struct CaptureTargetParams
    {
        std::string Target;
        std::string Name;
    };

    struct CaptureTargetReply
    {
        std::string Path;
        std::string Target;
        std::string Dtype;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t Channels{ 0 };
        std::string ChannelNames;
        uint64_t Bytes{ 0u };
        std::array<float, 4> ShowScale{};
        std::array<float, 4> ShowBias{};
        std::string Doc;
        uint64_t Frame{ 0u };
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
struct foundation::reflect::Describe<ocean_tools::CaptureTargetParams>
{
    using CaptureTargetParams = ocean_tools::CaptureTargetParams;

    static constexpr std::string_view Name{ "ocean.CaptureTargetParams" };
    static constexpr std::tuple Fields
    {
        Field<CaptureTargetParams, std::string>
        {
            "Target",
            &CaptureTargetParams::Target,
            { .Doc = "The name of the target. A wrong name fails, and the error lists the names.", .Required = true },
        },
        Field<CaptureTargetParams, std::string>
        {
            "Name",
            &CaptureTargetParams::Name,
            { .Doc = "The file name without extension: letters, digits, '_' and '-'. Empty picks a name." },
        },
    };
};

template<>
struct foundation::reflect::Describe<ocean_tools::CaptureTargetReply>
{
    using CaptureTargetReply = ocean_tools::CaptureTargetReply;

    static constexpr std::string_view Name{ "ocean.CaptureTargetReply" };
    static constexpr std::tuple Fields
    {
        Field<CaptureTargetReply, std::string>
        {
            "Path",
            &CaptureTargetReply::Path,
            { .Doc = "The absolute path of the .bin file: the stored values, rows top first, channels interleaved." },
        },
        Field<CaptureTargetReply, std::string>{ "Target", &CaptureTargetReply::Target },
        Field<CaptureTargetReply, std::string>
        {
            "Dtype",
            &CaptureTargetReply::Dtype,
            { .Doc = "The type of one channel, as numpy names it: float16 or float32." },
        },
        Field<CaptureTargetReply, int32_t>{ "Width", &CaptureTargetReply::Width, { .Unit = "px" } },
        Field<CaptureTargetReply, int32_t>{ "Height", &CaptureTargetReply::Height, { .Unit = "px" } },
        Field<CaptureTargetReply, int32_t>{ "Channels", &CaptureTargetReply::Channels },
        Field<CaptureTargetReply, std::string>
        {
            "ChannelNames",
            &CaptureTargetReply::ChannelNames,
            { .Doc = "What each channel holds, in order, separated by commas." },
        },
        Field<CaptureTargetReply, uint64_t>{ "Bytes", &CaptureTargetReply::Bytes },
        Field<CaptureTargetReply, std::array<float, 4>>
        {
            "ShowScale",
            &CaptureTargetReply::ShowScale,
            { .Doc = "A picture of channel c shows clamp(value x ShowScale[c] + ShowBias[c], 0, 1)." },
        },
        Field<CaptureTargetReply, std::array<float, 4>>{ "ShowBias", &CaptureTargetReply::ShowBias },
        Field<CaptureTargetReply, std::string>{ "Doc", &CaptureTargetReply::Doc },
        Field<CaptureTargetReply, uint64_t>{ "Frame", &CaptureTargetReply::Frame },
    };
};

template<>
struct foundation::reflect::Describe<ExposureSettings>
{
    static constexpr std::string_view Name{ "ocean.ExposureSettings" };
    static constexpr std::string_view Doc{
        "The exposure of the final image. The composite reads it at the end of each frame." };

    static constexpr std::tuple Fields
    {
        Field<ExposureSettings, bool>
        {
            "Auto",
            &ExposureSettings::Auto,
            {
                .Doc =
                    "On: a GPU pass meters the scene each frame (the mean of log2 luminance maps to 18% gray), "
                    "and Ev adds to that. Off: Ev is the exposure.",
            },
        },
        Field<ExposureSettings, float>
        {
            "Ev",
            &ExposureSettings::Ev,
            {
                .Unit = "EV",
                .Range = FieldRange{ -16.0, 16.0 },
                .Doc = "Stops. The compensation with Auto on, the exposure with Auto off.",
            },
        },
        Field<ExposureSettings, float>
        {
            "Speed",
            &ExposureSettings::Speed,
            {
                .Unit = "1/s",
                .Range = FieldRange{ 0.01, 100.0 },
                .Doc = "How fast the auto exposure follows the scene. frame.repeatable makes it instant.",
            },
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
    constexpr std::string_view k_ExposureObject{ "ocean.exposure" };
    constexpr int32_t k_MaxToolRequestsPerFrame = 32;
    constexpr size_t k_MaxCaptureNameLength = 64u;

    enum class TargetId : uint8_t
    {
        Scene,
        HdrLighting,
        NormalSigma,
        NormalSigmaFiltered,
        FoamSss,
        MotionFoam,
        SeabedColor,
        OceanDepth,
        SeabedDepth,
        HiZ,
        ExposureState,
        ExposureEv,
    };

    // a target that capture.target copies. OceanScene::ToolSurface::SourceOf states where each one is at the end of
    // the frame, and the channels must match the shader that writes it
    struct CaptureTarget
    {
        std::string_view Name;
        TargetId Id;
        std::string_view Dtype;
        int32_t Channels;
        std::string_view ChannelNames;
        std::array<float, 4> ShowScale;
        std::array<float, 4> ShowBias;
        std::string_view Doc;
    };

    constexpr std::array k_CaptureTargets
    {
        CaptureTarget
        {
            "scene", TargetId::Scene, "float16", 4, "r,g,b,a",
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "The HDR scene that the composite exposes and tonemaps: scene-linear AP1, after the TAA resolve. The auto "
            "exposure meters this image.",
        },
        CaptureTarget
        {
            "hdr_lighting", TargetId::HdrLighting, "float16", 4, "r,g,b,a",
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "The deferred lighting output before the TAA resolve: scene-linear AP1.",
        },
        CaptureTarget
        {
            "normal_sigma", TargetId::NormalSigma, "float16", 4, "normal.x,normal.z,sigma2.x,sigma2.y",
            { 0.5f, 0.5f, 10.0f, 0.0f }, { 0.5f, 0.5f, 0.0f, 0.0f },
            "The surface G-buffer: the world normal's x and z, and the slope variance (LEADR) in x and z. Zero where "
            "no water was drawn.",
        },
        CaptureTarget
        {
            "normal_sigma_filtered", TargetId::NormalSigmaFiltered, "float16", 4, "normal.x,normal.z,sigma2.x,sigma2.y",
            { 0.5f, 0.5f, 10.0f, 0.0f }, { 0.5f, 0.5f, 0.0f, 0.0f },
            "normal_sigma after the specular anti-aliasing pass widens the slope variance. The lighting reads this one.",
        },
        CaptureTarget
        {
            "foam_sss", TargetId::FoamSss, "float16", 4, "whitecaps,underwater_foam,roughness,foam_luminance",
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "Foam coverage, foam below the surface, the material roughness, and the foam texture's luminance. A "
            "G-buffer debug view writes its colors here instead.",
        },
        CaptureTarget
        {
            "motion_foam", TargetId::MotionFoam, "float16", 4, "motion.u,motion.v,foam_normal.x,foam_normal.y",
            { 50.0f, 50.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f, 0.0f },
            "The screen motion since the last frame in UV units, without jitter, and the foam normal map's x and y.",
        },
        CaptureTarget
        {
            "seabed_color", TargetId::SeabedColor, "float16", 4, "r,g,b,a",
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "The lit seabed (and the sky background) before the water: scene-linear AP1.",
        },
        CaptureTarget
        {
            "ocean_depth", TargetId::OceanDepth, "float32", 1, "depth",
            { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "The water surface depth buffer. Reverse Z: 1 is the near plane, 0 is far or no water.",
        },
        CaptureTarget
        {
            "seabed_depth", TargetId::SeabedDepth, "float32", 1, "depth",
            { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "The seabed depth buffer. Reverse Z: 1 is the near plane, 0 is far.",
        },
        CaptureTarget
        {
            "hiz", TargetId::HiZ, "float32", 1, "depth",
            { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "Half size: each texel is the nearest (largest, reverse Z) of a 2x2 block of ocean_depth, with the block "
            "clamped at the right and bottom edges. No pass reads it yet.",
        },
        CaptureTarget
        {
            "exposure_state", TargetId::ExposureState, "float32", 4,
            "adapted_luminance,target_ev100,adapted_ev100,mean_log2_luminance",
            { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "One element: the auto exposure state. The mean is over every scene texel of log2 of the AP1 luminance, "
            "each clamped to [-8, 4]. EV100 is log2(luminance x 8). Not written while ocean.exposure.Auto is off.",
        },
        CaptureTarget
        {
            "exposure_ev", TargetId::ExposureEv, "float32", 1, "exposure_ev",
            { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
            "One element: the exposure in stops that the composite used in this frame. With Auto on, the auto exposure "
            "pass wrote log2(0.18 / adapted_luminance) + ocean.exposure.Ev. With Auto off, it is ocean.exposure.Ev.",
        },
    };

    [[nodiscard]] std::optional<size_t> FindCaptureTarget(std::string_view name) noexcept;
    [[nodiscard]] std::string CaptureTargetChoices();
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
                                      agent::ReplyToken<ocean_tools::CapturePixelReply>,
                                      agent::ReplyToken<ocean_tools::CaptureTargetReply>>;
    using TargetSource = std::variant<capture::CaptureSource, capture::BufferCaptureSource>;

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
        // an index into k_CaptureTargets, or -1 for the screen
        int32_t Target{ -1 };
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
        int32_t Target{ -1 };
        // the size of a target, which FrameCapture does not know for a buffer
        int32_t TargetWidth{ 0 };
        int32_t TargetHeight{ 0 };
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

    struct CaptureTargetCommand
    {
        static constexpr std::string_view Name{ "capture.target" };
        static constexpr std::string_view Doc{
            "Copies one intermediate image or buffer of the frame to a .bin file, unchanged, and returns its path, "
            "type, shape and channel names. The copy runs at the end of a frame, after the composite. Use it for "
            "values that a PNG cannot hold: normals, depth, HDR color, the exposure. A wrong name fails, and the error "
            "lists every target with what it holds." };
        using Params = ocean_tools::CaptureTargetParams;
        using Reply = ocean_tools::CaptureTargetReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        ToolSurface& Surface;
    };

    explicit ToolSurface(OceanScene& scene);

    void TickWaits();
    void TickCaptures();
    /** @brief Checks the area against the screen and the name, and queues the capture. Fails the token on a refusal */
    void QueueCapture(QueuedCapture request);
    [[nodiscard]] bool RecordCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image);
    void RecordTargetCapture(rhi::CommandBuffer& command_buffer, QueuedCapture request);
    [[nodiscard]] TargetSource SourceOf(TargetId id) const;
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
    CaptureTargetCommand CaptureTargetCmd;
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
    CapturePixel{ *this },
    CaptureTargetCmd{ *this }
{
    const agent::ObjectChanged seaChanged = agent::ObjectChanged::Create<ToolSurface,
        &ToolSurface::SeaStateChanged>(this);

    const std::array errors
    {
        Registry.AddObject(k_SeaObject, scene.seaState, seaChanged),
        Registry.AddObject(k_ExposureObject, scene.exposure),
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
        Registry.AddCommand(CaptureTargetCmd),
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
        const std::string_view kind = request.Target >= 0 ? k_CaptureTargets[static_cast<size_t>(request.Target)].Name
                                                           : (request.Pixel ? "pixel" : "capture");

        request.Name = std::format("tool_{}_{}", kind, NextCaptureNumber);
        ++NextCaptureNumber;
    }

    CaptureQueue.push_back(std::move(request));
}

bool OceanScene::ToolSurface::RecordCapture(rhi::CommandBuffer& command_buffer, rhi::ImageHandle image)
{
    QueuedCapture request = std::move(CaptureQueue.front());

    CaptureQueue.pop_front();

    // a target copy leaves the swapchain image to the caller, which moves it to present
    if (request.Target >= 0)
    {
        RecordTargetCapture(command_buffer, std::move(request));
        return false;
    }

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

void OceanScene::ToolSurface::RecordTargetCapture(rhi::CommandBuffer& command_buffer, QueuedCapture request)
{
    const CaptureTarget& entry = k_CaptureTargets[static_cast<size_t>(request.Target)];
    const TargetSource source = SourceOf(entry.Id);
    const capture::CaptureSource* image = std::get_if<capture::CaptureSource>(&source);
    std::optional<uint64_t> ticket;
    int32_t width = 1;
    int32_t height = 1;

    if (image != nullptr)
    {
        ticket = Scene.frameCapture->RecordTrackedCopy(command_buffer, Scene.currentFrame, *image, request.Name,
                                                       capture::CaptureOutput::Raw);
        width = static_cast<int32_t>(image->Width);
        height = static_cast<int32_t>(image->Height);
    }
    else
    {
        const auto& buffer = std::get<capture::BufferCaptureSource>(source);

        ticket = Scene.frameCapture->RecordTrackedBufferCopy(command_buffer, Scene.currentFrame, buffer, request.Name);
    }

    if (!ticket.has_value())
    {
        const auto fail = [](auto& typed) { typed.Fail("the target could not be copied"); };

        std::visit(fail, request.Token);
        return;
    }

    CapturesInFlight.push_back(CaptureInFlight
    {
        .Ticket = *ticket,
        .Frame = Scene.frameNumber,
        .Target = request.Target,
        .TargetWidth = width,
        .TargetHeight = height,
        .Token = std::move(request.Token),
    });
}

OceanScene::ToolSurface::TargetSource OceanScene::ToolSurface::SourceOf(TargetId id) const
{
    const math::Float2 extent = Scene.swapchain->GetExtent();
    capture::CaptureSource source{};

    // at the end of the frame, a compute pass was the last to read each G-buffer target, in ShaderReadOnly. The next
    // frame's barriers start from these images, so they wait for the copy
    source.Width = static_cast<uint32_t>(extent.x);
    source.Height = static_cast<uint32_t>(extent.y);
    source.Format = CommonFormats::RGBA16_Float;
    source.Encoding = capture::CaptureEncoding::LinearExtendedSRGB;
    source.CurrentLayout = rhi::ImageLayout::ShaderReadOnly;
    source.SrcStages = rhi::PipelineStage::ComputeShader;
    source.SrcAccess = rhi::Access::None;
    source.FinalLayout = rhi::ImageLayout::ShaderReadOnly;
    source.DstStages = rhi::PipelineStage::AllCommands;
    source.DstAccess = rhi::Access::MemoryRead | rhi::Access::MemoryWrite;

    switch (id)
    {
    case TargetId::Scene:
        // the composite's last barrier returns the scene target to its attachment layout
        source.Image = rhi::ImageHandle{ Scene.colorPipe->SceneResource().VkHandle };
        source.CurrentLayout = rhi::ImageLayout::Attachment;
        source.SrcStages = rhi::PipelineStage::ColorAttachmentOutput;
        source.FinalLayout = rhi::ImageLayout::Attachment;
        break;
    case TargetId::HdrLighting:
        source.Image = rhi::ImageHandle{ Scene.hdrLightingImage.VkHandle };
        break;
    case TargetId::NormalSigma:
        source.Image = rhi::ImageHandle{ Scene.gbufferNormalSigma.VkHandle };
        break;
    case TargetId::NormalSigmaFiltered:
        source.Image = rhi::ImageHandle{ Scene.gbufferNormalSigmaFiltered.VkHandle };
        break;
    case TargetId::FoamSss:
        source.Image = rhi::ImageHandle{ Scene.gbufferFoamSss.VkHandle };
        break;
    case TargetId::MotionFoam:
        source.Image = rhi::ImageHandle{ Scene.gbufferMotionFoam.VkHandle };
        break;
    case TargetId::SeabedColor:
        source.Image = rhi::ImageHandle{ Scene.seabedColorImage.VkHandle };
        break;
    case TargetId::OceanDepth:
        source.Image = rhi::ImageHandle{ Scene.oceanDepthImage.VkHandle };
        source.Format = CommonFormats::Depth32;
        source.Aspect = rhi::ImageAspect::Depth;
        break;
    case TargetId::SeabedDepth:
        source.Image = rhi::ImageHandle{ Scene.seabedDepthImage.VkHandle };
        source.Format = CommonFormats::Depth32;
        source.Aspect = rhi::ImageAspect::Depth;
        break;
    case TargetId::HiZ:
        // written in General by its compute pass, and read by nothing
        source.Image = rhi::ImageHandle{ Scene.hiZImage.VkHandle };
        source.Width = (source.Width + 1u) / 2u;
        source.Height = (source.Height + 1u) / 2u;
        source.Format = CommonFormats::R32_Float;
        source.CurrentLayout = rhi::ImageLayout::General;
        source.SrcAccess = rhi::Access::ShaderStorageWrite;
        source.FinalLayout = rhi::ImageLayout::General;
        break;
    case TargetId::ExposureState:
        return capture::BufferCaptureSource
        {
            .Buffer = rhi::BufferHandle{ Scene.exposureStateBuffer.VkHandle },
            .Offset = 0u,
            .Bytes = sizeof(float) * 4u,
            .SrcStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllTransfer,
            .SrcAccess = rhi::Access::ShaderStorageWrite | rhi::Access::TransferWrite,
            .DstStages = rhi::PipelineStage::AllCommands,
        };
    case TargetId::ExposureEv:
        // the auto exposure pass or the color pipe's fill wrote it, and the composite read it last
        return capture::BufferCaptureSource
        {
            .Buffer = rhi::BufferHandle{ Scene.colorPipe->ExposureResource().VkHandle },
            .Offset = 0u,
            .Bytes = sizeof(float),
            .SrcStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllTransfer | rhi::PipelineStage::FragmentShader,
            .SrcAccess = rhi::Access::ShaderStorageWrite | rhi::Access::TransferWrite,
            .DstStages = rhi::PipelineStage::AllCommands,
        };
    }

    return source;
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

    if (auto* target = std::get_if<agent::ReplyToken<ocean_tools::CaptureTargetReply>>(&flight.Token))
    {
        const CaptureTarget& entry = k_CaptureTargets[static_cast<size_t>(flight.Target)];
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(result.Path, error);

        target->Complete(ocean_tools::CaptureTargetReply
        {
            .Path = (error ? result.Path : absolute).string(),
            .Target = std::string{ entry.Name },
            .Dtype = std::string{ entry.Dtype },
            .Width = flight.TargetWidth,
            .Height = flight.TargetHeight,
            .Channels = entry.Channels,
            .ChannelNames = std::string{ entry.ChannelNames },
            .Bytes = result.Bytes,
            .ShowScale = entry.ShowScale,
            .ShowBias = entry.ShowBias,
            .Doc = std::string{ entry.Doc },
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

void OceanScene::ToolSurface::CaptureTargetCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
{
    const std::optional<size_t> index = FindCaptureTarget(params.Target);

    if (!index.has_value())
    {
        token.Fail(std::format("the target '{}' is unknown. {}", params.Target, CaptureTargetChoices()));
        return;
    }

    Surface.QueueCapture(QueuedCapture
    {
        .HideUi = false,
        .Target = static_cast<int32_t>(*index),
        .Name = params.Name,
        .Token = std::move(token),
    });
}

namespace
{
    std::optional<size_t> FindCaptureTarget(std::string_view name) noexcept
    {
        for (size_t i = 0; i < k_CaptureTargets.size(); ++i)
        {
            if (k_CaptureTargets[i].Name == name)
            {
                return i;
            }
        }

        return std::nullopt;
    }

    std::string CaptureTargetChoices()
    {
        std::string choices{ "Targets:" };

        for (const CaptureTarget& target : k_CaptureTargets)
        {
            choices += std::format(" {} ({}): {}", target.Name, target.ChannelNames, target.Doc);
        }

        return choices;
    }

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
