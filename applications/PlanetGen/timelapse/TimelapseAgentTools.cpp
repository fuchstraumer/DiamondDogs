#include "TimelapseAgentTools.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "ToolServer.hpp"
#include "reflect/Describe.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

namespace planet_tools
{
    struct StepParams
    {
        int32_t Steps{ 1 };
        float Dt{ 0.0f };
    };

    struct SimStateReply
    {
        int32_t Step{ 0 };
        float Time{ 0.0f };
    };

    struct CaptureParams
    {
        int32_t Downsample{ 4 };
        int32_t Mode{ -1 };
        std::string Name{ "map" };
    };

    struct CaptureRegionParams
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        int32_t Width{ 256 };
        int32_t Height{ 256 };
        int32_t Mode{ -1 };
        std::string Name{ "region" };
    };

    struct CaptureReply
    {
        std::string Path;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
    };
}

template<>
struct foundation::reflect::Describe<planet_tools::SimState>
{
    using SimState = planet_tools::SimState;

    static constexpr std::string_view Name{ "planet.SimState" };
    static constexpr std::string_view Doc{ "Live tectonic simulation state and map render settings." };
    static constexpr std::tuple Fields
    {
        Field<SimState, float>
        {
            "TimeStepMy",
            &SimState::TimeStepMy,
            { .Unit = "My", .Range = FieldRange{ 1.0e-6, k_NoLimit }, .Doc = "Simulation step size in million years." },
        },
        Field<SimState, int32_t>
        {
            "RenderMode",
            &SimState::RenderMode,
            { .Range = FieldRange{ 0.0, 5.0 }, .Doc = "0=elevation, 1=plates, 2=age, 3=relief, 4=fronts, 5=voronoi." },
        },
        Field<SimState, float>
        {
            "Time",
            &SimState::Time,
            { .Unit = "My", .Doc = "Current simulation time.", .ReadOnly = true },
        },
        Field<SimState, int32_t>
        {
            "Step",
            &SimState::Step,
            { .Doc = "Completed live steps since startup or reset.", .ReadOnly = true },
        },
        Field<SimState, int32_t>
        {
            "Width",
            &SimState::Width,
            { .Unit = "px", .Doc = "Map output width.", .ReadOnly = true },
        },
        Field<SimState, int32_t>
        {
            "Height",
            &SimState::Height,
            { .Unit = "px", .Doc = "Map output height.", .ReadOnly = true },
        },
    };
};

template<>
struct foundation::reflect::Describe<planet_tools::StepParams>
{
    using StepParams = planet_tools::StepParams;

    static constexpr std::string_view Name{ "planet.StepParams" };
    static constexpr std::tuple Fields
    {
        Field<StepParams, int32_t>
        {
            "Steps",
            &StepParams::Steps,
            { .Range = FieldRange{ 0.0, 100000.0 }, .Doc = "Number of live steps to execute." },
        },
        Field<StepParams, float>
        {
            "Dt",
            &StepParams::Dt,
            { .Unit = "My", .Range = FieldRange{ 0.0, k_NoLimit }, .Doc = "Optional new TimeStepMy when > 0." },
        },
    };
};

template<>
struct foundation::reflect::Describe<planet_tools::SimStateReply>
{
    using SimStateReply = planet_tools::SimStateReply;

    static constexpr std::string_view Name{ "planet.SimStateReply" };
    static constexpr std::tuple Fields
    {
        Field<SimStateReply, int32_t>{ "Step", &SimStateReply::Step },
        Field<SimStateReply, float>{ "Time", &SimStateReply::Time, { .Unit = "My" } },
    };
};

template<>
struct foundation::reflect::Describe<planet_tools::CaptureParams>
{
    using CaptureParams = planet_tools::CaptureParams;

    static constexpr std::string_view Name{ "planet.CaptureParams" };
    static constexpr std::tuple Fields
    {
        Field<CaptureParams, int32_t>
        {
            "Downsample",
            &CaptureParams::Downsample,
            { .Range = FieldRange{ 1.0, 64.0 }, .Doc = "Block downsample factor (>= 1)." },
        },
        Field<CaptureParams, int32_t>
        {
            "Mode",
            &CaptureParams::Mode,
            { .Range = FieldRange{ -1.0, 5.0 }, .Doc = "Optional temporary render mode (0..5), or -1 for current." },
        },
        Field<CaptureParams, std::string>
        {
            "Name",
            &CaptureParams::Name,
            { .Doc = "Output PNG stem (letters, digits, '_' and '-')." },
        },
    };
};

