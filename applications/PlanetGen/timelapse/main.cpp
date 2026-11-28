#include "CommandBuffer.hpp"
#include "FrameCapture.hpp"
#include "GpuBuffers.hpp"
#include "Healpix.hpp"
#include "ImageDataFormats.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "StepState.hpp"
#include "ShaderLibrary.host.hpp"
#include "TimelapseAgentTools.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// PlanetGenTimelapse: runs live plate simulation steps and captures equirectangular PNG frames,
// or serves interactive AgentTools remote-control requests when --tool-port is provided.
namespace
{
    struct TimelapseOptions
    {
        std::filesystem::path Reference;
        std::filesystem::path OutDir;
        uint32_t Order{ 7u };
        uint32_t Steps{ 60u };
        float Dt{ 0.5f };
        uint32_t Every{ 1u };
        uint32_t Width{ 1024u };
        uint32_t Height{ 512u };
        uint32_t Mode{ 0u };
        std::filesystem::path StatsPath;
        uint32_t StatsEvery{ 10u };
        int32_t ToolPort{ -1 };
    };

    [[nodiscard]] bool ParseUint(std::string_view text, uint32_t& value);
    [[nodiscard]] bool ParseInt(std::string_view text, int32_t& value);
    [[nodiscard]] bool ParseFloat(std::string_view text, float& value);
    [[nodiscard]] bool ParseMode(std::string_view text, uint32_t& mode);
    [[nodiscard]] std::optional<TimelapseOptions> ParseOptions(std::span<char* const> arguments);

    [[nodiscard]] bool UpdateTimeUniforms(planet::GpuBuffers& buffers, const planet::PlanetShaders& shaders, float time);
    [[nodiscard]] bool UpdateParamsUniform(planet::GpuBuffers& buffers,
                                           const planet::PlanetShaders& shaders,
                                           planet::StepSize size,
                                           float dt,
                                           uint32_t healpix_order);
    [[nodiscard]] bool UpdateRenderMapUniform(planet::GpuBuffers& buffers,
                                              const planet::PlanetShaders& shaders,
                                              uint32_t width,
                                              uint32_t height,
                                              float time,
                                              uint32_t mode);
    [[nodiscard]] bool RenderMapToOutput(planet::PlanetSession& session,
                                         const planet::PlanetStep& step_runner,
                                         uint32_t width,
                                         uint32_t height);

    struct SimulationHost
    {
        planet::PlanetSession& Session;
        const planet::PlanetShaders& Shaders;
        planet::GpuBuffers& Buffers;
        const planet::PlanetStep& StepRunner;
        const planet::ReferenceSet& Reference;
        capture::FrameCapture& Capture;
        rhi::ImageHandle MapOutput;
        ImageFormat MapFormat;
        planet::StepSize Size;
        uint32_t HealpixOrder{ 7u };
        planet_tools::SimState Sim;

        void OnSimChanged(std::string_view field_path);
        [[nodiscard]] std::optional<std::string> Step(int32_t steps, float dt);
        [[nodiscard]] std::optional<std::string> Reset();
        [[nodiscard]] planet_tools::CaptureOutput CaptureArea(int32_t x,
                                                              int32_t y,
                                                              int32_t width,
                                                              int32_t height,
                                                              uint32_t downsample,
                                                              int32_t mode,
                                                              std::string_view name);
    };

    [[nodiscard]] int RunBatchTimelapse(planet::PlanetSession& session,
                                        const planet::PlanetShaders& shaders,
                                        planet::GpuBuffers& buffers,
                                        const planet::PlanetStep& step_runner,
                                        const planet::ReferenceSet& reference,
                                        capture::FrameCapture& capture,
                                        rhi::ImageHandle map_output,
                                        ImageFormat map_format,
                                        const TimelapseOptions& options);

    [[nodiscard]] int RunToolMode(planet::PlanetSession& session,
                                  const planet::PlanetShaders& shaders,
                                  planet::GpuBuffers& buffers,
                                  const planet::PlanetStep& step_runner,
                                  const planet::ReferenceSet& reference,
                                  capture::FrameCapture& capture,
                                  rhi::ImageHandle map_output,
                                  ImageFormat map_format,
                                  planet::StepSize size,
                                  const TimelapseOptions& options);
}

