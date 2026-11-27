#include "BootResult.hpp"
#include "DeviceCapability.hpp"
#include "DeviceConfig.hpp"
#include "DeviceFilter.hpp"
#include "DeviceRequirements.hpp"
#include "RhiContextWebGpu.hpp"
#include "ShaderManifestDevice.hpp"
#include "WebGpuDeviceStage.hpp"

#include <emscripten/emscripten.h>

#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <vector>

// The WebGPU device stage's rules, in the browser: a Required need that the adapter lacks fails the boot and names the
// need, a Wanted need is enabled when the adapter has it, Declined leaves a Wanted need off, a limit above the adapter
// fails, and the device gets the adapter's own limits. Each case starts the stage, steps it until it is ready or has
// failed, and checks the result. A pass is window.__ddTestPassed true.

namespace
{
    // what one case expects once the stage settles. `check` returns an empty string on a pass
    struct BootCase
    {
        std::string_view Name;
        bootstrap::DeviceRequirements Requirements;
        bootstrap::DeviceConfig Config;
        std::string (*Check)(const bootstrap::WebGpuDeviceStage& stage);
    };

    struct AppState
    {
        std::vector<BootCase> Cases;
        int32_t Current{ 0 };
        bool Started{ false };
        int32_t Failures{ 0 };
        std::string Log;
        bootstrap::WebGpuDeviceStage Stage;
    };

