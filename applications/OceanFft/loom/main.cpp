#include "Compile.hpp"
#include "Device.hpp"
#include "GraphBuilder.hpp"
#include "PassBuilders.hpp"
#include "DeviceConfig.hpp"
#include "FrameLimit.hpp"
#include "FrameTarget.hpp"
#include "ImGuiModule.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomGraph.hpp"
#include "OceanGraph.hpp"
#include "PlatformSystem.hpp"
#include "RecordContext.hpp"
#include "ResourceContext.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "SpineNeeds.hpp"
#include "Swapchain.hpp"
#include "SwapchainFrameTarget.hpp"
#include "WindowedBoot.hpp"
#include "CommandBuffer.hpp"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>

// OceanFftLoom: the shell of the OceanFft port to Loom. It boots a window, builds the graph from the Register functions
// in OceanGraph.cpp, and draws ImGui over a cleared screen. --frames=N exits after N submitted frames.

namespace
{
    constexpr int32_t k_FrameSlots = 2;
    constexpr double k_SettleSeconds = 0.1;
    constexpr int64_t k_TimeoutNs = 5'000'000'000;
    constexpr loom::PassKey k_UiPass{ "Ocean.Ui" };

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "OceanFft.ldmanifest",
            .BuildTreePath = OCEAN_FFT_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // The ImGui pass. It owns the ImGui frame: Begin before the Tick, and the Record callback renders it. When the Tick
    // records nothing, End closes the frame so that the next Begin is valid.
    class UiPass
    {
    public:
        explicit UiPass(ImGuiModule& imgui_module) noexcept : imgui{ &imgui_module }
        {
        }

        void Begin()
        {
            imgui->BeginFrame();
            ImGui::Begin("OceanFft (Loom)");
            ImGui::Text("%.2f ms", 1000.0f / ImGui::GetIO().Framerate);
            ImGui::End();
            rendered = false;
        }

        void End()
        {
            if (!rendered)
            {
                ImGui::EndFrame();
            }
        }

