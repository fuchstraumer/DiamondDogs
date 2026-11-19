#pragma once
#ifndef DIAMOND_DOGS_PLANET_SHADERS_HPP
#define DIAMOND_DOGS_PLANET_SHADERS_HPP
#include "ManifestDescriptors.hpp"
#include "PlanetGpuErrors.hpp"
#include "RhiHandle.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rhi
{
    class Device;
    class ShaderLibrary;
    struct CommandBuffer;
}

namespace bootstrap
{
    struct DeviceFilter;
}

namespace planet
{
    /** The Slang module that Lodestone cooks from assets/shaders/compute/Planet/PlanetLab.slang. */
    inline constexpr std::string_view k_PlanetModuleName{ "PlanetLab" };
    /** The Capability axis that selects the storage of the crust flags (Buffers.slang). */
    inline constexpr std::string_view k_CrustFlagAxisName{ "CRUST_FLAG_BYTES" };

    /** How the crust flags (alive, continental) are stored: one byte or one 32-bit word for each cell. */
    enum class CrustFlagStorage : uint8_t
    {
        Invalid = 0,
        Bytes,
        Words,
    };

    /** The variant that PlanetShaders::Create selects. BestForDevice takes Bytes when the device has 8-bit storage. */
    enum class CrustFlagChoice : uint8_t
    {
        Invalid = 0,
        BestForDevice,
        Bytes,
        Words,
    };

    [[nodiscard]] std::string_view CrustFlagBufferNames(CrustFlagStorage storage) noexcept;
    [[nodiscard]] std::string_view ToString(CrustFlagStorage storage) noexcept;

    /** One member of a uniform buffer, as raw bits. UniformFloat and UniformUint make it. */
    struct UniformValue
    {
        std::string_view Member;
        uint32_t Bits{ 0u };
    };

    [[nodiscard]] UniformValue UniformFloat(std::string_view member, float value) noexcept;
    [[nodiscard]] UniformValue UniformUint(std::string_view member, uint32_t value) noexcept;

    struct PlanetShadersImpl;
    struct PassBindingsImpl;

    /** A descriptor set for each entry point of PlanetShaders, written from one set of named buffers. It owns the
     *  descriptor pool. */
    class PassBindings
    {
    public:
        explicit PassBindings(std::unique_ptr<PassBindingsImpl>&& _impl) noexcept;
        ~PassBindings();
        PassBindings(const PassBindings&) = delete;
        PassBindings& operator=(const PassBindings&) = delete;

    private:
        friend class PlanetShaders;
        std::unique_ptr<PassBindingsImpl> impl;
    };

    /** The compute programs of the PlanetLab module, one for each entry point, with the binding layout that the cook
     *  reflected. All the programs use one variant. */
    class PlanetShaders
    {
    public:
        /** @brief Creates a program for each name in `entry_points`, from the variant that `crust_flags` selects among
         *  the variants that the device can run. Fails with NoVariantFitsDevice when the device cannot run the chosen
         *  storage. The names must be literals, or live longer than the returned object. */
        [[nodiscard]] static PlanetGpuResult<std::unique_ptr<PlanetShaders>> Create(
            rhi::Device& device,
            const rhi::ShaderLibrary& library,
            const bootstrap::DeviceFilter& filter,
            CrustFlagChoice crust_flags,
            std::span<const std::string_view> entry_points);

        explicit PlanetShaders(std::unique_ptr<PlanetShadersImpl>&& _impl) noexcept;
        ~PlanetShaders();
        PlanetShaders(const PlanetShaders&) = delete;
        PlanetShaders& operator=(const PlanetShaders&) = delete;

        /** @brief The crust flag storage of the selected variant. The host creates the flag buffers in this form. */
        [[nodiscard]] CrustFlagStorage CrustFlags() const noexcept;
        /** @brief The bytes of the uniform buffer `resource` as `entry_point` reads it. The size and the member offsets
         *  come from the manifest. A member name matches when the reflected name ends with it. */
        [[nodiscard]] PlanetGpuResult<std::vector<std::byte>> UniformBytes(std::string_view entry_point,
                                                                           std::string_view resource,
                                                                           std::span<const UniformValue> values) const;
        /** @brief A descriptor set for each entry point, written from `resources` by the reflected names. */
        [[nodiscard]] PlanetGpuResult<std::unique_ptr<PassBindings>> Bind(const BoundResources& resources) const;
        /** @brief Binds the program and its set, and dispatches enough workgroups for `thread_count` threads in x. */
        void RecordDispatch(rhi::CommandBuffer& command_buffer,
                            const PassBindings& bindings,
                            std::string_view entry_point,
                            uint32_t thread_count) const;
        /** @brief Binds the program and its set, and dispatches enough workgroups for `width` x `height` threads in xy. */
        void RecordDispatch2D(rhi::CommandBuffer& command_buffer,
                              const PassBindings& bindings,
                              std::string_view entry_point,
                              uint32_t width,
                              uint32_t height) const;
        /** @brief Binds the program and its set, and dispatches the group counts at `offset` in `arguments`, which an
         *  earlier pass wrote. RecordIndirectBarrier must come between that pass and this dispatch. */
        void RecordDispatchIndirect(rhi::CommandBuffer& command_buffer,
                                    const PassBindings& bindings,
                                    std::string_view entry_point,
                                    const rhi::BufferHandle& arguments,
                                    uint64_t offset) const;

    private:
        std::unique_ptr<PlanetShadersImpl> impl;
    };

    /** @brief A barrier from one compute dispatch to the next: storage writes before storage reads and writes. */
    void RecordComputeBarrier(rhi::CommandBuffer& command_buffer);

    /** @brief RecordComputeBarrier, and the storage writes also before the reads of indirect group counts. */
    void RecordIndirectBarrier(rhi::CommandBuffer& command_buffer);
}

#endif // !DIAMOND_DOGS_PLANET_SHADERS_HPP
