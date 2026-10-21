#pragma once
#ifndef RHI_SYSTEM_SHADER_LIBRARY_HPP
#define RHI_SYSTEM_SHADER_LIBRARY_HPP
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "VariantKey.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace lodestone
{
    class ManifestIndex;
}

namespace rhi
{
    class ShaderProgram;
    struct ManifestProgramImpl;
    struct ShaderLibraryImpl;

    /** @brief Failures of opening a cooked Lodestone bundle, or of building a program from one. */
    enum class ShaderLibraryError : uint8_t
    {
        Invalid = 0,
        Success,
        ManifestFileUnreadable,
        ManifestInvalid,
        ProfileNotInManifest,
        ProfileNotSpirv,
        ModuleNotInManifest,
        EnvironmentInvalid,
        EntryPointNotInModule,
        EntryPointNotInVariant,
        NoEntryPointsRequested,
        LayoutMergeFailed,
        UnsupportedResourcePlacement,
        UnsupportedBindingKind,
        DescriptorSetLayoutCreationFailed,
        PipelineLayoutCreationFailed,
        ShaderProgramCreationFailed,
    };

    [[nodiscard]] std::string_view ToString(ShaderLibraryError error) noexcept;

    /** @brief Creation options the manifest does not carry. They apply to every compute entry point of the program. */
    struct ManifestProgramOptions
    {
        /** @brief See ShaderBinaryOptions: for variants whose wave ops assume lane == local thread index.
         *  The caller knows which variant it picked, so it sets these from the same axis values. */
        uint32_t RequiredSubgroupSize{ 0u };
        bool RequireFullSubgroups{ false };
    };

    template<typename T>
    using ShaderLibraryResult = std::expected<T, ShaderLibraryError>;

    /** @brief A program built from the entry points of one cooked variant, with the layouts its resources need.
     *  It owns the descriptor set layouts and the pipeline layout. Bind descriptor sets with `PipelineLayout()`:
     *  shader objects have no pipeline, but vkCmdBindDescriptorSets still needs a compatible layout. */
    class ManifestProgram
    {
    public:
        explicit ManifestProgram(std::unique_ptr<ManifestProgramImpl>&& _impl) noexcept;
        ~ManifestProgram();
        ManifestProgram(const ManifestProgram&) = delete;
        ManifestProgram& operator=(const ManifestProgram&) = delete;
        ManifestProgram(ManifestProgram&& other) noexcept;
        ManifestProgram& operator=(ManifestProgram&& other) noexcept;

        [[nodiscard]] ShaderProgram& Program() const noexcept;
        [[nodiscard]] PipelineLayoutHandle PipelineLayout() const noexcept;
        /** @brief One layout for each descriptor set, indexed by set number. A set the program doesn't use
         *  is an empty layout, because Vulkan requires the set layouts of a pipeline layout to be contiguous. */
        [[nodiscard]] std::span<const DescriptorSetLayoutHandle> SetLayouts() const noexcept;
        /** @brief Returns the workgroup size of the first entry point, as reflected by the cook. */
        [[nodiscard]] std::array<uint32_t, 3> Workgroup() const noexcept;
        /** @brief The one push constant range of the program, from offset 0, for every stage that reads push constants:
         *  the entry point `uniform` parameters of ordinary data. Empty when no stage has any. */
        [[nodiscard]] std::optional<PushConstantRange> PushConstants() const noexcept;

    private:
        std::unique_ptr<ManifestProgramImpl> impl;
    };

    /** @brief One cooked Lodestone shader bundle, opened for one target profile (`spirv` for this renderer).
     *  This replaces runtime Slang compilation for cooked shaders: the cook compiled, reflected, and
     *  validated every variant, so this only reads the manifest and creates API objects from it. */
    class ShaderLibrary
    {
    public:
        explicit ShaderLibrary(std::unique_ptr<ShaderLibraryImpl>&& _impl) noexcept;
        ~ShaderLibrary();
        ShaderLibrary(const ShaderLibrary&) = delete;
        ShaderLibrary& operator=(const ShaderLibrary&) = delete;
        ShaderLibrary(ShaderLibrary&& other) noexcept = default;
        ShaderLibrary& operator=(ShaderLibrary&& other) noexcept = default;

        /** @brief Reads the bundle at `manifest_path` and validates it. */
        [[nodiscard]] static ShaderLibraryResult<std::unique_ptr<ShaderLibrary>> Open(
            const std::filesystem::path& manifest_path,
            std::string_view profile_name = "spirv");

        /** @brief Returns the query surface of one module, used to pick a variant by its axis values.
         *  The returned index lives as long as this library. */
        [[nodiscard]] ShaderLibraryResult<const lodestone::ManifestIndex*> ModuleIndex(std::string_view module_name) const;

        /** @brief Builds a program from the named entry points of one variant. The layouts are merged across the
         *  entry points, so a resource that two stages read appears once with both stage bits. */
        [[nodiscard]] ShaderLibraryResult<std::unique_ptr<ManifestProgram>> CreateProgram(
            const DeviceHandle& device,
            std::string_view module_name,
            lodestone::VariantKey variant,
            std::span<const std::string_view> entry_points,
            const ManifestProgramOptions& options = {}) const;

    private:
        std::unique_ptr<ShaderLibraryImpl> impl;
    };

} // namespace rhi

#endif // !RHI_SYSTEM_SHADER_LIBRARY_HPP
