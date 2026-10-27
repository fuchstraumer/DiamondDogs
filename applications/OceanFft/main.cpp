#include "ArcballCamera.hpp"
#include "Device.hpp"
#include "FrameLimit.hpp"
#include "OceanScene.hpp"
#include "PlatformSystem.hpp"
#include "RhiSystem.hpp"
#include "SceneCommands.hpp"
#define WIN32_LEAN_AND_MEAN
// Windows.h first: libloaderapi.h and minwindef.h need the target architecture macros it sets
#include <Windows.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <system_error>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The OceanFft demo: a square of FFT ocean over a seabed, with an orbit camera and an ImGui panel.
//   --frames=N      render N frames and exit (for unattended runs)
//   --size=N        IFFT size: 128, 256, 512 (default) or 1024
//   --no-wave-ops   use the variant without subgroup operations, even when the device supports them
//   --validation=off  run without the validation layer even when it is installed, for the GPU timings panel
// Capture and scripted control (see SceneCommands.hpp for the command language):
//   --capture=DIR        write captures to DIR (default "captures")
//   --capture-count=K    capture K frames, the first at --capture-first=F (default 0), one every --capture-every=N
//                        (default 1). Without --frames, the demo exits after the last one
//   --dt=SECONDS         fixed simulation step per frame, so captures N frames apart are N * SECONDS apart
//   --camera=YAW,PITCH,DISTANCE[,TX,TY,TZ]   initial camera, degrees and meters
//   --script=FILE        run the commands in FILE between frames
//   --stdin              also read commands from standard input
namespace
{
    struct CommandLine
    {
        bool NoWaveOps{ false };
        bool Stdin{ false };
        std::optional<std::string> Script;
    };

