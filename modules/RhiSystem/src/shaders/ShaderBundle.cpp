#include "ShaderBundle.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace manifest = lodestone::manifest;

    [[nodiscard]] rhi::ShaderBundleResult<std::vector<std::byte>> ReadBundleBytes(const std::filesystem::path& file_path)
    {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);

        if (!file.is_open())
        {
            return std::unexpected(rhi::ShaderBundleError::FileUnreadable);
        }

        const std::streamsize fileSize = file.tellg();

        if (fileSize <= 0)
        {
            return std::unexpected(rhi::ShaderBundleError::FileUnreadable);
        }

        std::vector<std::byte> bytes(static_cast<size_t>(fileSize));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize))
        {
            return std::unexpected(rhi::ShaderBundleError::FileUnreadable);
        }

        return bytes;
    }

}

namespace rhi
{
    struct ShaderBundleModule
    {
        // views returned by the environment keep a pointer to it, so it lives on the heap and never moves
        std::unique_ptr<manifest::EnvironmentView> Environment;
        // an index is built over one environment, so each module needs its own (that scopes the queries, too)
        std::unique_ptr<lodestone::ManifestIndex> Index;
    };

    struct ShaderBundleImpl
    {
        // every view points into these bytes: the impl sits behind a pointer, so they never move
        std::vector<std::byte> Bytes;
        manifest::BundleView Bundle;
        uint32_t ProfileIndex{ 0u };
        std::unordered_map<std::string, ShaderBundleModule> Modules;

        [[nodiscard]] ShaderBundleResult<const ShaderBundleModule*> FindModule(std::string_view module_name) const;
    };

    ShaderBundleResult<const ShaderBundleModule*> ShaderBundleImpl::FindModule(std::string_view module_name) const
    {
        const auto moduleIter = Modules.find(std::string{ module_name });

        if (moduleIter == Modules.end())
        {
            return std::unexpected(ShaderBundleError::ModuleNotInBundle);
        }

        return &moduleIter->second;
    }

    std::string_view ToString(ShaderBundleError error) noexcept
    {
        switch (error)
        {
        case ShaderBundleError::Invalid: return "Invalid";
        case ShaderBundleError::Success: return "Success";
        case ShaderBundleError::FileUnreadable: return "FileUnreadable";
        case ShaderBundleError::BundleInvalid: return "BundleInvalid";
        case ShaderBundleError::ProfileNotInBundle: return "ProfileNotInBundle";
        case ShaderBundleError::EnvironmentInvalid: return "EnvironmentInvalid";
        case ShaderBundleError::ModuleNotInBundle: return "ModuleNotInBundle";
        }

        return "Unknown ShaderBundleError";
    }

    ShaderBundle::ShaderBundle(std::unique_ptr<ShaderBundleImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {
    }

    ShaderBundle::~ShaderBundle() = default;
    ShaderBundle::ShaderBundle(ShaderBundle&& other) noexcept = default;
    ShaderBundle& ShaderBundle::operator=(ShaderBundle&& other) noexcept = default;

    ShaderBundleResult<ShaderBundle> ShaderBundle::Open(const std::filesystem::path& file_path, std::string_view profile_name)
    {
        ShaderBundleResult<std::vector<std::byte>> bytes = ReadBundleBytes(file_path);

        if (!bytes.has_value())
        {
            return std::unexpected(bytes.error());
        }

        return FromBytes(std::move(*bytes), profile_name);
    }

    ShaderBundleResult<ShaderBundle> ShaderBundle::FromBytes(std::vector<std::byte> bytes, std::string_view profile_name)
    {
        auto bundleImpl = std::make_unique<ShaderBundleImpl>();
        bundleImpl->Bytes = std::move(bytes);

        manifest::ManifestResult<manifest::BundleView> bundle = manifest::BundleView::Open(bundleImpl->Bytes);

        if (!bundle.has_value())
        {
            return std::unexpected(ShaderBundleError::BundleInvalid);
        }

        bundleImpl->Bundle = *bundle;
        const int32_t profileIndex = bundleImpl->Bundle.FindProfile(profile_name);

        if (profileIndex < 0)
        {
            return std::unexpected(ShaderBundleError::ProfileNotInBundle);
        }

        bundleImpl->ProfileIndex = static_cast<uint32_t>(profileIndex);

        // Every module is opened now. A reader that opens each module on first use replaces this loop only.
        for (uint32_t moduleIndex = 0; moduleIndex < bundleImpl->Bundle.ModuleCount(); ++moduleIndex)
        {
            manifest::ManifestResult<manifest::EnvironmentView> environment =
                bundleImpl->Bundle.OpenEnvironment(bundleImpl->ProfileIndex, moduleIndex);

            if (!environment.has_value())
            {
                return std::unexpected(ShaderBundleError::EnvironmentInvalid);
            }

            ShaderBundleModule bundleModule;
            bundleModule.Environment = std::make_unique<manifest::EnvironmentView>(*environment);
            bundleModule.Index = std::make_unique<lodestone::ManifestIndex>(*bundleModule.Environment);
            const std::string_view moduleName = bundleImpl->Bundle.Module(moduleIndex).Name();
            bundleImpl->Modules.emplace(std::string{ moduleName }, std::move(bundleModule));
        }

        return ShaderBundle{ std::move(bundleImpl) };
    }

    const lodestone::manifest::Profile& ShaderBundle::Profile() const noexcept
    {
        return impl->Bundle.Profiles()[impl->ProfileIndex];
    }

    std::vector<std::string_view> ShaderBundle::ModuleNames() const
    {
        std::vector<std::string_view> names;
        names.reserve(impl->Bundle.ModuleCount());

        for (uint32_t moduleIndex = 0; moduleIndex < impl->Bundle.ModuleCount(); ++moduleIndex)
        {
            names.push_back(impl->Bundle.Module(moduleIndex).Name());
        }

        return names;
    }

    ShaderBundleResult<const lodestone::manifest::EnvironmentView*> ShaderBundle::ModuleEnvironment(
        std::string_view module_name) const
    {
        const ShaderBundleResult<const ShaderBundleModule*> bundleModule = impl->FindModule(module_name);

        if (!bundleModule.has_value())
        {
            return std::unexpected(bundleModule.error());
        }

        return (*bundleModule)->Environment.get();
    }

    ShaderBundleResult<const lodestone::ManifestIndex*> ShaderBundle::ModuleIndex(std::string_view module_name) const
    {
        const ShaderBundleResult<const ShaderBundleModule*> bundleModule = impl->FindModule(module_name);

        if (!bundleModule.has_value())
        {
            return std::unexpected(bundleModule.error());
        }

        return (*bundleModule)->Index.get();
    }

}
