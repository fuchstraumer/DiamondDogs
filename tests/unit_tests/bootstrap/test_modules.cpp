#include "test_modules.hpp"
#include "BootResult.hpp"
#include "CoreStage.hpp"
#include "ShaderBundle.hpp"
#include <array>
#include <fstream>
#include <memory>

namespace
{
    [[nodiscard]] bootstrap::BootResult StartAlpha(const bootstrap::CoreStartContext& context,
                                                   bootstrap::ModuleInstance& instance);
    [[nodiscard]] bootstrap::BootResult StartBeta(const bootstrap::CoreStartContext& context,
                                                  bootstrap::ModuleInstance& instance);
    [[nodiscard]] bootstrap::BootResult StartGamma(const bootstrap::GpuStartContext& context,
                                                   bootstrap::ModuleInstance& instance);
    [[nodiscard]] bootstrap::BootResult StartUndeclared(const bootstrap::CoreStartContext& context,
                                                        bootstrap::ModuleInstance& instance);
    [[nodiscard]] bootstrap::BootResult StartFailing(const bootstrap::CoreStartContext& context,
                                                     bootstrap::ModuleInstance& instance);
    [[nodiscard]] bootstrap::BootResult StartOwnsNothing(const bootstrap::CoreStartContext& context,
                                                         bootstrap::ModuleInstance& instance);

    constexpr std::array<std::string_view, 1> k_DependsOnAlpha{ "alpha" };
    constexpr std::array<std::string_view, 1> k_DependsOnGamma{ "gamma" };

    constexpr std::string_view k_BootNeedsPath{ BOOT_NEEDS_BUNDLE };
    constexpr std::string_view k_HashFillPath{ HASH_FILL_BUNDLE };

    constexpr std::array<std::string_view, 1> k_WantedInstanceExtensions{ "VK_EXT_debug_utils" };
    constexpr std::array<std::string_view, 2> k_RequiredDeviceExtensions{ "VK_KHR_swapchain", "VK_EXT_shader_object" };
    constexpr std::array<std::string_view, 1> k_RequiredCapabilities{ "GroupNonUniformArithmetic" };

    constexpr bootstrap::BundleRequest k_BootNeeds
    {
        .FileName = "BootNeeds.ldmanifest",
        .BuildTreePath = k_BootNeedsPath,
        .Profile = rhi::k_SpirvProfileName,
    };

    constexpr bootstrap::BundleRequest k_HashFill
    {
        .FileName = "HashFill.ldmanifest",
        .BuildTreePath = k_HashFillPath,
        .Profile = rhi::k_SpirvProfileName,
    };

    constexpr bootstrap::BundleRequest k_Missing
    {
        .FileName = "missing.ldbundle",
        .BuildTreePath = "C:/nowhere/missing.ldbundle",
        .Profile = rhi::k_SpirvProfileName,
    };

    constexpr std::array<bootstrap::BundleRequest, 2> k_HashFillUserBundles{ k_HashFill, k_BootNeeds };
    constexpr std::array<bootstrap::BundleRequest, 1> k_MissingBundles{ k_Missing };
}

namespace bootstrap_tests
{
    Alpha::~Alpha()
    {
        DestroyLog().emplace_back("alpha");
    }

    Beta::~Beta()
    {
        DestroyLog().emplace_back("beta");
    }

    const bootstrap::ModuleKey<Alpha> k_AlphaKey{ "alpha" };
    const bootstrap::ModuleKey<Beta> k_BetaKey{ "beta" };

    const bootstrap::ModuleDescriptor k_AlphaModule
    {
        .Name = "alpha",
        .StartCore = &StartAlpha,
    };

    const bootstrap::ModuleDescriptor k_BetaModule
    {
        .Name = "beta",
        .DependsOn = k_DependsOnAlpha,
        .StartCore = &StartBeta,
    };

    const bootstrap::ModuleDescriptor k_GammaModule
    {
        .Name = "gamma",
        .StartGpu = &StartGamma,
    };

    const bootstrap::ModuleDescriptor k_UndeclaredModule
    {
        .Name = "undeclared",
        .StartCore = &StartUndeclared,
    };

    const bootstrap::ModuleDescriptor k_FailingModule
    {
        .Name = "failing",
        .StartCore = &StartFailing,
    };

    const bootstrap::ModuleDescriptor k_OwnsNothingModule
    {
        .Name = "owns_nothing",
        .StartCore = &StartOwnsNothing,
    };