    // The validation layer comes with the Vulkan SDK, not with the GPU driver. On a machine without the SDK the demo
    // runs without validation, instead of failing to create its instance
    rhi::ValidationLayers PickValidationLayers()
    {
        uint32_t count = 0u;
        std::vector<VkLayerProperties> layers;

        if (vkEnumerateInstanceLayerProperties(&count, nullptr) == VK_SUCCESS)
        {
            layers.resize(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            layers.resize(count);
        }

        const bool found = std::ranges::any_of(layers, [](const VkLayerProperties& layer)
        {
            return std::string_view{ layer.layerName } == "VK_LAYER_KHRONOS_validation";
        });

        if (!found)
        {
            std::cerr << "OceanFft: VK_LAYER_KHRONOS_validation is not installed (it comes with the Vulkan SDK), so the demo runs without validation\n";
            return rhi::ValidationLayers::None;
        }

        return rhi::ValidationLayers::Full;
    }

    // The build copies the cooked shader bundle next to the executable, so the folder runs on another machine. A
    // build-tree executable without the copy falls back to the bundle in the build directory
    std::filesystem::path FindManifest()
    {
        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        const std::filesystem::path beside = std::filesystem::path{ std::wstring_view{ modulePath.data(), length } }.parent_path() / "OceanFft.ldmanifest";
        std::error_code error;

        if (length != 0u && std::filesystem::exists(beside, error))
        {
            return beside;
        }

        return std::filesystem::path{ OCEAN_FFT_MANIFEST };
    }

    std::filesystem::path FindFonts()
    {
        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        const std::filesystem::path beside = std::filesystem::path{ std::wstring_view{ modulePath.data(), length } }.parent_path() / "fonts";
        std::error_code error;

        if (length != 0u && std::filesystem::exists(beside, error))
        {
            return beside;
        }

        return std::filesystem::path{ OCEAN_FFT_FONT_DIR };
    }

    template<typename T>
    bool ParseNumber(std::string_view text, T& value)
    {
        const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    void ParseArguments(int argc, char* argv[], OceanSceneSettings& settings, CommandLine& command_line)
    {
        constexpr float k_DegreesToRadians = std::numbers::pi_v<float> / 180.0f;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument{ argv[i] };
            if (argument == "--no-wave-ops")
            {
                command_line.NoWaveOps = true;
            }
            else if (argument == "--stdin")
            {
                command_line.Stdin = true;
            }
            else if (argument.starts_with("--script="))
            {
                command_line.Script = std::string{ argument.substr(9) };
            }
            else if (argument.starts_with("--capture="))
            {
                settings.CaptureDirectory = std::string{ argument.substr(10) };
            }
            else if (argument.starts_with("--capture-count="))
            {
                if (!ParseNumber(argument.substr(16), settings.Capture.Count)) { std::cerr << "--capture-count takes a whole number\n"; }
            }
            else if (argument.starts_with("--capture-every="))
            {
                if (!ParseNumber(argument.substr(16), settings.Capture.Every)) { std::cerr << "--capture-every takes a whole number\n"; }
            }
            else if (argument.starts_with("--capture-first="))
            {
                if (!ParseNumber(argument.substr(16), settings.Capture.First)) { std::cerr << "--capture-first takes a whole number\n"; }
            }
            else if (argument.starts_with("--dt="))
            {
                if (!ParseNumber(argument.substr(5), settings.FixedTimeStep)) { std::cerr << "--dt takes seconds\n"; }
            }
            else if (argument.starts_with("--camera="))
            {
                const std::expected<SceneCommand, std::string> parsed = SceneCommand::Parse(std::string{ "camera " } + std::string{ argument.substr(9) });
                if (!parsed.has_value())
                {
                    std::cerr << "--camera: " << parsed.error() << "\n";
                    continue;
                }
                ArcballCamera::State state{};
                state.Yaw = parsed->Values[0] * k_DegreesToRadians;
                state.Pitch = parsed->Values[1] * k_DegreesToRadians;
                state.Distance = parsed->Values[2];
                if (parsed->ValueCount == 6u)
                {
                    state.Target = { parsed->Values[3], parsed->Values[4], parsed->Values[5] };
                }
                settings.InitialCamera = state;
            }
            else if (argument.starts_with("--size="))
            {
                const std::string_view value = argument.substr(7);
                uint32_t size = 0u;
                if (std::from_chars(value.data(), value.data() + value.size(), size).ec == std::errc{} &&
                    (size == 128u || size == 256u || size == 512u || size == 1024u))
                {
                    settings.Simulation.Size = size;
                }
                else
                {
                    std::cerr << "--size must be 128, 256, 512 or 1024\n";
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    rhi::RhiSystemCreateInfo rhiCreateInfo{};
    rhiCreateInfo.ApplicationName = "OceanFft";
    rhiCreateInfo.VkVersion = rhi::ApiVersion::Latest;
    // the instance comes before the other arguments are parsed, so this one is looked for here
    const bool validationOff = std::ranges::contains(std::span{ argv, static_cast<size_t>(argc) }, std::string_view{ "--validation=off" },
        [](const char* argument) { return std::string_view{ argument }; });
    rhiCreateInfo.ValidationLevel = validationOff ? rhi::ValidationLayers::None : PickValidationLayers();
    rhiCreateInfo.RequiredInstanceExtensions =
    {
        "VK_EXT_debug_utils",
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
        "VK_KHR_get_surface_capabilities2",
        "VK_EXT_swapchain_colorspace"
    };
    rhiCreateInfo.RequiredDeviceExtensions =
    {
        "VK_KHR_swapchain",
        "VK_EXT_shader_object"
    };
    std::unique_ptr<rhi::RhiSystem> rhiSystem = std::make_unique<rhi::RhiSystem>(rhiCreateInfo);

    const PlatformWindowCreateInfo platformCreateInfo
    {
        "DiamondDogs - OceanFft",
        nullptr, // use primary display
        PlatformWindowMode::Windowed,
        1920,
        1080,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };
    std::unique_ptr<PlatformWindowSystem> platformSystem = std::make_unique<PlatformWindowSystem>(platformCreateInfo);
    platformSystem->CreateDefaultSwapchain(rhiSystem->GetInstance(), rhiSystem->GetDevice());

    OceanSceneSettings settings{};
    settings.ManifestPath = FindManifest();
    settings.FontDirectory = FindFonts();
    settings.ValidationEnabled = rhiCreateInfo.ValidationLevel != rhi::ValidationLayers::None;
    CommandLine commandLine{};
    ParseArguments(argc, argv, settings, commandLine);
    settings.Simulation.WaveSize = commandLine.NoWaveOps ? 0u : ocean::PickWaveSize(*rhiSystem->GetDevice());

    SceneCommands commands;
    if (commandLine.Script.has_value())
    {
        if (const std::expected<void, std::string> loaded = commands.LoadScript(*commandLine.Script); !loaded.has_value())
        {
            std::cerr << "--script: " << loaded.error() << "\n";
            return 1;
        }
    }
    
    if (commandLine.Stdin)
    {
        commands.StartStdin();
    }

    if (commandLine.Script.has_value() || commandLine.Stdin)
    {
        settings.Commands = &commands;
    }

    int exitCode = 0;
    {
        OceanScene scene(rhiSystem.get(), platformSystem.get(), settings);
        scene.Initialize(nullptr);
        int32_t frameLimit = ParseFrameLimit(argc, argv);
        // a capture schedule without a frame limit ends with its last capture
        if (frameLimit < 0 && settings.Capture.Count > 0u && !commandLine.Stdin && !commandLine.Script.has_value())
        {
            frameLimit = static_cast<int32_t>(settings.Capture.LastFrame() + 1u);
        }
        int32_t framesRendered = 0;
        while (!platformSystem->ShouldWindowClose() && !scene.QuitRequested() && (frameLimit < 0 || framesRendered < frameLimit))
        {
            platformSystem->Update();
            scene.Render(nullptr);
            ++framesRendered;
        }
        scene.Destroy();
    }

    platformSystem->DestroySwapchain();

    return exitCode;
}
