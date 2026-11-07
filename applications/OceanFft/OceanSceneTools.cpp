#include "OceanScene.hpp"
#include "SceneCommands.hpp"
#include "SeaState.reflect.hpp"
#include "ToolLine.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "ToolServer.hpp"
#include "reflect/Describe.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace ocean_tools
{
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
    };

    struct FrameWaitParams
    {
        int32_t Frames{ 0 };
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

    explicit ToolSurface(OceanScene& scene);

    void TickWaits();
    void SeaStateChanged(std::string_view field_path);

    [[nodiscard]] static ocean_tools::CameraReply ReadCamera(const OceanScene& scene) noexcept;
    [[nodiscard]] static ocean_tools::FrameReply ReadFrame(const OceanScene& scene) noexcept;

    // the replies go first, so that they outlive every pending reply that points to them
    StdoutReplies Replies;
    OceanScene& Scene;
    std::vector<PendingWait> Waits;
    CameraGetCommand CameraGet;
    CameraSetCommand CameraSet;
    CameraLookAtCommand CameraLookAt;
    FrameInfoCommand FrameInfo;
    FrameWaitCommand FrameWait;
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

    if (settings.Tools != nullptr)
    {
        settings.Tools->Pump(toolSurface->Registry, k_MaxToolRequestsPerFrame);
    }
}

void OceanScene::runToolLine(std::string_view line)
{
    agent::RunToolLine(toolSurface->Registry, line, toolSurface->Replies, toolSurface->NextRequestId++);
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
    FrameWait{ scene, Waits }
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
    return ocean_tools::FrameReply{ .Frame = scene.frameNumber, .SimulationTime = scene.simulationTime };
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
}