int main(int argc, char* argv[])
{
    const std::span<char* const> arguments{ argv, static_cast<size_t>(argc) };
    const std::optional<TimelapseOptions> options = ParseOptions(arguments);

    if (!options.has_value())
    {
        return 1;
    }

    const std::optional<planet::ReferenceSet> reference = planet::ReferenceSet::Open(options->Reference);

    if (!reference.has_value())
    {
        return 1;
    }

    const planet::PlanetSessionConfig sessionConfig
    {
        "PlanetGenTimelapse",
        rhi::ValidationLayers::None,
        planet::ConfigFileMode::Ignore,
        {},
    };
    const std::unique_ptr<planet::PlanetSession> session =
        planet::PlanetSession::Start(arguments, sessionConfig);

    if (session == nullptr)
    {
        return 1;
    }

    std::array<std::string_view, planet::k_LiveStepEntryPoints.size() + 1u> entryPoints{};
    std::ranges::copy(planet::k_LiveStepEntryPoints, entryPoints.begin());
    entryPoints.back() = "RenderMapCS";

    const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders =
        session->CreateShaders(planet::CrustFlagChoice::BestForDevice, entryPoints);

    if (!shaders.has_value())
    {
        return 1;
    }

    const uint32_t pointCount = static_cast<uint32_t>(planet::HealpixPixelCount(options->Order));
    std::println("PlanetGenTimelapse: {}, {} points (HEALPix order {}), {} plates, crust flags in {}",
                 session->DeviceName(), pointCount, options->Order, reference->PlateCount(),
                 planet::ToString((*shaders)->CrustFlags()));

    if (options->ToolPort < 0)
    {
        std::println("Running {} steps (dt = {} My, capture every {} step(s), {}x{}, mode={}) -> {}",
                     options->Steps, options->Dt, options->Every, options->Width, options->Height, options->Mode,
                     options->OutDir.string());
    }

    planet::GpuBuffers buffers(session->Resources());
    const planet::StepSize size
    {
        pointCount,
        static_cast<uint32_t>(reference->PlateCount()),
    };

    const std::array<planet::UniformValue, 1> movePlatesInit
    {
        planet::UniformFloat("Time", reference->Time()),
    };
    const std::array<planet::UniformValue, 4> renderMapInit
    {
        planet::UniformUint("Width", options->Width),
        planet::UniformUint("Height", options->Height),
        planet::UniformFloat("Time", reference->Time()),
        planet::UniformUint("Mode", options->Mode),
    };
    const ImageFormat mapFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
    const planet_gen_shaders::PlumeParams<
        planet_gen_shaders::CookProfile::Spirv,
        ::lodestone::LayoutRule::Std140> zeroPlumes{};

    if (!planet::AddHealpixStepBuffers(buffers, **shaders, *reference, options->Order, (*shaders)->CrustFlags(),
                                       options->ToolPort >= 0, options->Dt, !options->StatsPath.empty()) ||
        buffers.AddUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", movePlatesInit) != planet::PlanetGpuError::Success ||
        buffers.AddUniformFrom(**shaders, "RenderMapCS", "RenderMapConstants", renderMapInit) != planet::PlanetGpuError::Success ||
        buffers.AddUniform("PlumeConstants", std::as_bytes(std::span{ &zeroPlumes, 1u })) != planet::PlanetGpuError::Success ||
        buffers.AddStorageImage("MapOutput", options->Width, options->Height, mapFormat) != planet::PlanetGpuError::Success)
    {
        return 1;
    }

    if (options->ToolPort >= 0)
    {
        const size_t rotBytes = static_cast<size_t>(reference->PlateCount()) * 3u * 4u * sizeof(float);
        const size_t poleBytes = static_cast<size_t>(reference->PlateCount()) * 4u * sizeof(float);

        if (buffers.AddZeroedStorage("Initial.Rotations", rotBytes, false) != planet::PlanetGpuError::Success ||
            buffers.AddZeroedStorage("Initial.PoleRatesIn", poleBytes, false) != planet::PlanetGpuError::Success)
        {
            return 1;
        }
    }

    const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetStep>> stepRunner =
        planet::PlanetStep::Create(**shaders, buffers, size);

    if (!stepRunner.has_value())
    {
        return 1;
    }

    const std::optional<rhi::ImageHandle> mapOutput = buffers.ImageHandle("MapOutput");

    if (!mapOutput.has_value())
    {
        return 1;
    }

    {
        rhi::CommandBuffer initCommands = session->BeginCommands();
        const rhi::ImageBarrier toGeneral
        {
            .Image = *mapOutput,
            .SrcStages = rhi::PipelineStage::None,
            .SrcAccess = rhi::Access::None,
            .DstStages = rhi::PipelineStage::ComputeShader,
            .DstAccess = rhi::Access::ShaderStorageWrite,
            .OldLayout = rhi::ImageLayout::Undefined,
            .NewLayout = rhi::ImageLayout::General,
            .Range = rhi::ImageSubresourceRange{ rhi::ImageAspect::Color, 0u, 1u, 0u, 1u },
        };
        initCommands.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toGeneral, 1 } });

        if (options->ToolPort >= 0)
        {
            if (buffers.RecordCopy(initCommands, "Rotations", "Initial.Rotations") != planet::PlanetGpuError::Success ||
                buffers.RecordCopy(initCommands, "PoleRatesIn", "Initial.PoleRatesIn") != planet::PlanetGpuError::Success)
            {
                session->DiscardCommands(initCommands);
                return 1;
            }
        }

        if (!session->SubmitAndWait(initCommands))
        {
            return 1;
        }
    }

    const uint32_t writerThreads = options->ToolPort >= 0
        ? 1u
        : std::clamp(std::thread::hardware_concurrency(), 2u, 12u);
    capture::FrameCapture capture(session->Resources(), 1u, options->OutDir, writerThreads);

    if (options->ToolPort >= 0)
    {
        return RunToolMode(*session, **shaders, buffers, **stepRunner, *reference, capture, *mapOutput, mapFormat, size, *options);
    }

    return RunBatchTimelapse(*session, **shaders, buffers, **stepRunner, *reference, capture, *mapOutput, mapFormat, *options);
}

