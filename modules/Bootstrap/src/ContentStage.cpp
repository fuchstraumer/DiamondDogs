#include "ContentStage.hpp"
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "ShaderBundle.hpp"
#include "ShaderManifest.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    [[nodiscard]] bootstrap::BootResult ContentFailure(bootstrap::BootResultCode code,
                                                       std::string_view requested_by,
                                                       std::string subject,
                                                       std::string source);
    [[nodiscard]] bool SameRequest(const bootstrap::LoadedBundle& loaded, const bootstrap::BundleRequest& request) noexcept;
    // returns the first module of the bundle that has no code facts, or an empty view
    [[nodiscard]] std::string_view FindModuleWithoutCodeFacts(const rhi::ShaderBundle& bundle);
}

namespace bootstrap
{
    ContentStage::ContentStage() noexcept = default;

    ContentStage::~ContentStage()
    {
        Stop();
    }

    BootResult ContentStage::Start(const CoreStage& _core, std::span<const BundleRequest> app_bundles)
    {
        Stop();

        const LaunchStage& launch = _core.Launch();
        const ScopedBootSpan stageSpan(launch.Timings(), Stage::Content, BootSpanKind::Stage, ToString(Stage::Content));

        core = &_core;

        for (const BundleRequest& request : app_bundles)
        {
            BootResult opened = openBundle(request, std::string_view{});

            if (!opened.Succeeded())
            {
                Stop();
                return opened;
            }
        }

        for (const ModuleDescriptor* descriptor : launch.Selected())
        {
            for (const BundleRequest& request : descriptor->Needs.Bundles)
            {
                BootResult opened = openBundle(request, descriptor->Name);

                if (!opened.Succeeded())
                {
                    Stop();
                    return opened;
                }
            }
        }

        return SuccessIn(Stage::Content);
    }

    void ContentStage::Stop() noexcept
    {
        while (!bundles.empty())
        {
            bundles.pop_back();
        }

        core = nullptr;
    }

    bool ContentStage::Started() const noexcept
    {
        return core != nullptr;
    }

    const CoreStage& ContentStage::Core() const noexcept
    {
        return *core;
    }

    std::span<const LoadedBundle> ContentStage::Bundles() const noexcept
    {
        return bundles;
    }

    const rhi::ShaderBundle* ContentStage::FindBundle(std::string_view file_name) const noexcept
    {
        const auto found = std::ranges::find(bundles, file_name, &LoadedBundle::FileName);

        if (found == bundles.end())
        {
            return nullptr;
        }

        return &found->Bundle;
    }

    BootResult ContentStage::openBundle(const BundleRequest& request, std::string_view requested_by)
    {
        const auto sameRequest = [&request](const LoadedBundle& loaded) { return SameRequest(loaded, request); };
        const auto earlier = std::ranges::find_if(bundles, sameRequest);

        if (earlier != bundles.end() && earlier->BuildTreePath == request.BuildTreePath)
        {
            return SuccessIn(Stage::Content);
        }

        // every cook writes ShaderLibrary.ldmanifest, so two requests can reuse one name by mistake
        if (earlier != bundles.end())
        {
            return ContentFailure(BootResultCode::BundleNameConflict,
                                  requested_by,
                                  std::string{ request.FileName },
                                  std::format("\"{}\" and \"{}\"", earlier->BuildTreePath, request.BuildTreePath));
        }

        const LaunchStage& launch = core->Launch();
        const ScopedBootSpan bundleSpan(launch.Timings(), Stage::Content, BootSpanKind::Bundle, request.FileName);
        const std::filesystem::path path = launch.FindBeside(request.FileName, request.BuildTreePath);
        std::error_code error;

        if (path.empty() || !std::filesystem::is_regular_file(path, error))
        {
            return ContentFailure(BootResultCode::BundleNotFound,
                                  requested_by,
                                  std::string{ request.FileName },
                                  std::format("not beside the executable, and not at \"{}\"", request.BuildTreePath));
        }

        rhi::ShaderBundleResult<rhi::ShaderBundle> bundle = rhi::ShaderBundle::Open(path, request.Profile);

        if (!bundle.has_value())
        {
            return ContentFailure(BootResultCode::BundleInvalid,
                                  requested_by,
                                  path.string(),
                                  std::format("{} (profile \"{}\")", rhi::ToString(bundle.error()), request.Profile));
        }

        // without code facts, the bundle cannot say what the device needs
        const std::string_view blindModule = FindModuleWithoutCodeFacts(*bundle);

        if (!blindModule.empty())
        {
            return ContentFailure(BootResultCode::BundleInvalid,
                                  requested_by,
                                  path.string(),
                                  std::format("module {} has no code facts: the cook read no capabilities", blindModule));
        }

        bundles.push_back(LoadedBundle
        {
            .FileName = request.FileName,
            .Profile = request.Profile,
            .BuildTreePath = request.BuildTreePath,
            .RequestedBy = requested_by,
            .Path = path,
            .Bundle = std::move(*bundle),
        });

        return SuccessIn(Stage::Content);
    }
}

namespace
{
    bootstrap::BootResult ContentFailure(bootstrap::BootResultCode code,
                                         std::string_view requested_by,
                                         std::string subject,
                                         std::string source)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Content,
            .Module = requested_by,
            .Subject = std::move(subject),
            .Source = std::move(source),
        };
    }

    bool SameRequest(const bootstrap::LoadedBundle& loaded, const bootstrap::BundleRequest& request) noexcept
    {
        return loaded.FileName == request.FileName && loaded.Profile == request.Profile;
    }

    std::string_view FindModuleWithoutCodeFacts(const rhi::ShaderBundle& bundle)
    {
        for (const std::string_view moduleName : bundle.ModuleNames())
        {
            const rhi::ShaderBundleResult<const lodestone::manifest::EnvironmentView*> environment =
                bundle.ModuleEnvironment(moduleName);

            if (environment.has_value() && !(*environment)->HasCodeFacts())
            {
                return moduleName;
            }
        }

        return std::string_view{};
    }
}
