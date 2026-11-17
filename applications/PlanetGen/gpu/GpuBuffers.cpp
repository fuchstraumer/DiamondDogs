#include "GpuBuffers.hpp"
#include "CommandBuffer.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "RhiFlags.hpp"
#include "RhiTypes.hpp"
#include <algorithm>
#include <cstring>
#include <print>
#include <vector>

namespace planet
{
    namespace
    {
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    }

    GpuBuffers::GpuBuffers(ResourceContext& _resources) noexcept :
        resources{ _resources }
    {}

    GpuBuffers::~GpuBuffers()
    {
        for (const Entry& entry : entries)
        {
            for (const GraphicsResource& resource : { entry.Buffer, entry.Readback })
            {
                if (!resource)
                {
                    continue;
                }

                rhi::OperationFuture destroyed = resources.DestroyResource(resource);

                if (resources.Wait(destroyed, k_TimeoutNs) != rhi::FutureState::Complete)
                {
                    std::println(stderr, "GpuBuffers: {} was not destroyed", entry.Name);
                }
            }
        }
    }

    PlanetGpuError GpuBuffers::AddStorage(std::string_view name, std::span<const std::byte> data, bool readable)
    {
        BufferCreateInfo createInfo{};
        createInfo.Domain = ResourceDomain::GPUOnly;
        createInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        createInfo.Size = data.size();
        createInfo.Name = name;
        PlanetGpuResult<GraphicsResource> buffer = create(createInfo, data);

        if (!buffer.has_value())
        {
            return buffer.error();
        }

        Entry& entry = entries.emplace_back(Entry{ std::string{ name }, *buffer, GraphicsResource::Null(), data.size() });
        bound.emplace(entry.Name, BoundResource{ .Resource = entry.Buffer });

        if (!readable)
        {
            return PlanetGpuError::Success;
        }

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = data.size();
        PlanetGpuResult<GraphicsResource> readback = create(readbackInfo, {});

        if (!readback.has_value())
        {
            return readback.error();
        }

        entry.Readback = *readback;
        return PlanetGpuError::Success;
    }

    PlanetGpuError GpuBuffers::AddUniform(std::string_view name, std::span<const std::byte> data)
    {
        BufferCreateInfo createInfo{};
        createInfo.Domain = ResourceDomain::CPUToGPU;
        createInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        createInfo.Size = data.size();
        createInfo.Name = name;
        PlanetGpuResult<GraphicsResource> buffer = create(createInfo, data);

        if (!buffer.has_value())
        {
            return buffer.error();
        }

        Entry& entry = entries.emplace_back(Entry{ std::string{ name }, *buffer, GraphicsResource::Null(), data.size() });
        bound.emplace(entry.Name, BoundResource{ .Resource = entry.Buffer });
        return PlanetGpuError::Success;
    }

    const BoundResources& GpuBuffers::Bound() const noexcept
    {
        return bound;
    }

    void GpuBuffers::RecordReadbackCopies(rhi::CommandBuffer& command_buffer) const
    {
        const rhi::GlobalBarrier toCopy
        {
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::Copy, rhi::Access::TransferRead
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &toCopy, 1 }, {}, {} });

        for (const Entry& entry : entries)
        {
            if (!entry.Readback)
            {
                continue;
            }

            const rhi::BufferCopy region{ 0u, 0u, entry.ByteSize };
            command_buffer.CopyBuffer(rhi::BufferHandle{ entry.Buffer.VkHandle }, rhi::BufferHandle{ entry.Readback.VkHandle },
                                      std::span{ &region, 1 });
        }

        const rhi::GlobalBarrier toHost
        {
            rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
            rhi::PipelineStage::Host, rhi::Access::HostRead
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
    }

    PlanetGpuResult<std::vector<std::byte>> GpuBuffers::Read(std::string_view name) const
    {
        const Entry* entry = find(name);

        if (entry == nullptr || !entry->Readback)
        {
            std::println(stderr, "GpuBuffers: {} is not a readable buffer", name);
            return std::unexpected(PlanetGpuError::BufferNotFound);
        }

        rhi::ResourceFuture<void*> mapped = resources.MapBuffer(entry->Readback, entry->ByteSize, 0u);

        if (resources.Wait(mapped, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: MapBuffer failed on {}", name);
            return std::unexpected(PlanetGpuError::ReadbackFailed);
        }

        std::vector<std::byte> bytes(entry->ByteSize);
        std::memcpy(bytes.data(), mapped.Get(), entry->ByteSize);
        rhi::OperationFuture unmapped = resources.UnmapBuffer(entry->Readback, entry->ByteSize, 0u);

        if (resources.Wait(unmapped, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: UnmapBuffer failed on {}", name);
            return std::unexpected(PlanetGpuError::ReadbackFailed);
        }

        return bytes;
    }

    PlanetGpuResult<GraphicsResource> GpuBuffers::create(const BufferCreateInfo& create_info, std::span<const std::byte> data)
    {
        const RhiBufferResourceData initial{ data, 0u };
        std::span<const RhiBufferResourceData> initialData{};

        if (!data.empty())
        {
            initialData = std::span{ &initial, 1u };
        }

        // Complete means created and, for a device-local buffer, uploaded. The check waits on the CPU so that its
        // submits need no wait on the transfer timeline
        rhi::ResourceFuture<GraphicsResource> created = resources.CreateBuffer(create_info, initialData);

        if (resources.Wait(created, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: ResourceContext did not create {} ({} bytes)", create_info.Name, create_info.Size);
            return std::unexpected(PlanetGpuError::BufferCreationFailed);
        }

        return created.Get();
    }

    const GpuBuffers::Entry* GpuBuffers::find(std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(entries, name, &Entry::Name);

        if (found == entries.end())
        {
            return nullptr;
        }

        return &*found;
    }
}
