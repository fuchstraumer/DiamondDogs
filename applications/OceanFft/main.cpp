#include "ArcballCamera.hpp"
#include "BootResult.hpp"
#include "ColorPipe.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceStage.hpp"
#include "FrameLimit.hpp"
#include "GpuPyramid.hpp"
#include "GpuStage.hpp"
#include "ImGuiModule.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "OceanScene.hpp"
#include "ResourceTypes.hpp"
#include "OceanSimulation.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "RhiSystem.hpp"
#include "SceneCommands.hpp"
#include "ShaderBundle.hpp"
#include "SpineNeeds.hpp"
#include "ToolServer.hpp"
#include "WindowStage.hpp"
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The OceanFft demo: a square of FFT ocean over a seabed, with an orbit camera and an ImGui panel.
//   --frames=N      render N frames and exit (for unattended runs)
//   --size=N        IFFT size: 128, 256 or 512 (default)
//   --no-wave-ops   use the variant without subgroup operations, even when the device supports them
//   --unfused       run the separate tick, IFFT and resolve passes instead of the two fused kernels
//   --validation=off  run without the validation layer even when it is installed, for the GPU timings panel
//   --msaa=N         highest MSAA sample count: 1, 2, 4 or 8 (default 4). The scene uses the highest count up to N that
//                    the device supports. 1 turns MSAA off
//   --aniso=N        anisotropic filtering of the surface: 1, 2, 4, 8 or 16 (default 8). 1 turns it off
//   --grid-resolution=N   quads per edge of the center tile (default 384). Each ring halves the one inside it, down to 6,
//                    so N must be 6 times a power of two (192, 384, ...)
// Capture and scripted control (see SceneCommands.hpp for the command language):
//   --capture=DIR        write captures to DIR (default "captures")
//   --capture-count=K    capture K frames, the first at --capture-first=F (default 0), one every --capture-every=N
//                        (default 1). Without --frames, the demo exits after the last one
//   --dt=SECONDS         fixed simulation step per frame, so captures N frames apart are N * SECONDS apart
//   --camera=YAW,PITCH,DISTANCE[,TX,TY,TZ]   initial camera, degrees and meters
//   --script=FILE        run the commands in FILE between frames
//   --stdin              also read commands from standard input
//   --tool-port=N        serve agent tools on 127.0.0.1:N. 0 lets the system choose, and the demo prints the port
// Window:
//   --window=WxH         the size of the window (default 1920x1080)
//   --borderless         a borderless window that covers the primary display, at the display's size
namespace
{
    constexpr std::array<uint32_t, 4> k_MsaaChoices{ 1u, 2u, 4u, 8u };
    constexpr std::array<uint32_t, 5> k_AnisotropyChoices{ 1u, 2u, 4u, 8u, 16u };
    // ToolServer::Start refuses this, so a --tool-port that does not parse stops the demo
    constexpr int32_t k_RefusedToolPort = -1;

    struct CommandLine
    {
        bool NoWaveOps{ false };
        bool Unfused{ false };
        bool Stdin{ false };
        std::optional<std::string> Script;
        std::optional<int32_t> ToolPort;
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