    [[nodiscard]] std::vector<BootCase> MakeCases();
    [[nodiscard]] bootstrap::DeviceRequirements RequirementsOf(std::vector<bootstrap::Need> needs);
    [[nodiscard]] std::string ExpectFailure(const bootstrap::WebGpuDeviceStage& stage, std::string_view subject);
    [[nodiscard]] std::string CheckAdapterLimits(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckRequiredDrawIndirectCount(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckWantedDrawIndirectCount(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckWantedF16(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckDeclinedF16(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckVulkanExtension(const bootstrap::WebGpuDeviceStage& stage);
    [[nodiscard]] std::string CheckLimitAboveAdapter(const bootstrap::WebGpuDeviceStage& stage);
    void Finish(AppState& state);
    void MainLoopTick(void* user_data);
}

int main()
{
    static AppState state;

    state.Cases = MakeCases();

    EM_ASM(
        window['__ddTestDone'] = false;
        window['__ddTestPassed'] = false;
    );

    emscripten_set_main_loop_arg(&MainLoopTick, &state, 0, 1);
    return 0;
}

namespace
{
    std::vector<BootCase> MakeCases()
    {
        bootstrap::DeviceConfig declinesF16{};

        declinesF16.Declined.emplace_back("f16");

        bootstrap::DeviceRequirements aboveAdapter{};

        aboveAdapter.RequiredLimits.MaxWorkgroupSizeX = 1u << 30u;

        std::vector<BootCase> cases;

        cases.push_back(BootCase{ "AdapterLimits", bootstrap::DeviceRequirements{}, bootstrap::DeviceConfig{}, &CheckAdapterLimits });
        cases.push_back(BootCase
        {
            "RequiredDrawIndirectCount",
            RequirementsOf({ bootstrap::NeutralNeed(bootstrap::DeviceCapability::DrawIndirectCount, bootstrap::NeedLevel::Required) }),
            bootstrap::DeviceConfig{},
            &CheckRequiredDrawIndirectCount,
        });
        cases.push_back(BootCase
        {
            "WantedDrawIndirectCount",
            RequirementsOf({ bootstrap::NeutralNeed(bootstrap::DeviceCapability::DrawIndirectCount, bootstrap::NeedLevel::Wanted) }),
            bootstrap::DeviceConfig{},
            &CheckWantedDrawIndirectCount,
        });
        cases.push_back(BootCase
        {
            "WantedF16",
            RequirementsOf({ bootstrap::Need{ .Name = "f16", .Kind = bootstrap::NeedKind::Capability, .Level = bootstrap::NeedLevel::Wanted } }),
            bootstrap::DeviceConfig{},
            &CheckWantedF16,
        });
        cases.push_back(BootCase
        {
            "DeclinedF16",
            RequirementsOf({ bootstrap::Need{ .Name = "f16", .Kind = bootstrap::NeedKind::Capability, .Level = bootstrap::NeedLevel::Wanted } }),
            declinesF16,
            &CheckDeclinedF16,
        });
        cases.push_back(BootCase
        {
            "RequiredVulkanExtension",
            RequirementsOf({ bootstrap::Need{ .Name = "VK_KHR_swapchain", .Kind = bootstrap::NeedKind::DeviceExtension, .Level = bootstrap::NeedLevel::Required } }),
            bootstrap::DeviceConfig{},
            &CheckVulkanExtension,
        });
        cases.push_back(BootCase{ "LimitAboveAdapter", aboveAdapter, bootstrap::DeviceConfig{}, &CheckLimitAboveAdapter });
        return cases;
    }

    bootstrap::DeviceRequirements RequirementsOf(std::vector<bootstrap::Need> needs)
    {
        bootstrap::DeviceRequirements requirements{};

        for (const bootstrap::Need& need : needs)
        {
            switch (need.Kind)
            {
            case bootstrap::NeedKind::DeviceExtension:
                requirements.DeviceExtensions.push_back(need);
                break;
            case bootstrap::NeedKind::Capability:
                requirements.Capabilities.push_back(need);
                break;
            case bootstrap::NeedKind::NeutralCapability:
                requirements.NeutralCapabilities.push_back(need);
                break;
            default:
                requirements.DeviceFeatures.push_back(need);
                break;
            }
        }

        return requirements;
    }

    std::string ExpectFailure(const bootstrap::WebGpuDeviceStage& stage, std::string_view subject)
    {
        if (!stage.HasFailed())
        {
            return std::format("the stage started; it must fail on {}", subject);
        }

        const bootstrap::BootResult& failure = stage.Failure();

        if (failure.Code != bootstrap::BootResultCode::NoDeviceMeetsRequirements || failure.Subject != subject)
        {
            return std::format("wrong failure: {}", bootstrap::FormatFailure(failure));
        }

        return std::string{};
    }

    std::string CheckAdapterLimits(const bootstrap::WebGpuDeviceStage& stage)
    {
        if (!stage.IsReady())
        {
            return bootstrap::FormatFailure(stage.Failure());
        }

        // without requiredLimits, the device gets WebGPU's defaults (16384 bytes, 256 invocations), below the adapter
        const rhi::WebGpuRhiContextView view = rhi::UnwrapWebGpuRhiContext(stage.Context());
        const lodestone::manifest::DeviceLimits& device = stage.Filter().Limits;
        wgpu::Limits adapter{};

        if (view.Adapter.GetLimits(&adapter) != wgpu::Status::Success)
        {
            return "the adapter's limits could not be read";
        }

        if (device.MaxWorkgroupMemoryBytes != adapter.maxComputeWorkgroupStorageSize ||
            device.MaxInvocationsPerWorkgroup != adapter.maxComputeInvocationsPerWorkgroup)
        {
            return std::format("device limits {} bytes, {} invocations; adapter {} bytes, {} invocations",
                               device.MaxWorkgroupMemoryBytes,
                               device.MaxInvocationsPerWorkgroup,
                               adapter.maxComputeWorkgroupStorageSize,
                               adapter.maxComputeInvocationsPerWorkgroup);
        }

        return std::string{};
    }

    // Chrome offers multi-draw-indirect only with the unsafe-WebGPU flag. Without it the boot fails and names the
    // neutral capability; with it the device has the capability
    std::string CheckRequiredDrawIndirectCount(const bootstrap::WebGpuDeviceStage& stage)
    {
        if (stage.IsReady())
        {
            return bootstrap::HasCapability(stage.Filter(), bootstrap::DeviceCapability::DrawIndirectCount) ?
                   std::string{} :
                   std::string{ "the stage started without the Required capability" };
        }

        return ExpectFailure(stage, "DrawIndirectCount");
    }

    std::string CheckWantedDrawIndirectCount(const bootstrap::WebGpuDeviceStage& stage)
    {
        if (!stage.IsReady())
        {
            return std::format("a Wanted need failed the boot: {}", bootstrap::FormatFailure(stage.Failure()));
        }

        return std::string{};
    }

    std::string CheckWantedF16(const bootstrap::WebGpuDeviceStage& stage)
    {
        if (!stage.IsReady())
        {
            return bootstrap::FormatFailure(stage.Failure());
        }

        const bootstrap::DeviceFilter filter = stage.Filter();

        // the RTX 4080 has shader-f16. Another adapter may not, and then the name must be unsupported, not an error
        std::println("WantedF16: f16 supported: {}", filter.Supports(filter.User, "f16"));
        return std::string{};
    }

    std::string CheckDeclinedF16(const bootstrap::WebGpuDeviceStage& stage)
    {
        if (!stage.IsReady())
        {
            return bootstrap::FormatFailure(stage.Failure());
        }

        const bootstrap::DeviceFilter filter = stage.Filter();

        if (filter.Supports(filter.User, "f16"))
        {
            return "f16 was declined, and the filter still supports it";
        }

        return std::string{};
    }

    std::string CheckVulkanExtension(const bootstrap::WebGpuDeviceStage& stage)
    {
        return ExpectFailure(stage, "VK_KHR_swapchain");
    }

    std::string CheckLimitAboveAdapter(const bootstrap::WebGpuDeviceStage& stage)
    {
        return ExpectFailure(stage, "maxComputeWorkgroupSizeX");
    }

    void Finish(AppState& state)
    {
        const bool passed = state.Failures == 0;
        const std::string summary = std::format("{}WebGpuBootTest: {}/{} passed",
                                                state.Log,
                                                std::ssize(state.Cases) - state.Failures,
                                                std::ssize(state.Cases));

        std::println("{}", summary);

        EM_ASM(
            {
                window['__ddTestDone'] = true;
                window['__ddTestPassed'] = Boolean($0);
                window['__ddTestSummary'] = UTF8ToString($1);
            },
            passed ? 1 : 0,
            summary.c_str());

        emscripten_cancel_main_loop();
    }

    void MainLoopTick(void* user_data)
    {
        AppState& state = *static_cast<AppState*>(user_data);

        if (state.Current >= std::ssize(state.Cases))
        {
            Finish(state);
            return;
        }

        const BootCase& current = state.Cases[static_cast<size_t>(state.Current)];

        if (!state.Started)
        {
            state.Stage.StartHeadless(current.Requirements, current.Config);
            state.Started = true;
        }

        state.Stage.Step();

        if (!state.Stage.IsReady() && !state.Stage.HasFailed())
        {
            return;
        }

        const std::string problem = current.Check(state.Stage);
        const bool passed = problem.empty();

        state.Log += std::format("[{}] {}{}\n", passed ? "PASS" : "FAIL", current.Name, passed ? "" : ": " + problem);
        state.Failures += passed ? 0 : 1;
        state.Stage.Stop();
        state.Started = false;
        ++state.Current;
    }
}
