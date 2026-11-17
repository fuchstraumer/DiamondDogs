#pragma once
#ifndef DIAMOND_DOGS_PLANET_GPU_BUFFERS_HPP
#define DIAMOND_DOGS_PLANET_GPU_BUFFERS_HPP
#include "ManifestDescriptors.hpp"
#include "PlanetGpuErrors.hpp"
#include "ResourceTypes.hpp"
#include <cstddef>
#include <deque>
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
        /** @brief A host-visible uniform buffer that holds `data`. */
        [[nodiscard]] PlanetGpuError AddUniform(std::string_view name, std::span<const std::byte> data);

        [[nodiscard]] const BoundResources& Bound() const noexcept;
        /** @brief Records a copy of each readable buffer to its readback buffer, with the barriers before and after. */
        void RecordReadbackCopies(rhi::CommandBuffer& command_buffer) const;
        /** @brief The bytes of a readable buffer. Call it after the submit that holds RecordReadbackCopies finished. */
        [[nodiscard]] PlanetGpuResult<std::vector<std::byte>> Read(std::string_view name) const;

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