template<>
struct foundation::reflect::Describe<planet_tools::CaptureRegionParams>
{
    using CaptureRegionParams = planet_tools::CaptureRegionParams;

    static constexpr std::string_view Name{ "planet.CaptureRegionParams" };
    static constexpr std::tuple Fields
    {
        Field<CaptureRegionParams, int32_t>{ "X", &CaptureRegionParams::X, { .Unit = "px" } },
        Field<CaptureRegionParams, int32_t>{ "Y", &CaptureRegionParams::Y, { .Unit = "px" } },
        Field<CaptureRegionParams, int32_t>{ "Width", &CaptureRegionParams::Width, { .Unit = "px" } },
        Field<CaptureRegionParams, int32_t>{ "Height", &CaptureRegionParams::Height, { .Unit = "px" } },
        Field<CaptureRegionParams, int32_t>
        {
            "Mode",
            &CaptureRegionParams::Mode,
            { .Range = FieldRange{ -1.0, 5.0 }, .Doc = "Optional temporary render mode (0..5), or -1 for current." },
        },
        Field<CaptureRegionParams, std::string>
        {
            "Name",
            &CaptureRegionParams::Name,
            { .Doc = "Output PNG stem (letters, digits, '_' and '-')." },
        },
    };
};

template<>
struct foundation::reflect::Describe<planet_tools::CaptureReply>
{
    using CaptureReply = planet_tools::CaptureReply;

    static constexpr std::string_view Name{ "planet.CaptureReply" };
    static constexpr std::tuple Fields
    {
        Field<CaptureReply, std::string>{ "Path", &CaptureReply::Path },
        Field<CaptureReply, int32_t>{ "Width", &CaptureReply::Width, { .Unit = "px" } },
        Field<CaptureReply, int32_t>{ "Height", &CaptureReply::Height, { .Unit = "px" } },
    };
};

namespace
{
    constexpr size_t k_MaxCaptureNameLength = 64u;
    constexpr int32_t k_MaxToolRequestsPerPump = 16;

    [[nodiscard]] bool IsSafeCaptureName(std::string_view name) noexcept
    {
        if (name.size() > k_MaxCaptureNameLength)
        {
            return false;
        }

        for (const char ch : name)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (!std::isalnum(uch) && ch != '_' && ch != '-')
            {
                return false;
            }
        }