        // Clears the screen first, until RegisterComposite writes it
        void Record(loom::RecordContext& context)
        {
            const VkCommandBuffer commandBuffer = context.CommandBuffer().Handle().As<VkCommandBuffer>();
            const loom::Extent2D extent = context.RenderExtent();

            const VkClearAttachment clear
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0u,
                .clearValue = VkClearValue{ .color = VkClearColorValue{ .float32 = { 0.02f, 0.03f, 0.05f, 1.0f } } },
            };
            const VkClearRect rect
            {
                .rect = VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ static_cast<uint32_t>(extent.Width), static_cast<uint32_t>(extent.Height) } },
                .baseArrayLayer = 0u,
                .layerCount = 1u,
            };
            vkCmdClearAttachments(commandBuffer, 1u, &clear, 1u, &rect);

            imgui->Render(context.CommandBuffer().Handle());
            rendered = true;
        }

    private:
        ImGuiModule* imgui;
        bool rendered{ false };
    };

    void PrintDiagnostics(const loom::CollectingSink& sink)
    {
        for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
        {
            std::cerr << std::format("{} {}: {} (pass '{}', resource '{}')\n", loom::ToString(diagnostic.Code),
                                     loom::ToString(diagnostic.Severity), loom::SummaryOf(diagnostic.Code),
                                     diagnostic.Pass.Name, diagnostic.Resource.Name);
        }
    }

    void RegisterUi(loom::GraphBuilder& graph, UiPass& ui)
    {
        const auto build = [&ui](loom::RecordBuilder& pass)
        {
            pass.Uses(ocean_loom::k_Screen, loom::ResourceAccess::Write, loom::ImageUse::ColorTarget);
            pass.Record(Delegate<void(loom::RecordContext&)>::Create<UiPass, &UiPass::Record>(&ui));
        };

        graph.AddRecord(k_UiPass, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, build);
    }

    struct BuildArguments
    {
        WindowedBoot& Boot;
        UiPass& Ui;
        loom::CollectingSink& Sink;
        std::span<const loom::FrameTargetBinding> Targets;
    };

    std::unique_ptr<loom::Graph> BuildGraph(const BuildArguments& arguments)
    {
        loom::GraphBuilder builder{ "Ocean.Graph", "Ocean.Shaders" };
        ocean_loom::RegisterScreen(builder);
        ocean_loom::RegisterSimulation(builder);
        ocean_loom::RegisterFoam(builder);
        ocean_loom::RegisterEnvironment(builder);
        ocean_loom::RegisterSurface(builder);
        ocean_loom::RegisterLighting(builder);
        ocean_loom::RegisterComposite(builder);
        RegisterUi(builder, arguments.Ui);

        // One bundle: the ocean shaders, ColorPipe and GpuPyramid, cooked together
        const rhi::Device& device = *arguments.Boot.Device.Rhi().GetDevice();
        const bootstrap::DeviceFilter filter = arguments.Boot.Device.Filter();

        const loom::CompileInputs inputs
        {
            .Bundle = arguments.Boot.Content.Bundles().front().Bundle,
            .Supports = filter.Supports,
            .SupportsUser = filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(device),
            .Limits = loom::LayoutLimitsOf(device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, inputs, arguments.Sink);

        if (!compiled.has_value())
        {
            std::cerr << "Compile failed\n";
            PrintDiagnostics(arguments.Sink);
            return nullptr;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = arguments.Boot.Gpu.Resources(),
            .Device = device,
            .FrameSlots = k_FrameSlots,
            .Timestamps = loom::PassTimestamps::Off,
            .Aliasing = loom::MemoryAliasing::On,
            .Sink = arguments.Sink,
            .FrameTargets = arguments.Targets,
            .TargetSettleSeconds = k_SettleSeconds,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::cerr << std::format("Realize refused the graph: {}\n", loom::ToString(realized.error()));
            return nullptr;
        }

        return std::move(*realized);
    }

    // Returns false when a Tick fails
    bool RunFrames(WindowedBoot& boot, loom::Graph& graph, UiPass& ui, int32_t frame_limit)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        ResourceContext& resources = boot.Gpu.Resources();
        auto last = std::chrono::steady_clock::now();
        int32_t submitted = 0;

        while (!platform.ShouldWindowClose() && (frame_limit <= 0 || submitted < frame_limit))
        {
            platform.Update();
            const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
            resources.Tick(std::span{ &pending, 1u });

            ui.Begin();
            const auto now = std::chrono::steady_clock::now();
            const loom::TickResult result = graph.Tick(std::chrono::duration<double>{ now - last });
            last = now;
            ui.End();

            switch (result.Status)
            {
            case loom::TickStatus::Submitted:
                ++submitted;
                break;
            case loom::TickStatus::NoFrameSlot:
                if (!graph.WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
                {
                    std::cerr << "timed out on a frame slot\n" << graph.ExplainLastTick();
                    return false;
                }
                break;
            case loom::TickStatus::NothingToRun:
            case loom::TickStatus::TargetNotReady:
                break;
            case loom::TickStatus::Invalid:
            case loom::TickStatus::Failed:
                std::cerr << "Tick failed\n" << graph.ExplainLastTick();
                return false;
            }
        }

        return true;
    }

    int32_t Run(WindowedBoot& boot, int32_t frame_limit)
    {
        PlatformWindowSystem& platform = boot.Window.Platform();
        rhi::RhiSystem& rhiSystem = boot.Device.Rhi();
        const rhi::Device& device = *rhiSystem.GetDevice();
        Swapchain& swapchain = *platform.GetActiveSwapchain();

        const ImGuiModuleCreateInfo imguiInfo
        {
            .PlatformWindowHandle = platform.GetWindowHandle(),
            .Instance = rhiSystem.GetInstance(),
            .Device = &device,
            .VulkanApiVersion = rhiSystem.GetVulkanApiVersion(),
            .MinImageCount = swapchain.ImageCount(),
            .ImageCount = swapchain.ImageCount(),
            .ColorFormat = swapchain.ImageFormat(),
            .InstallInputCallbacks = true,
            .FontDirectory = OCEAN_FFT_FONT_DIR,
        };
        ImGuiModule imgui{ imguiInfo };
        UiPass ui{ imgui };

        loom::SwapchainFrameTarget target{ device, swapchain };
        const loom::FrameTargetBinding binding{ .Key = ocean_loom::k_Screen, .Target = target };
        loom::CollectingSink sink;

        const BuildArguments arguments
        {
            .Boot = boot,
            .Ui = ui,
            .Sink = sink,
            .Targets = std::span{ &binding, 1u },
        };

        std::unique_ptr<loom::Graph> graph = BuildGraph(arguments);

        if (!graph)
        {
            return 1;
        }

        const bool ran = RunFrames(boot, *graph, ui, frame_limit);

        // the graph waits for its own work before ImGui and the target go
        graph.reset();

        if (sink.ErrorCount() != 0)
        {
            PrintDiagnostics(sink);
            return 1;
        }

        return ran ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    const PlatformWindowCreateInfo windowInfo
    {
        "DiamondDogs - OceanFft (Loom)",
        nullptr, // the primary display
        PlatformWindowMode::Windowed,
        1600,
        900,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };

    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "OceanFftLoom",
    };

    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, k_Bundles, bootstrap::ShaderObjectNeeds(), config, windowInfo))
    {
        return 1;
    }

    return Run(boot, ParseFrameLimit(argc, argv));
}