    // The build copies the cooked shader bundles next to the executable, so the folder runs on another machine. A
    // build-tree executable without the copies falls back to the bundles in the build directory
    constexpr std::string_view k_OceanBundle{ "OceanFft.ldmanifest" };
    constexpr std::string_view k_ColorPipeBundle{ "ColorPipe.ldmanifest" };
    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = k_OceanBundle,
            .BuildTreePath = OCEAN_FFT_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
        bootstrap::BundleRequest
        {
            .FileName = k_ColorPipeBundle,
            .BuildTreePath = OCEAN_FFT_COLOR_PIPE_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // the ocean and seabed samplers filter anisotropically when the device has it
    constexpr bootstrap::Need k_AnisotropyNeed
    {
        .Name = k_SamplerAnisotropyFeature,
        .Kind = bootstrap::NeedKind::DeviceFeature,
        .Level = bootstrap::NeedLevel::Wanted,
    };

    // the OceanFft bundle lists UniformAndStorageBuffer16BitAccess, which implicitly declares this one, and the cook
    // does not list implied capabilities yet
    constexpr bootstrap::Need k_StorageBuffer16BitNeed
    {
        .Name = "StorageBuffer16BitAccess",
        .Kind = bootstrap::NeedKind::Capability,
        .Level = bootstrap::NeedLevel::Required,
    };

    // prints a boot failure to stderr
    [[nodiscard]] bool Booted(const bootstrap::BootResult& result)
    {
        if (!result.Succeeded())
        {
            std::cerr << bootstrap::FormatFailure(result) << "\n";
        }

        return result.Succeeded();
    }

    // where the content stage found the bundle that k_Bundles names `file_name`. ContentStage::Start fails unless it
    // opened every request
    [[nodiscard]] std::filesystem::path BundlePath(const bootstrap::ContentStage& content, std::string_view file_name)
    {
        const auto found = std::ranges::find(content.Bundles(), file_name, &bootstrap::LoadedBundle::FileName);

        return found->Path;
    }

    // the spine needs, then what the demo's own code uses: shader objects, GPU timers, dynamic rendering for the
    // scene, the color pipe and ImGui, and the wave-op variants, the fast mip pipeline and anisotropic filtering when
    // the device has them
    [[nodiscard]] std::vector<bootstrap::Need> AppNeeds(const bootstrap::WindowStage& window_stage)
    {
        std::vector<bootstrap::Need> needs = bootstrap::SpineNeeds(window_stage);

        needs.append_range(bootstrap::ShaderObjectNeeds());
        needs.append_range(bootstrap::QueryPoolNeeds());
        needs.append_range(bootstrap::FeatureNeeds(color::k_ColorPipeFeatures, bootstrap::NeedLevel::Required));
        needs.append_range(bootstrap::FeatureNeeds(k_ImGuiModuleFeatures, bootstrap::NeedLevel::Required));
        needs.append_range(bootstrap::FeatureNeeds(ocean::k_WaveOpFeatures, bootstrap::NeedLevel::Wanted));
        needs.append_range(bootstrap::FeatureNeeds(pyramid::k_FastPipelineFeatures, bootstrap::NeedLevel::Wanted));
        needs.push_back(k_AnisotropyNeed);
        needs.push_back(k_StorageBuffer16BitNeed);
        return needs;
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

    // The build copies the packed foam textures next to the executable. A build-tree executable without the copy
    // falls back to the folder that the packer wrote
    std::filesystem::path FindFoamMaterialDirectory()
    {
        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        const std::filesystem::path besideDirectory = std::filesystem::path{ std::wstring_view{ modulePath.data(), length } }.parent_path();
        const std::filesystem::path besideFile = besideDirectory / (std::string{ OCEAN_FFT_FOAM_NAME } + "_Surface.ktx");
        std::error_code error;

        if (length != 0u && std::filesystem::exists(besideFile, error))
        {
            return besideDirectory;
        }

        return std::filesystem::path{ OCEAN_FFT_FOAM_DIR };
    }

    std::filesystem::path FindSeabedMaterialDirectory()
    {
        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        const std::filesystem::path besideDirectory = std::filesystem::path{ std::wstring_view{ modulePath.data(), length } }.parent_path();
        const std::filesystem::path besideFile = besideDirectory / (std::string{ OCEAN_FFT_SEABED_NAME } + "_Albedo.ktx");
        std::error_code error;

        if (length != 0u && std::filesystem::exists(besideFile, error))
        {
            return besideDirectory;
        }

        return std::filesystem::path{ OCEAN_FFT_SEABED_DIR };
    }

    template<typename T>
    bool ParseNumber(std::string_view text, T& value)
    {
        const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    struct WindowArguments
    {
        uint32_t Width{ 1920u };
        uint32_t Height{ 1080u };
        bool Borderless{ false };
        bool Valid{ true };
    };

    // the window exists before the other arguments are parsed, so these are looked for first
    WindowArguments ParseWindowArguments(int argc, char* argv[])
    {
        WindowArguments window{};

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument{ argv[index] };

            if (argument == "--borderless")
            {
                window.Borderless = true;
                continue;
            }

            if (!argument.starts_with("--window="))
            {
                continue;
            }

            const std::string_view size = argument.substr(9);
            const size_t separator = size.find('x');
            const bool parsed = separator != std::string_view::npos &&
                                ParseNumber(size.substr(0, separator), window.Width) &&
                                ParseNumber(size.substr(separator + 1), window.Height);

            window.Valid = parsed && window.Width >= 64u && window.Height >= 64u && window.Width <= 16384u &&
                           window.Height <= 16384u;
        }

        return window;
    }

    // true when the text is a number from choices, which is what each flag takes
    template<size_t N>
    bool ParseChoice(std::string_view text, const std::array<uint32_t, N>& choices, uint32_t& value)
    {
        uint32_t parsed = 0u;
        if (!ParseNumber(text, parsed))
        {
            return false;
        }

        if (!std::ranges::contains(choices, parsed))
        {
            return false;
        }

        value = parsed;
        return true;
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
            else if (argument == "--unfused")
            {
                command_line.Unfused = true;
            }
            else if (argument == "--stdin")
            {
                command_line.Stdin = true;
            }
            else if (argument.starts_with("--script=") || argument.starts_with("--commands="))
            {
                command_line.Script = std::string{ argument.starts_with("--script=") ? argument.substr(9) : argument.substr(11) };
            }
            else if (argument.starts_with("--tool-port="))
            {
                int32_t port = 0;

                command_line.ToolPort = ParseNumber(argument.substr(12), port) ? port : k_RefusedToolPort;
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
                    (size == 128u || size == 256u || size == 512u))
                {
                    settings.Simulation.Size = size;
                }
                else
                {
                    std::cerr << "--size must be 128, 256 or 512\n";
                }
            }
            else if (argument.starts_with("--msaa="))
            {
                uint32_t samples = 0u;
                if (ParseChoice(argument.substr(7), k_MsaaChoices, samples))
                {
                    settings.MaxMsaaSamples = samples;
                }
                else
                {
                    std::cerr << "--msaa must be 1, 2, 4 or 8\n";
                }
            }
            else if (argument.starts_with("--aniso="))
            {
                uint32_t anisotropy = 0u;
                if (ParseChoice(argument.substr(8), k_AnisotropyChoices, anisotropy))
                {
                    settings.MaxAnisotropy = static_cast<float>(anisotropy);
                }
                else
                {
                    std::cerr << "--aniso must be 1, 2, 4, 8 or 16\n";
                }
            }
            else if (argument.starts_with("--grid-resolution="))
            {
                uint32_t resolution = 0u;
                if (ParseNumber(argument.substr(18), resolution) && GridStitches(resolution, settings.GridRings, settings.GridMinResolution))
                {
                    settings.GridResolution = resolution;
                }
                else
                {
                    std::cerr << "--grid-resolution must be 6 times a power of two (for example 192 or 384), so the rings stitch\n";
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    bootstrap::LaunchStage launch;

    if (!Booted(launch.Start(std::span{ argv, static_cast<size_t>(argc) }, std::span<const bootstrap::ModuleDescriptor>{}, bootstrap::Stage::Gpu)))
    {
        return 1;
    }

    bootstrap::CoreStage core;

    if (!Booted(core.Start(launch)))
    {
        return 1;
    }

    // each bundle is looked for beside the executable first, so the package folder runs on another machine
    bootstrap::ContentStage content;

    if (!Booted(content.Start(core, k_Bundles)))
    {
        return 1;
    }

    const WindowArguments window = ParseWindowArguments(argc, argv);

    if (!window.Valid)
    {
        std::cerr << "--window takes WIDTHxHEIGHT, each from 64 to 16384, for example --window=3440x1440\n";
        return 1;
    }

    const PlatformWindowCreateInfo platformCreateInfo
    {
        "DiamondDogs - OceanFft",
        nullptr, // use primary display
        window.Borderless ? PlatformWindowMode::FullScreenWindowed : PlatformWindowMode::Windowed,
        window.Width,
        window.Height,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };
    bootstrap::WindowStage windowStage;

    if (!Booted(windowStage.Start(content, platformCreateInfo)))
    {
        return 1;
    }

    const std::vector<bootstrap::Need> needs = AppNeeds(windowStage);
    const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(content, needs);
    // a config file's Device section can change these, for example to decline a Wanted need
    const bool validationOff = std::ranges::contains(launch.Arguments(), std::string_view{ "--validation=off" });
    bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = validationOff ? rhi::ValidationLayers::None : PickValidationLayers(),
        .ApplicationName = "OceanFft",
    };

    if (!Booted(bootstrap::ReadDeviceConfig(launch, config)))
    {
        return 1;
    }

    bootstrap::DeviceStage deviceStage;

    if (!Booted(deviceStage.Start(content, requirements, config, windowStage)))
    {
        return 1;
    }

    bootstrap::GpuStage gpu;

    if (!Booted(gpu.Start(deviceStage, windowStage)))
    {
        return 1;
    }

    rhi::RhiSystem& rhiSystem = deviceStage.Rhi();
    PlatformWindowSystem& platformSystem = windowStage.Platform();
    OceanSceneSettings settings{};
    settings.ManifestPath = BundlePath(content, k_OceanBundle);
    settings.ColorPipeManifestPath = BundlePath(content, k_ColorPipeBundle);
    settings.FontDirectory = FindFonts();
    settings.FoamMaterialDirectory = FindFoamMaterialDirectory();
    settings.FoamMaterialName = OCEAN_FFT_FOAM_NAME;
    settings.SeabedMaterialDirectory = FindSeabedMaterialDirectory();
    settings.SeabedMaterialName = OCEAN_FFT_SEABED_NAME;
    settings.ValidationEnabled = config.Validation != rhi::ValidationLayers::None;
    CommandLine commandLine{};
    ParseArguments(argc, argv, settings, commandLine);
    settings.Simulation.WaveSize = commandLine.NoWaveOps ? 0u : ocean::PickWaveSize(*rhiSystem.GetDevice());
    settings.Simulation.FusedKernels = !commandLine.Unfused;

    SceneCommands commands;
    if (commandLine.Script.has_value())
    {
        const ScriptResult loaded = commands.LoadScript(*commandLine.Script);

        if (loaded != ScriptResult::Success)
        {
            std::cerr << "--script: " << ToString(loaded) << ": " << *commandLine.Script << "\n";
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

    agent::ToolServer toolServer;

    if (commandLine.ToolPort.has_value())
    {
        const agent::ToolServerResult started = toolServer.Start(*commandLine.ToolPort);

        if (started != agent::ToolServerResult::Success)
        {
            std::cerr << "--tool-port: " << agent::ToString(started) << ": " << *commandLine.ToolPort << "\n";
            return 1;
        }

        std::cout << "tool server listening on 127.0.0.1:" << toolServer.Port() << std::endl;
        settings.Tools = &toolServer;
    }

    int exitCode = 0;
    {
        OceanScene scene(&rhiSystem, &platformSystem, gpu.Resources(), settings);
        scene.Initialize(nullptr);
        int32_t frameLimit = ParseFrameLimit(argc, argv);
        // a capture schedule without a frame limit ends with its last capture
        if (frameLimit < 0 && settings.Capture.Count > 0u && !commandLine.Stdin && !commandLine.Script.has_value())
        {
            frameLimit = static_cast<int32_t>(settings.Capture.LastFrame() + 1u);
        }
        int32_t framesRendered = 0;
        while (!platformSystem.ShouldWindowClose() && !scene.QuitRequested() && (frameLimit < 0 || framesRendered < frameLimit))
        {
            platformSystem.Update();
            scene.Render(nullptr);
            ++framesRendered;
        }
        scene.Destroy();
    }

    // the stages stop in reverse: the swapchain, the surface and the device, then the window
    return exitCode;
}
