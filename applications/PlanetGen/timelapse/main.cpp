#include "CommandBuffer.hpp"
#include "FrameCapture.hpp"
#include "GpuBuffers.hpp"
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
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

// PlanetGenTimelapse: runs live plate simulation steps and captures equirectangular PNG frames.
namespace
{
    struct TimelapseOptions
    {
        std::filesystem::path Reference;
        std::filesystem::path OutDir;
        uint32_t Steps{ 60u };
        float Dt{ 0.5f };
        uint32_t Every{ 1u };
        uint32_t Width{ 1024u };
        uint32_t Height{ 512u };
    };

    [[nodiscard]] bool ParseUint(std::string_view text, uint32_t& value);
    [[nodiscard]] bool ParseFloat(std::string_view text, float& value);
    [[nodiscard]] std::optional<TimelapseOptions> ParseOptions(std::span<char* const> arguments);
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

    std::println("PlanetGenTimelapse: {}, {} points, {} plates, crust flags in {}",
                 session->DeviceName(), reference->PointCount(), reference->PlateCount(),
                 planet::ToString((*shaders)->CrustFlags()));
    std::println("Running {} steps (dt = {} My, capture every {} step(s), {}x{}) -> {}",
                 options->Steps, options->Dt, options->Every, options->Width, options->Height,
                 options->OutDir.string());

    uint32_t writtenCount = 0u;
    uint32_t failedCount = 0u;

    {
        planet::GpuBuffers buffers(session->Resources());
        const planet::StepSize size
        {
            static_cast<uint32_t>(reference->PointCount()),
            static_cast<uint32_t>(reference->PlateCount()),
        };

        const std::array<planet::UniformValue, 1> movePlatesInit
        {
            planet::UniformFloat("Time", reference->Time()),
        };
        const std::array<planet::UniformValue, 3> renderMapInit
        {
            planet::UniformUint("Width", options->Width),
            planet::UniformUint("Height", options->Height),
            planet::UniformFloat("Time", reference->Time()),
        };
        const ImageFormat mapFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };

        if (!planet::AddStepBuffers(buffers, **shaders, *reference, planet::StepReadback::None, options->Dt) ||
            buffers.AddUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", movePlatesInit) != planet::PlanetGpuError::Success ||
            buffers.AddUniformFrom(**shaders, "RenderMapCS", "RenderMapConstants", renderMapInit) != planet::PlanetGpuError::Success ||
            buffers.AddStorageImage("MapOutput", options->Width, options->Height, mapFormat) != planet::PlanetGpuError::Success)
        {
            return 1;
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

            if (!session->SubmitAndWait(initCommands))
            {
                return 1;
            }
        }

        capture::FrameCapture capture(session->Resources(), 1u, options->OutDir);
        uint32_t frameIndex = 0u;

        for (uint32_t step = 0u; step < options->Steps; ++step)
        {
            const float time = reference->Time() + static_cast<float>(step + 1u) * options->Dt;
            const std::array<planet::UniformValue, 1> timeValues
            {
                planet::UniformFloat("Time", time),
            };
            const std::array<planet::UniformValue, 3> renderValues
            {
                planet::UniformUint("Width", options->Width),
                planet::UniformUint("Height", options->Height),
                planet::UniformFloat("Time", time),
            };

            if (buffers.UpdateUniformFrom(**shaders, "FillGapsCS", "Pass3Constants", timeValues) != planet::PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "UpliftCS", "UpliftConstants", timeValues) != planet::PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", timeValues) != planet::PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "RenderMapCS", "RenderMapConstants", renderValues) != planet::PlanetGpuError::Success)
            {
                return 1;
            }

            rhi::CommandBuffer commandBuffer = session->BeginCommands();
            (*stepRunner)->RecordLiveStep(commandBuffer);
            planet::RecordComputeBarrier(commandBuffer);
            (*stepRunner)->RecordDispatch2D(commandBuffer, "RenderMapCS", options->Width, options->Height);

            if (step % options->Every == 0u)
            {
                const capture::CaptureSource source
                {
                    .Image = *mapOutput,
                    .Width = options->Width,
                    .Height = options->Height,
                    .Format = mapFormat,
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
                    session->DiscardCommands(commandBuffer);
                    return 1;
                }
            }

            if (!session->SubmitAndWait(commandBuffer))
            {
                return 1;
            }

            capture.Collect(0u);
        }

        capture.Flush();
        writtenCount = capture.WrittenCount();
        failedCount = capture.FailedCount();
    }

    std::println("Wrote {} frames ({} failed) to {}", writtenCount, failedCount, options->OutDir.string());
    std::println("Encode video with:\n  ffmpeg -framerate 15 -i \"{}/frame_%04d.png\" -c:v libx264 -pix_fmt yuv420p \"{}/timelapse.mp4\"",
                 options->OutDir.generic_string(), options->OutDir.generic_string());
    return failedCount == 0u ? 0 : 1;
}

namespace
{
    bool ParseUint(std::string_view text, uint32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    bool ParseFloat(std::string_view text, float& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
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
            else if (text.starts_with("--width="))
            {
                valid = ParseUint(text.substr(8u), options.Width);
            }
            else if (text.starts_with("--height="))
            {
                valid = ParseUint(text.substr(9u), options.Height);
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
            options.Width == 0u || options.Height == 0u)
        {
            std::println(stderr, "usage: PlanetGenTimelapse --reference=<folder> --out=<dir> [--steps=N] [--dt=F] [--every=N] [--width=N] [--height=N]");
            return std::nullopt;
        }

        return options;
    }
}
