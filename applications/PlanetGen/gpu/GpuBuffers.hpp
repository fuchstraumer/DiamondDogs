#pragma once
#ifndef DIAMOND_DOGS_PLANET_GPU_BUFFERS_HPP
#define DIAMOND_DOGS_PLANET_GPU_BUFFERS_HPP
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "PlanetGpuErrors.hpp"
#include "ResourceTypes.hpp"
#include "RhiHandle.hpp"
#include "math/MathStorage.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class ResourceContext;

namespace rhi
{
    struct CommandBuffer;
}

namespace planet
{
    class PlanetShaders;
    struct UniformValue;

    /** Buffers by the names that the shaders use, so that WriteManifestDescriptors can bind them. Each Add call
     *  waits until ResourceContext has created the buffer and finished its upload, so a dispatch needs no wait on
     *  the transfer timeline. The destructor destroys every buffer. */
    class GpuBuffers
    {
    public:
        explicit GpuBuffers(ResourceContext& _resources) noexcept;
        ~GpuBuffers();
        GpuBuffers(const GpuBuffers&) = delete;
        GpuBuffers& operator=(const GpuBuffers&) = delete;

        /** @brief A device-local storage buffer that holds `data`. A readable buffer also gets a GPUToCPU copy, which
         *  RecordReadbackCopies fills. */
        [[nodiscard]] PlanetGpuError AddStorage(std::string_view name, std::span<const std::byte> data, bool readable);
        /** @brief A device-local zeroed storage buffer of `byte_size` bytes. */
        [[nodiscard]] PlanetGpuError AddZeroedStorage(std::string_view name, size_t byte_size, bool readable);
        /** @brief A host-visible uniform buffer that holds `data`. */
        [[nodiscard]] PlanetGpuError AddUniform(std::string_view name, std::span<const std::byte> data);
        /** @brief Packs `values` for `resource` in `entry_point` and creates a host-visible uniform buffer. */
        [[nodiscard]] PlanetGpuError AddUniformFrom(const PlanetShaders& shaders,
                                                    std::string_view entry_point,
                                                    std::string_view resource,
                                                    std::span<const UniformValue> values);
        /** @brief Overwrites an existing host-visible uniform buffer with `data`. */
        [[nodiscard]] PlanetGpuError UpdateUniform(std::string_view name, std::span<const std::byte> data) const;
        /** @brief Packs `values` for `resource` in `entry_point` and updates an existing host-visible uniform buffer. */
        [[nodiscard]] PlanetGpuError UpdateUniformFrom(const PlanetShaders& shaders,
                                                       std::string_view entry_point,
                                                       std::string_view resource,
                                                       std::span<const UniformValue> values) const;
        /** @brief A device-local 2D storage image with compute UAV and transfer source usage. */
        [[nodiscard]] PlanetGpuError AddStorageImage(std::string_view name, uint32_t width, uint32_t height, ImageFormat format);

        [[nodiscard]] const BoundResources& Bound() const noexcept;
        /** @brief Records a copy of each readable buffer to its readback buffer, with the barriers before and after. */
        void RecordReadbackCopies(rhi::CommandBuffer& command_buffer) const;
        /** @brief Records a fill of the whole storage buffer `name` with `value` in each 32-bit word. No barrier. */
        [[nodiscard]] PlanetGpuError RecordFill(rhi::CommandBuffer& command_buffer, std::string_view name, uint32_t value) const;
        /** @brief Records a copy of the whole storage buffer `source` into `destination`, which has the same size. No
         *  barrier. */
        [[nodiscard]] PlanetGpuError RecordCopy(rhi::CommandBuffer& command_buffer, std::string_view source, std::string_view destination) const;
        /** @brief The buffer `name`, for a command that takes a buffer (an indirect dispatch), or nothing. */
        [[nodiscard]] std::optional<rhi::BufferHandle> Handle(std::string_view name) const;
        /** @brief The image `name`, for a barrier or capture, or nothing. */
        [[nodiscard]] std::optional<rhi::ImageHandle> ImageHandle(std::string_view name) const;
        /** @brief The bytes of a readable buffer. Call it after the submit that holds RecordReadbackCopies finished. */
        [[nodiscard]] PlanetGpuResult<std::vector<std::byte>> Read(std::string_view name) const;
        [[nodiscard]] PlanetGpuResult<std::vector<int32_t>> ReadInts(std::string_view name) const;
        [[nodiscard]] PlanetGpuResult<std::vector<uint32_t>> ReadWords(std::string_view name) const;
        [[nodiscard]] PlanetGpuResult<std::vector<float>> ReadFloats(std::string_view name) const;
        [[nodiscard]] PlanetGpuResult<std::vector<math::Float4>> ReadFloat4s(std::string_view name) const;
        [[nodiscard]] PlanetGpuResult<std::vector<math::Int4>> ReadInt4s(std::string_view name) const;

    private:
        struct Entry
        {
            std::string Name;
            GraphicsResource Buffer;
            GraphicsResource Readback;
            size_t ByteSize{ 0u };
        };

        [[nodiscard]] PlanetGpuResult<GraphicsResource> create(const BufferCreateInfo& create_info,
                                                               std::span<const std::byte> data);
        [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

        ResourceContext& resources;
        // a deque keeps the addresses of the names, which the keys of `bound` point to
        std::deque<Entry> entries;
        BoundResources bound;
    };
}

#endif // !DIAMOND_DOGS_PLANET_GPU_BUFFERS_HPP