namespace
{
    bool ParseUint(std::string_view text, uint32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    bool ParseInt(std::string_view text, int32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    bool ParseFloat(std::string_view text, float& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
    }

    bool ParseMode(std::string_view text, uint32_t& mode)
    {
        if (text == "elevation")
        {
            mode = 0u;
            return true;
        }
        if (text == "plates")
        {
            mode = 1u;
            return true;
        }
        if (text == "age")
        {
            mode = 2u;
            return true;
        }
        if (text == "relief")
        {
            mode = 3u;
            return true;
        }
        if (text == "fronts")
        {
            mode = 4u;
            return true;
        }
        if (text == "voronoi")
        {
            mode = 5u;
            return true;
        }
        return false;
    }

    std::optional<TimelapseOptions> ParseOptions(std::span<char* const> arguments)
    {
        TimelapseOptions options;

        for (const char* argument : arguments.subspan(1))
        {
            const std::string_view text{ argument };
            bool valid = true;

            if (text.starts_with("--reference="))
            {
                options.Reference = text.substr(12u);
            }
            else if (text.starts_with("--out="))
            {
                options.OutDir = text.substr(6u);
            }
            else if (text.starts_with("--order="))
            {
                valid = ParseUint(text.substr(8u), options.Order) &&
                        options.Order >= 4u &&
                        options.Order <= 9u;
            }
            else if (text.starts_with("--steps="))
            {
                valid = ParseUint(text.substr(8u), options.Steps);
            }
            else if (text.starts_with("--dt="))
            {
                valid = ParseFloat(text.substr(5u), options.Dt);
            }
            else if (text.starts_with("--every="))
            {
                valid = ParseUint(text.substr(8u), options.Every);
            }
            else if (text.starts_with("--capture-every="))
            {
                valid = ParseUint(text.substr(16u), options.Every);
            }
            else if (text.starts_with("--width="))
            {
                valid = ParseUint(text.substr(8u), options.Width);
            }
            else if (text.starts_with("--height="))
            {
                valid = ParseUint(text.substr(9u), options.Height);
            }
            else if (text.starts_with("--mode="))
            {
                valid = ParseMode(text.substr(7u), options.Mode);
            }
            else if (text.starts_with("--stats="))
            {
                options.StatsPath = text.substr(8u);
                valid = !options.StatsPath.empty();
            }
            else if (text.starts_with("--stats-every="))
            {
                valid = ParseUint(text.substr(14u), options.StatsEvery) && options.StatsEvery > 0u;
            }
            else if (text.starts_with("--tool-port="))
            {
                valid = ParseInt(text.substr(12u), options.ToolPort) &&
                        options.ToolPort >= -1 &&
                        options.ToolPort <= std::numeric_limits<uint16_t>::max();
            }
            else
            {
                valid = false;
            }

            if (!valid)
            {
                std::println(stderr, "PlanetGenTimelapse: '{}' is not an option, or its value is invalid", text);
                return std::nullopt;
            }
        }

        if (options.Reference.empty() || options.OutDir.empty() ||
            options.Steps == 0u || options.Dt <= 0.0f || options.Every == 0u ||
            options.StatsEvery == 0u || options.Width == 0u || options.Height == 0u)
        {
            std::println(stderr,
                         "usage: PlanetGenTimelapse --reference=<folder> --out=<dir> [--order=N] [--steps=N] [--dt=F] [--every=N] "
                         "[--width=N] [--height=N] [--mode=elevation|plates|age|relief|fronts|voronoi] "
                         "[--stats=<path.csv>] [--stats-every=N] [--tool-port=N]");
            return std::nullopt;
        }

        return options;
    }

    bool UpdateTimeUniforms(planet::GpuBuffers& buffers, const planet::PlanetShaders& shaders, float time)
    {
        const std::array<planet::UniformValue, 1> timeValues
        {
            planet::UniformFloat("Time", time),
        };

        return buffers.UpdateUniformFrom(shaders, "FillGapsCS", "Pass3Constants", timeValues) == planet::PlanetGpuError::Success &&
               buffers.UpdateUniformFrom(shaders, "FrontDistanceAndUpliftCS", "UpliftConstants", timeValues) == planet::PlanetGpuError::Success &&
               buffers.UpdateUniformFrom(shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", timeValues) == planet::PlanetGpuError::Success;
    }

    bool UpdateParamsUniform(planet::GpuBuffers& buffers,
                             const planet::PlanetShaders& shaders,
                             planet::StepSize size,
                             float dt,
                             uint32_t healpix_order)
    {
        const std::array<planet::UniformValue, 4> paramsValues
        {
            planet::UniformUint("PointCount", size.PointCount),
            planet::UniformUint("PlateCount", size.PlateCount),
            planet::UniformFloat("TimeStepMy", dt),
            planet::UniformUint("HealpixOrder", healpix_order),
        };

        return buffers.UpdateUniformFrom(shaders, "OwnersCS", "StepParams", paramsValues) == planet::PlanetGpuError::Success;
    }

    bool UpdateRenderMapUniform(planet::GpuBuffers& buffers,
                                const planet::PlanetShaders& shaders,
                                uint32_t width,
                                uint32_t height,
                                float time,
                                uint32_t mode)
    {
        const std::array<planet::UniformValue, 4> renderValues
        {
            planet::UniformUint("Width", width),
            planet::UniformUint("Height", height),
            planet::UniformFloat("Time", time),
            planet::UniformUint("Mode", mode),
        };

        return buffers.UpdateUniformFrom(shaders, "RenderMapCS", "RenderMapConstants", renderValues) == planet::PlanetGpuError::Success;
    }

    bool RenderMapToOutput(planet::PlanetSession& session,
                           const planet::PlanetStep& step_runner,
                           uint32_t width,
                           uint32_t height)
    {
        rhi::CommandBuffer commandBuffer = session.BeginCommands();
        step_runner.RecordDispatch2D(commandBuffer, "RenderMapCS", width, height);
        return session.SubmitAndWait(commandBuffer);
    }

    void SimulationHost::OnSimChanged(std::string_view field_path)
    {
        if (field_path == "TimeStepMy")
        {
            static_cast<void>(UpdateParamsUniform(Buffers, Shaders, Size, Sim.TimeStepMy, HealpixOrder));
        }
        else if (field_path == "RenderMode")
        {
            const uint32_t width = static_cast<uint32_t>(Sim.Width);
            const uint32_t height = static_cast<uint32_t>(Sim.Height);
            const uint32_t mode = static_cast<uint32_t>(Sim.RenderMode);

            if (UpdateRenderMapUniform(Buffers, Shaders, width, height, Sim.Time, mode))
            {
                static_cast<void>(RenderMapToOutput(Session, StepRunner, width, height));
            }
        }
    }

    std::optional<std::string> SimulationHost::Step(int32_t steps, float dt)
    {
        if (dt > 0.0f)
        {
            Sim.TimeStepMy = dt;
            if (!UpdateParamsUniform(Buffers, Shaders, Size, Sim.TimeStepMy, HealpixOrder))
            {
                return std::string{ "failed to update Params uniform" };
            }
        }

        for (int32_t step = 0; step < steps; ++step)
        {
            Sim.Time += Sim.TimeStepMy;
            ++Sim.Step;

            if (!UpdateTimeUniforms(Buffers, Shaders, Sim.Time))
            {
                return std::string{ "failed to update time uniforms" };
            }

            rhi::CommandBuffer commandBuffer = Session.BeginCommands();
            StepRunner.RecordLiveStep(commandBuffer);

            if (!Session.SubmitAndWait(commandBuffer))
            {
                return std::string{ "failed to submit live step" };
            }
        }

        const uint32_t width = static_cast<uint32_t>(Sim.Width);
        const uint32_t height = static_cast<uint32_t>(Sim.Height);

        if (!UpdateRenderMapUniform(Buffers, Shaders, width, height, Sim.Time,
                                    static_cast<uint32_t>(Sim.RenderMode)) ||
            !RenderMapToOutput(Session, StepRunner, width, height))
        {
            return std::string{ "failed to render MapOutput" };
        }

        return std::nullopt;
    }

    std::optional<std::string> SimulationHost::Reset()
    {
        Sim.Step = 0;
        Sim.Time = Reference.Time();

        const uint32_t width = static_cast<uint32_t>(Sim.Width);
        const uint32_t height = static_cast<uint32_t>(Sim.Height);

        if (!UpdateTimeUniforms(Buffers, Shaders, Sim.Time) ||
            !UpdateRenderMapUniform(Buffers, Shaders, width, height, Sim.Time,
                                    static_cast<uint32_t>(Sim.RenderMode)))
        {
            return std::string{ "failed to restore uniforms" };
        }

        rhi::CommandBuffer commandBuffer = Session.BeginCommands();

        if (Buffers.RecordCopy(commandBuffer, "Initial.Rotations", "Rotations") != planet::PlanetGpuError::Success ||
            Buffers.RecordCopy(commandBuffer, "Initial.PoleRatesIn", "PoleRatesIn") != planet::PlanetGpuError::Success ||
            StepRunner.RecordReset(commandBuffer) != planet::PlanetGpuError::Success)
        {
            Session.DiscardCommands(commandBuffer);
            return std::string{ "failed to record simulation reset" };
        }

        StepRunner.RecordStep(commandBuffer);
        planet::RecordComputeBarrier(commandBuffer);
        StepRunner.RecordDispatch2D(commandBuffer, "RenderMapCS", width, height);

        if (!Session.SubmitAndWait(commandBuffer))
        {
            return std::string{ "failed to submit reset" };
        }

        return std::nullopt;
    }

    planet_tools::CaptureOutput SimulationHost::CaptureArea(int32_t x,
                                                            int32_t y,
                                                            int32_t width,
                                                            int32_t height,
                                                            uint32_t downsample,
                                                            int32_t mode,
                                                            std::string_view name)
    {
        const uint32_t mapWidth = static_cast<uint32_t>(Sim.Width);
        const uint32_t mapHeight = static_cast<uint32_t>(Sim.Height);
        const bool overrideMode = mode >= 0 && mode <= 5 && mode != Sim.RenderMode;
        const uint32_t effectiveMode = (mode >= 0 && mode <= 5) ? static_cast<uint32_t>(mode)
                                                                : static_cast<uint32_t>(Sim.RenderMode);

        if (overrideMode && !UpdateRenderMapUniform(Buffers, Shaders, mapWidth, mapHeight, Sim.Time, effectiveMode))
        {
            return planet_tools::CaptureOutput{ .Error = "failed to update RenderMapConstants" };
        }

        rhi::CommandBuffer commandBuffer = Session.BeginCommands();

        if (overrideMode)
        {
            StepRunner.RecordDispatch2D(commandBuffer, "RenderMapCS", mapWidth, mapHeight);
        }

        const capture::CaptureSource source
        {
            .Image = MapOutput,
            .Width = static_cast<uint32_t>(width),
            .Height = static_cast<uint32_t>(height),
            .Format = MapFormat,
            .Encoding = capture::CaptureEncoding::sRGB,
            .CurrentLayout = rhi::ImageLayout::General,
            .SrcStages = rhi::PipelineStage::ComputeShader,
            .SrcAccess = rhi::Access::ShaderStorageWrite,
            .FinalLayout = rhi::ImageLayout::General,
            .DstStages = rhi::PipelineStage::ComputeShader,
            .DstAccess = rhi::Access::ShaderStorageWrite,
            .OffsetX = x,
            .OffsetY = y,
            .Downsample = downsample,
        };

        const std::optional<uint64_t> ticket =
            Capture.RecordTrackedCopy(commandBuffer, 0u, source, std::string{ name }, capture::CaptureOutput::Png);

        if (!ticket.has_value())
        {
            Session.DiscardCommands(commandBuffer);
            return planet_tools::CaptureOutput{ .Error = "failed to record tracked capture" };
        }

        if (!Session.SubmitAndWait(commandBuffer))
        {
            return planet_tools::CaptureOutput{ .Error = "failed to submit capture command buffer" };
        }

        Capture.Collect(0u);
        Capture.Flush();
        const std::vector<capture::CaptureResult> results = Capture.TakeResults();

        if (overrideMode)
        {
            if (UpdateRenderMapUniform(Buffers, Shaders, mapWidth, mapHeight, Sim.Time, static_cast<uint32_t>(Sim.RenderMode)))
            {
                static_cast<void>(RenderMapToOutput(Session, StepRunner, mapWidth, mapHeight));
            }
        }

        const auto match = std::ranges::find_if(results, [id = *ticket](const capture::CaptureResult& r)
        {
            return r.Ticket == id;
        });

        if (match == results.end() || !match->Succeeded)
        {
            return planet_tools::CaptureOutput{ .Error = "capture failed to write PNG" };
        }

        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(match->Path, error);

        return planet_tools::CaptureOutput
        {
            .Succeeded = true,
            .Path = (error ? match->Path : absolute).string(),
            .Width = static_cast<int32_t>(match->Width),
            .Height = static_cast<int32_t>(match->Height),
        };
    }

    [[nodiscard]] bool WriteStatsRow(std::ofstream& stats_file,
                                     const planet::PlanetShaders& shaders,
                                     const planet::GpuBuffers& buffers,
                                     uint32_t step,
                                     float time)
    {
        const planet::PlanetGpuResult<std::vector<uint32_t>> frontOffsets =
            buffers.ReadWords("PlateFrontPointOffset");
        const planet::PlanetGpuResult<std::vector<math::Float4>> poleRates =
            buffers.ReadFloat4s("PoleRatesIn");
        const planet::PlanetGpuResult<std::vector<uint32_t>> collisionJob =
            buffers.ReadWords("CollisionJob");
        const planet::PlanetGpuResult<std::vector<uint32_t>> scratchCounters =
            buffers.ReadWords("CollideScratchCounters");
        const planet::PlanetGpuResult<std::vector<math::Float4>> crustState =
            buffers.ReadFloat4s("CrustState");

        if (!frontOffsets.has_value() || frontOffsets->empty() ||
            !poleRates.has_value() ||
            !collisionJob.has_value() || collisionJob->size() < 9u ||
            !scratchCounters.has_value() || scratchCounters->size() < 3u ||
            !crustState.has_value())
        {
            return false;
        }

        const planet::CrustFlagStorage flagStorage = shaders.CrustFlags();
        const std::string_view flagBufferName = planet::CrustFlagBufferNames(flagStorage);
        std::vector<std::byte> flagBytes;
        std::vector<uint32_t> flagWords;

        if (flagStorage == planet::CrustFlagStorage::Bytes)
        {
            const planet::PlanetGpuResult<std::vector<std::byte>> readBytes = buffers.Read(flagBufferName);
            if (!readBytes.has_value() || readBytes->size() != crustState->size())
            {
                return false;
            }
            flagBytes = *readBytes;
        }
        else
        {
            const planet::PlanetGpuResult<std::vector<uint32_t>> readWords = buffers.ReadWords(flagBufferName);
            if (!readWords.has_value() || readWords->size() != crustState->size())
            {
                return false;
            }
            flagWords = *readWords;
        }

        const uint32_t frontPoints = frontOffsets->back();

        double speedSum = 0.0;
        float maxSpeed = 0.0f;
        for (const math::Float4& pole : *poleRates)
        {
            const float speedKmMy = pole.w * 6370.0f;
            speedSum += static_cast<double>(speedKmMy);
            maxSpeed = std::max(maxSpeed, speedKmMy);
        }
        const float meanSpeed = poleRates->empty()
            ? 0.0f
            : static_cast<float>(speedSum / static_cast<double>(poleRates->size()));

        const uint32_t collisionFired = ((*collisionJob)[0] != 0xFFFFFFFFu) ? 1u : 0u;
        const int32_t collSrc = collisionFired != 0u ? static_cast<int32_t>((*collisionJob)[3]) : -1;
        const int32_t collDst = collisionFired != 0u ? static_cast<int32_t>((*collisionJob)[4]) : -1;
        const uint32_t collTerrane = collisionFired != 0u ? (*collisionJob)[8] : 0u;
        const uint32_t collSuture = collisionFired != 0u ? (*scratchCounters)[2] : 0u;

        std::vector<float> reliefs;
        double reliefSum = 0.0;
        float reliefMax = -std::numeric_limits<float>::infinity();
        uint32_t orogAndean = 0u;
        uint32_t orogHimalayan = 0u;
        uint32_t orogIslandArc = 0u;

        for (size_t i = 0u; i < crustState->size(); ++i)
        {
            const uint32_t flags = flagStorage == planet::CrustFlagStorage::Bytes
                ? static_cast<uint32_t>(flagBytes[i])
                : flagWords[i];
            if ((flags & 3u) != 3u)
            {
                continue;
            }

            const math::Float4& state = (*crustState)[i];
            reliefs.push_back(state.x);
            reliefSum += static_cast<double>(state.x);
            reliefMax = std::max(reliefMax, state.x);

            const int32_t orog = std::bit_cast<int32_t>(state.w);
            if (orog == 1)
            {
                ++orogAndean;
            }
            else if (orog == 2)
            {
                ++orogHimalayan;
            }
            else if (orog == 3)
            {
                ++orogIslandArc;
            }
        }

        const uint32_t contCells = static_cast<uint32_t>(reliefs.size());
        float reliefMean = 0.0f;
        float reliefP95 = 0.0f;
        if (reliefs.empty())
        {
            reliefMax = 0.0f;
        }
        else
        {
            reliefMean = static_cast<float>(reliefSum / static_cast<double>(reliefs.size()));
            const size_t p95Index = std::min(reliefs.size() - 1u, (reliefs.size() * 95u) / 100u);
            std::nth_element(reliefs.begin(),
                             reliefs.begin() + static_cast<std::ptrdiff_t>(p95Index),
                             reliefs.end());
            reliefP95 = reliefs[p95Index];
        }

        stats_file << std::format(
            "{},{:.3f},{},{:.4f},{:.4f},{},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{},{},{}\n",
            step, time, frontPoints, meanSpeed, maxSpeed,
            collisionFired, collSrc, collDst, collTerrane, collSuture,
            contCells, reliefMean, reliefP95, reliefMax,
            orogAndean, orogHimalayan, orogIslandArc);
        stats_file.flush();
        return stats_file.good();
    }

    int RunBatchTimelapse(planet::PlanetSession& session,
                          const planet::PlanetShaders& shaders,
                          planet::GpuBuffers& buffers,
                          const planet::PlanetStep& step_runner,
                          const planet::ReferenceSet& reference,
                          capture::FrameCapture& capture,
                          rhi::ImageHandle map_output,
                          ImageFormat map_format,
                          const TimelapseOptions& options)
    {
        std::optional<std::ofstream> statsFile;
        if (!options.StatsPath.empty())
        {
            const std::filesystem::path parentDir = options.StatsPath.parent_path();
            if (!parentDir.empty())
            {
                std::error_code dirError;
                std::filesystem::create_directories(parentDir, dirError);
                if (dirError)
                {
                    std::println(stderr, "PlanetGenTimelapse: failed to create stats directory {}: {}",
                                 parentDir.string(), dirError.message());
                    return 1;
                }
            }

            statsFile.emplace(options.StatsPath);
            if (!statsFile->is_open())
            {
                std::println(stderr, "PlanetGenTimelapse: failed to open stats file {}", options.StatsPath.string());
                return 1;
            }

            *statsFile << "step,time_my,front_points,mean_speed_km_my,max_speed_km_my,collision_fired,coll_src,coll_dst,"
                          "coll_terrane,coll_suture,cont_cells,relief_mean_km,relief_p95_km,relief_max_km,"
                          "orog_andean,orog_himalayan,orog_island_arc\n";
        }

        uint32_t frameIndex = 0u;

        for (uint32_t step = 0u; step < options.Steps; ++step)
        {
            const float time = reference.Time() + static_cast<float>(step + 1u) * options.Dt;

            const bool captureThisStep = (step % options.Every == 0u);
            const bool statsThisStep = statsFile.has_value() &&
                (step == 0u || (step + 1u) % options.StatsEvery == 0u || (step + 1u) == options.Steps);

            if (!UpdateTimeUniforms(buffers, shaders, time))
            {
                return 1;
            }

            if (captureThisStep &&
                !UpdateRenderMapUniform(buffers, shaders, options.Width, options.Height, time, options.Mode))
            {
                return 1;
            }

            rhi::CommandBuffer commandBuffer = session.BeginCommands();
            step_runner.RecordLiveStep(commandBuffer);

            if (captureThisStep)
            {
                planet::RecordComputeBarrier(commandBuffer);
                step_runner.RecordDispatch2D(commandBuffer, "RenderMapCS", options.Width, options.Height);

                const capture::CaptureSource source
                {
                    .Image = map_output,
                    .Width = options.Width,
                    .Height = options.Height,
                    .Format = map_format,
                    .Encoding = capture::CaptureEncoding::sRGB,
                    .CurrentLayout = rhi::ImageLayout::General,
                    .SrcStages = rhi::PipelineStage::ComputeShader,
                    .SrcAccess = rhi::Access::ShaderStorageWrite,
                    .FinalLayout = rhi::ImageLayout::General,
                    .DstStages = rhi::PipelineStage::ComputeShader,
                    .DstAccess = rhi::Access::ShaderStorageWrite,
                };
                const std::string name = std::format("frame_{:04d}", frameIndex++);

                if (!capture.RecordCopy(commandBuffer, 0u, source, name))
                {
                    std::println(stderr, "PlanetGenTimelapse: failed to record capture for {}", name);
                    session.DiscardCommands(commandBuffer);
                    return 1;
                }
            }

            if (statsThisStep)
            {
                buffers.RecordReadbackCopies(commandBuffer);
            }

            if (!session.SubmitAndWait(commandBuffer))
            {
                return 1;
            }

            if (captureThisStep)
            {
                capture.Collect(0u);
            }

            if (statsThisStep && !WriteStatsRow(*statsFile, shaders, buffers, step + 1u, time))
            {
                std::println(stderr, "PlanetGenTimelapse: failed to read back or write stats for step {}", step + 1u);
                return 1;
            }

            if ((step + 1u) % 10u == 0u || (step + 1u) == options.Steps)
            {
                std::println("step {:4d}/{} ({:6.1f} My) — queued {}, written {}",
                             step + 1u, options.Steps, time, frameIndex, capture.WrittenCount());
            }
        }

        capture.Flush();
        const uint32_t writtenCount = capture.WrittenCount();
        const uint32_t failedCount = capture.FailedCount();

        std::println("Wrote {} frames ({} failed) to {}", writtenCount, failedCount, options.OutDir.string());
        std::println("Encode video with:\n  ffmpeg -framerate 15 -i \"{}/frame_%04d.png\" -c:v libx264 -pix_fmt yuv420p \"{}/timelapse.mp4\"",
                     options.OutDir.generic_string(), options.OutDir.generic_string());
        return failedCount == 0u ? 0 : 1;
    }

    int RunToolMode(planet::PlanetSession& session,
                    const planet::PlanetShaders& shaders,
                    planet::GpuBuffers& buffers,
                    const planet::PlanetStep& step_runner,
                    const planet::ReferenceSet& reference,
                    capture::FrameCapture& capture,
                    rhi::ImageHandle map_output,
                    ImageFormat map_format,
                    planet::StepSize size,
                    const TimelapseOptions& options)
    {
        // Populate MapOutput at step 0 without advancing plates so initial inspection works immediately.
        {
            rhi::CommandBuffer initStep = session.BeginCommands();
            step_runner.RecordStep(initStep);
            planet::RecordComputeBarrier(initStep);
            step_runner.RecordDispatch2D(initStep, "RenderMapCS", options.Width, options.Height);

            if (!session.SubmitAndWait(initStep))
            {
                return 1;
            }
        }

        SimulationHost simHost
        {
            .Session = session,
            .Shaders = shaders,
            .Buffers = buffers,
            .StepRunner = step_runner,
            .Reference = reference,
            .Capture = capture,
            .MapOutput = map_output,
            .MapFormat = map_format,
            .Size = size,
            .HealpixOrder = options.Order,
            .Sim = planet_tools::SimState
            {
                .TimeStepMy = options.Dt,
                .RenderMode = static_cast<int32_t>(options.Mode),
                .Time = reference.Time(),
                .Step = 0,
                .Width = static_cast<int32_t>(options.Width),
                .Height = static_cast<int32_t>(options.Height),
            },
        };

        planet_tools::TimelapseToolHost toolHost
        {
            .Sim = simHost.Sim,
            .OnSimChanged = [&simHost](std::string_view field_path)
            {
                simHost.OnSimChanged(field_path);
            },
            .Step = [&simHost](int32_t steps, float dt)
            {
                return simHost.Step(steps, dt);
            },
            .Reset = [&simHost]()
            {
                return simHost.Reset();
            },
            .CaptureArea = [&simHost](int32_t x,
                                      int32_t y,
                                      int32_t width,
                                      int32_t height,
                                      uint32_t downsample,
                                      int32_t mode,
                                      std::string_view name)
            {
                return simHost.CaptureArea(x, y, width, height, downsample, mode, name);
            },
        };

        return planet_tools::RunTimelapseToolServer(options.ToolPort, toolHost);
    }
}