        return true;
    }

    struct ServerState
    {
        planet_tools::TimelapseToolHost& Host;
        bool QuitRequested{ false };

        void OnSimChanged(std::string_view field_path)
        {
            if (Host.OnSimChanged)
            {
                Host.OnSimChanged(field_path);
            }
        }

        void HandleCapture(int32_t x,
                           int32_t y,
                           int32_t width,
                           int32_t height,
                           uint32_t downsample,
                           int32_t mode,
                           std::string_view name,
                           std::string_view default_name,
                           agent::ReplyToken<planet_tools::CaptureReply> token)
        {
            if (!IsSafeCaptureName(name))
            {
                token.Fail(std::format("the name '{}' is refused: use up to {} letters, digits, '_' and '-'",
                                       name, k_MaxCaptureNameLength));
                return;
            }

            const std::string captureName{ name.empty() ? default_name : name };
            const planet_tools::CaptureOutput result =
                Host.CaptureArea(x, y, width, height, downsample, mode, captureName);

            if (!result.Succeeded)
            {
                token.Fail(result.Error.empty() ? "capture failed to write PNG" : result.Error);
                return;
            }

            token.Complete(planet_tools::CaptureReply
            {
                .Path = result.Path,
                .Width = result.Width,
                .Height = result.Height,
            });
        }
    };

    struct StepCommand
    {
        static constexpr std::string_view Name{ "planet.step" };
        static constexpr std::string_view Doc{ "Runs Steps live tectonic simulation ticks and updates MapOutput." };
        using Params = planet_tools::StepParams;
        using Reply = planet_tools::SimStateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const std::optional<std::string> error = State.Host.Step(params.Steps, params.Dt);
            if (error.has_value())
            {
                token.Fail(*error);
                return;
            }

            token.Complete(Reply{ .Step = State.Host.Sim.Step, .Time = State.Host.Sim.Time });
        }

        ServerState& State;
    };

    struct ResetCommand
    {
        static constexpr std::string_view Name{ "planet.reset" };
        static constexpr std::string_view Doc{ "Resets simulation buffers to step 0 and renders the initial MapOutput." };
        using Params = agent::NoParams;
        using Reply = planet_tools::SimStateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);

            const std::optional<std::string> error = State.Host.Reset();
            if (error.has_value())
            {
                token.Fail(*error);
                return;
            }

            token.Complete(Reply{ .Step = State.Host.Sim.Step, .Time = State.Host.Sim.Time });
        }

        ServerState& State;
    };

    struct CaptureCommand
    {
        static constexpr std::string_view Name{ "planet.capture" };
        static constexpr std::string_view Doc{ "Captures the full MapOutput image to a PNG file." };
        using Params = planet_tools::CaptureParams;
        using Reply = planet_tools::CaptureReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const uint32_t downsample = static_cast<uint32_t>(std::max(params.Downsample, 1));
            State.HandleCapture(0, 0, State.Host.Sim.Width, State.Host.Sim.Height, downsample, params.Mode,
                                params.Name, "map", std::move(token));
        }

        ServerState& State;
    };

    struct CaptureRegionCommand
    {
        static constexpr std::string_view Name{ "planet.capture_region" };
        static constexpr std::string_view Doc{ "Captures a clamped sub-rectangle of MapOutput at 1:1 resolution to a PNG file." };
        using Params = planet_tools::CaptureRegionParams;
        using Reply = planet_tools::CaptureReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const int32_t clampedX = std::clamp(params.X, 0, State.Host.Sim.Width - 1);
            const int32_t clampedY = std::clamp(params.Y, 0, State.Host.Sim.Height - 1);
            const int32_t clampedW = std::clamp(params.Width, 1, State.Host.Sim.Width - clampedX);
            const int32_t clampedH = std::clamp(params.Height, 1, State.Host.Sim.Height - clampedY);

            State.HandleCapture(clampedX, clampedY, clampedW, clampedH, 1u, params.Mode, params.Name, "region",
                                std::move(token));
        }

        ServerState& State;
    };

    struct QuitCommand
    {
        static constexpr std::string_view Name{ "planet.quit" };
        static constexpr std::string_view Doc{ "Stops the PlanetGenTimelapse tool server and exits." };
        using Params = agent::NoParams;
        using Reply = agent::NoReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);
            State.QuitRequested = true;
            token.Complete(agent::NoReply{});
        }

        ServerState& State;
    };
}

namespace planet_tools
{
    int RunTimelapseToolServer(int32_t port, TimelapseToolHost& host)
    {
        ServerState state{ .Host = host };

        StepCommand stepCmd{ state };
        ResetCommand resetCmd{ state };
        CaptureCommand captureCmd{ state };
        CaptureRegionCommand captureRegionCmd{ state };
        QuitCommand quitCmd{ state };

        agent::ToolRegistry registry;
        const agent::ObjectChanged simChanged = agent::ObjectChanged::Create<ServerState, &ServerState::OnSimChanged>(&state);

        const std::array errors
        {
            registry.AddObject("planet.sim", host.Sim, simChanged),
            registry.AddCommand(stepCmd),
            registry.AddCommand(resetCmd),
            registry.AddCommand(captureCmd),
            registry.AddCommand(captureRegionCmd),
            registry.AddCommand(quitCmd),
        };

        for (const agent::ToolResultCode error : errors)
        {
            if (error != agent::ToolResultCode::Success)
            {
                std::println(stderr, "PlanetGenTimelapse: tool registration failed: {}", agent::ToString(error));
                return 1;
            }
        }

        registry.Freeze();

        agent::ToolServer server;
        const agent::ToolServerResult started = server.Start(port);

        if (started != agent::ToolServerResult::Success)
        {
            std::println(stderr, "PlanetGenTimelapse: ToolServer failed to start on port {}: {}",
                         port, agent::ToString(started));
            return 1;
        }

        std::println("ToolServer listening on 127.0.0.1:{}", server.Port());
        std::fflush(stdout);

        while (!state.QuitRequested)
        {
            server.Pump(registry, k_MaxToolRequestsPerPump);

            if (!state.QuitRequested)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        server.Stop();
        return 0;
    }
}