    const bootstrap::ModuleDescriptor k_NeedsGammaModule
    {
        .Name = "needs_gamma",
        .DependsOn = k_DependsOnGamma,
        .StartCore = &StartAlpha,
    };

    const bootstrap::BundleRequest k_BootNeedsRequest{ k_BootNeeds };
    const bootstrap::BundleRequest k_HashFillRequest{ k_HashFill };
    const bootstrap::BundleRequest k_MissingRequest{ k_Missing };

    const bootstrap::ModuleDescriptor k_HashFillUserModule
    {
        .Name = "hash_fill_user",
        .Needs = bootstrap::ModuleNeeds{ .Bundles = k_HashFillUserBundles },
        .StartGpu = &StartGamma,
    };

    const bootstrap::ModuleDescriptor k_ExtensionsModule
    {
        .Name = "extensions",
        .Needs = bootstrap::ModuleNeeds
        {
            .WantedInstanceExtensions = k_WantedInstanceExtensions,
            .RequiredDeviceExtensions = k_RequiredDeviceExtensions,
            .RequiredCapabilities = k_RequiredCapabilities,
        },
        .StartGpu = &StartGamma,
    };

    const bootstrap::ModuleDescriptor k_MissingBundleModule
    {
        .Name = "missing_bundle",
        .Needs = bootstrap::ModuleNeeds{ .Bundles = k_MissingBundles },
        .StartGpu = &StartGamma,
    };

    std::vector<std::string>& DestroyLog()
    {
        static std::vector<std::string> log;

        return log;
    }

    TestArguments::TestArguments(std::initializer_list<std::string_view> values)
    {
        storage.emplace_back("bootstrap_tests.exe");

        for (const std::string_view value : values)
        {
            storage.emplace_back(value);
        }

        for (std::string& argument : storage)
        {
            pointers.push_back(argument.data());
        }
    }

    std::span<char* const> TestArguments::Span() const noexcept
    {
        return pointers;
    }

    std::filesystem::path WriteConfig(std::string_view file_name, std::string_view text)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return path;
    }
}

namespace
{
    bootstrap::BootResult StartAlpha(const bootstrap::CoreStartContext&, bootstrap::ModuleInstance& instance)
    {
        instance.Own(bootstrap_tests::k_AlphaKey, std::make_unique<bootstrap_tests::Alpha>());
        return bootstrap::SuccessIn(bootstrap::Stage::Core);
    }

    bootstrap::BootResult StartBeta(const bootstrap::CoreStartContext& context, bootstrap::ModuleInstance& instance)
    {
        bootstrap::BootResult result = bootstrap::SuccessIn(bootstrap::Stage::Core);
        bootstrap_tests::Alpha* alpha = context.Require(bootstrap_tests::k_AlphaKey, result);

        if (!result.Succeeded())
        {
            return result;
        }

        std::unique_ptr<bootstrap_tests::Beta> beta = std::make_unique<bootstrap_tests::Beta>();

        beta->Dependency = alpha;
        instance.Own(bootstrap_tests::k_BetaKey, std::move(beta));
        return result;
    }

    bootstrap::BootResult StartGamma(const bootstrap::GpuStartContext&, bootstrap::ModuleInstance&)
    {
        return bootstrap::SuccessIn(bootstrap::Stage::Gpu);
    }

    bootstrap::BootResult StartUndeclared(const bootstrap::CoreStartContext& context, bootstrap::ModuleInstance&)
    {
        bootstrap::BootResult result = bootstrap::SuccessIn(bootstrap::Stage::Core);
        bootstrap_tests::Alpha* alpha = context.Require(bootstrap_tests::k_AlphaKey, result);

        if (alpha != nullptr)
        {
            // never reached: Require refuses an undeclared key
            alpha->Value = 0;
        }

        return result;
    }

    bootstrap::BootResult StartFailing(const bootstrap::CoreStartContext&, bootstrap::ModuleInstance&)
    {
        return bootstrap::BootResult{ .Code = bootstrap::BootResultCode::ModuleStartFailed, .Subject = "planted" };
    }

    bootstrap::BootResult StartOwnsNothing(const bootstrap::CoreStartContext&, bootstrap::ModuleInstance&)
    {
        return bootstrap::SuccessIn(bootstrap::Stage::Core);
    }
}
