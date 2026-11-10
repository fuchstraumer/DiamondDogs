#pragma once
#ifndef DIAMOND_DOGS_RHI_SYSTEM_SHADER_BUNDLE_HPP
#define DIAMOND_DOGS_RHI_SYSTEM_SHADER_BUNDLE_HPP
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace lodestone
{
    class ManifestIndex;
}

namespace lodestone::manifest
{
    class EnvironmentView;
    struct Profile;
}

namespace rhi
{
    enum class ShaderBundleError : uint8_t
    {
        Invalid = 0,
        Success = 1,
        FileUnreadable = 2,
        BundleInvalid = 3,
        ProfileNotInBundle = 4,
        EnvironmentInvalid = 5,
        ModuleNotInBundle = 6,
    };

    template<typename T>
    using ShaderBundleResult = std::expected<T, ShaderBundleError>;

    [[nodiscard]] std::string_view ToString(ShaderBundleError error) noexcept;

    struct ShaderBundleImpl;

    /** @brief One cooked Lodestone bundle, opened for one profile, with no graphics device.
     *  Compile-time readers (Loom's Resolve and Bind) query it, and ShaderLibrary creates Vulkan objects from it.
     *  Every lookup is by module name. A later reader that maps the file and opens each module on first use can
     *  then replace this eager one without a change to any caller. */
    class ShaderBundle
    {
    public:
        ~ShaderBundle();
        ShaderBundle(ShaderBundle&& other) noexcept;
        ShaderBundle& operator=(ShaderBundle&& other) noexcept;
        ShaderBundle(const ShaderBundle&) = delete;
        ShaderBundle& operator=(const ShaderBundle&) = delete;

        /** @brief Reads the file, then opens the bytes as FromBytes does. */
        [[nodiscard]] static ShaderBundleResult<ShaderBundle> Open(const std::filesystem::path& file_path,
                                                                   std::string_view profile_name);
        /** @brief Takes ownership of the bytes. Move them in to avoid a copy. */
        [[nodiscard]] static ShaderBundleResult<ShaderBundle> FromBytes(std::vector<std::byte> bytes,
                                                                        std::string_view profile_name);

        [[nodiscard]] const lodestone::manifest::Profile& Profile() const noexcept;
        /** @brief Every module in the bundle, for a "did you mean" on a module name that is not found. */
        [[nodiscard]] std::vector<std::string_view> ModuleNames() const;

        /** @brief The views point into this bundle. They live as long as it does, and a move of the bundle keeps
         *  them valid: the bytes and the views stay where they are, behind the impl pointer. */
        [[nodiscard]] ShaderBundleResult<const lodestone::manifest::EnvironmentView*> ModuleEnvironment(
            std::string_view module_name) const;
        [[nodiscard]] ShaderBundleResult<const lodestone::ManifestIndex*> ModuleIndex(std::string_view module_name) const;

    private:
        explicit ShaderBundle(std::unique_ptr<ShaderBundleImpl>&& _impl) noexcept;

        std::unique_ptr<ShaderBundleImpl> impl;
    };

}

#endif // !DIAMOND_DOGS_RHI_SYSTEM_SHADER_BUNDLE_HPP
