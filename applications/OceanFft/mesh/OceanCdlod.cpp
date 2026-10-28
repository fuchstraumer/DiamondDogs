#include "OceanCdlod.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "GpuTimers.hpp"
#include "ManifestDescriptors.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace ocean
{
    namespace
    {
        constexpr std::string_view k_ModuleName{ "Quadtree" };
        constexpr std::string_view k_UpdateEntryPoint{ "QuadtreeUpdateCS" };
        constexpr std::string_view k_BuildEntryPoint{ "QuadtreeBuildIndirectArgsCS" };
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

        constexpr uint32_t k_GridResolution = 64u;
        constexpr uint32_t k_GridVerticesAcross = 65u;
        constexpr uint32_t k_TotalVertices = k_GridVerticesAcross * k_GridVerticesAcross;
        constexpr uint32_t k_TotalIndices = k_GridResolution * k_GridResolution * 6u;

        struct QuadtreePushConstants
        {
            uint32_t CurrentLevel{ 0u };
        };

        template<typename T>
        void PushConstants(rhi::CommandBuffer& command_buffer, const rhi::ManifestProgram& program, const T& value)
        {
            const std::optional<rhi::PushConstantRange> range = program.PushConstants();
            if (range.has_value())
            {
                command_buffer.PushConstants(program.PipelineLayout(), range->StageFlags, 0u,
                    std::as_bytes(std::span{ &value, 1 }).first(std::min<size_t>(sizeof(T), range->Size)));
            }
        }

        [[nodiscard]] uint32_t PickWaveSize(const rhi::Device& device) noexcept
        {
            const VkPhysicalDevice physicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
            VkPhysicalDeviceVulkan13Properties properties13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
            VkPhysicalDeviceSubgroupProperties subgroupProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &properties13 };
            VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &subgroupProperties };
            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

            VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13 };
            vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

            constexpr VkSubgroupFeatureFlags k_RequiredOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
            const uint32_t waveSize = subgroupProperties.subgroupSize;
            const bool sizeIsAnAxisValue = waveSize == 16u || waveSize == 32u || waveSize == 64u || waveSize == 128u;
            const bool supported =
                sizeIsAnAxisValue &&
                (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
                (subgroupProperties.supportedOperations & k_RequiredOperations) == k_RequiredOperations &&
                features13.subgroupSizeControl == VK_TRUE &&
                features13.computeFullSubgroups == VK_TRUE &&
                (properties13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
                properties13.minSubgroupSize <= waveSize && waveSize <= properties13.maxSubgroupSize;
            return supported ? waveSize : 0u;
        }

        [[nodiscard]] std::vector<OceanPatchVertex> GenerateGridVertices()
        {
            std::vector<OceanPatchVertex> vertices;
            vertices.reserve(k_TotalVertices);

            for (uint32_t y = 0u; y < k_GridVerticesAcross; ++y)
            {
                const float v = static_cast<float>(y) / static_cast<float>(k_GridResolution);

                for (uint32_t x = 0u; x < k_GridVerticesAcross; ++x)
                {
                    const float u = static_cast<float>(x) / static_cast<float>(k_GridResolution);
                    vertices.push_back(OceanPatchVertex{ .GridUv = { u, v } });
                }
            }

            return vertices;
        }

        [[nodiscard]] std::vector<uint32_t> GenerateGridIndices()
        {
            std::vector<uint32_t> indices;
            indices.reserve(k_TotalIndices);

            for (uint32_t y = 0u; y < k_GridResolution; ++y)
            {
                for (uint32_t x = 0u; x < k_GridResolution; ++x)
                {
                    const uint32_t v0 = y * k_GridVerticesAcross + x;
                    const uint32_t v1 = y * k_GridVerticesAcross + (x + 1u);
                    const uint32_t v2 = (y + 1u) * k_GridVerticesAcross + x;
                    const uint32_t v3 = (y + 1u) * k_GridVerticesAcross + (x + 1u);

                    indices.push_back(v0);
                    indices.push_back(v2);
                    indices.push_back(v1);

                    indices.push_back(v1);
                    indices.push_back(v2);
                    indices.push_back(v3);
                }
            }

            return indices;
        }
    }

    struct OceanCdlodImpl
    {
        ~OceanCdlodImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        const rhi::ShaderLibrary* Library{ nullptr };
        OceanCdlodSettings Settings;

        GraphicsResource GridVertexBuffer;
        GraphicsResource GridIndexBuffer;
        GraphicsResource NodesA;
        GraphicsResource NodesB;
        GraphicsResource NodeCounts;
        GraphicsResource SelectedPatches;
        GraphicsResource IndirectDrawArgs;

        std::vector<GraphicsResource> ParamsBuffers;
        std::vector<void*> ParamsPointers;

        const lodestone::ManifestIndex* ManifestIndexPtr{ nullptr };
        lodestone::VariantKey UpdateVariant;
        lodestone::VariantKey BuildVariant;

        std::unique_ptr<rhi::ManifestProgram> UpdateProgram;
        std::unique_ptr<rhi::ManifestProgram> BuildProgram;

        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        std::vector<VkDescriptorSet> UpdateSetsPing;
        std::vector<VkDescriptorSet> UpdateSetsPong;
        std::vector<VkDescriptorSet> BuildSets;

        uint32_t AllocatedMaxNodes{ 0u };
        uint32_t AllocatedMaxSelectedPatches{ 0u };
    };

    OceanCdlodImpl::~OceanCdlodImpl()
    {
        if (Device == nullptr)
        {
            return;
        }

        const VkDevice vkDevice = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDevice, DescriptorPool, nullptr);
        }

        if (Resources != nullptr)
        {
            for (size_t i = 0u; i < ParamsBuffers.size(); ++i)
            {
                if (ParamsBuffers[i])
                {
                    std::shared_ptr<MessageReply> reply = Resources->DestroyResource(ParamsBuffers[i]);
                    if (reply != nullptr)
                    {
                        reply->WaitForCompletion(k_TimeoutNs);
                    }
                }
            }

            auto destroyResourceWait = [this](GraphicsResource& res)
            {
                if (res)
                {
                    std::shared_ptr<MessageReply> reply = Resources->DestroyResource(res);
                    if (reply != nullptr)
                    {
                        reply->WaitForCompletion(k_TimeoutNs);
                    }
                }
            };

            destroyResourceWait(IndirectDrawArgs);
            destroyResourceWait(SelectedPatches);
            destroyResourceWait(NodeCounts);
            destroyResourceWait(NodesB);
            destroyResourceWait(NodesA);
            destroyResourceWait(GridIndexBuffer);
            destroyResourceWait(GridVertexBuffer);
        }
    }

    std::expected<std::unique_ptr<OceanCdlod>, std::string> OceanCdlod::Create(
        rhi::Device& device,
        ResourceContext& resources,
        const rhi::ShaderLibrary& library,
        uint32_t frame_count,
        const OceanCdlodSettings& settings)
    {
        if (frame_count == 0u)
        {
            return std::unexpected(std::string{ "frame_count must be greater than zero" });
        }

        auto impl = std::make_unique<OceanCdlodImpl>();
        impl->Device = &device;
        impl->Resources = &resources;
        impl->Library = &library;
        impl->Settings = settings;
        const VkDevice vkDevice = device.Handle().As<VkDevice>();

        const std::vector<OceanPatchVertex> vertices = GenerateGridVertices();
        const RhiBufferResourceData vertexData{ std::as_bytes(std::span{ vertices }), 0u };
        BufferCreateInfo vertexInfo{};
        vertexInfo.Domain = ResourceDomain::GPUOnly;
        vertexInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        vertexInfo.Usage = BufferUsageBits::StorageBuffer | BufferUsageBits::TransferDst;
        vertexInfo.Size = vertices.size() * sizeof(OceanPatchVertex);
        vertexInfo.Name = "Ocean_CdlodGridVertexBuffer";
        std::shared_ptr<GraphicsResourceReply> vertexReply = resources.CreateBuffer(vertexInfo, std::span{ &vertexData, 1 });

        if (vertexReply == nullptr || vertexReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodGridVertexBuffer failed" });
        }

        impl->GridVertexBuffer = vertexReply->GetResource();

        const std::vector<uint32_t> indices = GenerateGridIndices();
        const RhiBufferResourceData indexData{ std::as_bytes(std::span{ indices }), 0u };
        BufferCreateInfo indexInfo{};
        indexInfo.Domain = ResourceDomain::GPUOnly;
        indexInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        indexInfo.Usage = BufferUsageBits::IndexBuffer | BufferUsageBits::TransferDst;
        indexInfo.Size = indices.size() * sizeof(uint32_t);
        indexInfo.Name = "Ocean_CdlodGridIndexBuffer";
        std::shared_ptr<GraphicsResourceReply> indexReply = resources.CreateBuffer(indexInfo, std::span{ &indexData, 1 });

        if (indexReply == nullptr || indexReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodGridIndexBuffer failed" });
        }

        impl->GridIndexBuffer = indexReply->GetResource();

        const uint32_t maxNodes = std::max(settings.MaxNodes, 1024u);
        impl->AllocatedMaxNodes = maxNodes;
        const std::array<uint32_t, 1> rootKeyData{ 0u };
        const RhiBufferResourceData rootKeyUpload{ std::as_bytes(std::span{ rootKeyData }), 0u };
        BufferCreateInfo nodesAInfo{};
        nodesAInfo.Domain = ResourceDomain::GPUOnly;
        nodesAInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        nodesAInfo.Usage = BufferUsageBits::StorageBuffer | BufferUsageBits::TransferDst;
        nodesAInfo.Size = maxNodes * sizeof(uint32_t);
        nodesAInfo.Name = "Ocean_CdlodNodesA";
        std::shared_ptr<GraphicsResourceReply> nodesAReply = resources.CreateBuffer(nodesAInfo, std::span{ &rootKeyUpload, 1 });

        if (nodesAReply == nullptr || nodesAReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodNodesA failed" });
        }

        impl->NodesA = nodesAReply->GetResource();

        BufferCreateInfo nodesBInfo{};
        nodesBInfo.Domain = ResourceDomain::GPUOnly;
        nodesBInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        nodesBInfo.Usage = BufferUsageBits::StorageBuffer | BufferUsageBits::TransferDst;
        nodesBInfo.Size = maxNodes * sizeof(uint32_t);
        nodesBInfo.Name = "Ocean_CdlodNodesB";
        std::shared_ptr<GraphicsResourceReply> nodesBReply = resources.CreateBuffer(nodesBInfo, {});

        if (nodesBReply == nullptr || nodesBReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodNodesB failed" });
        }

        impl->NodesB = nodesBReply->GetResource();

        std::array<uint32_t, 32> initialCounts{};
        initialCounts[1] = 1u;
        const RhiBufferResourceData countsUpload{ std::as_bytes(std::span{ initialCounts }), 0u };
        BufferCreateInfo countsInfo{};
        countsInfo.Domain = ResourceDomain::GPUOnly;
        countsInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        countsInfo.Usage = BufferUsageBits::StorageBuffer | BufferUsageBits::TransferDst;
        countsInfo.Size = initialCounts.size() * sizeof(uint32_t);
        countsInfo.Name = "Ocean_CdlodNodeCounts";
        std::shared_ptr<GraphicsResourceReply> countsReply = resources.CreateBuffer(countsInfo, std::span{ &countsUpload, 1 });

        if (countsReply == nullptr || countsReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodNodeCounts failed" });
        }

        impl->NodeCounts = countsReply->GetResource();

        const uint32_t maxPatches = std::max(settings.MaxSelectedPatches, 256u);
        impl->AllocatedMaxSelectedPatches = maxPatches;
        BufferCreateInfo patchesInfo{};
        patchesInfo.Domain = ResourceDomain::GPUOnly;
        patchesInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        patchesInfo.Usage = BufferUsageBits::StorageBuffer;
        patchesInfo.Size = maxPatches * sizeof(OceanCdlodPatch);
        patchesInfo.Name = "Ocean_CdlodSelectedPatches";
        std::shared_ptr<GraphicsResourceReply> patchesReply = resources.CreateBuffer(patchesInfo, {});

        if (patchesReply == nullptr || patchesReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodSelectedPatches failed" });
        }

        impl->SelectedPatches = patchesReply->GetResource();

        const std::array<uint32_t, 5> initialIndirect{ k_TotalIndices, 0u, 0u, 0u, 0u };
        const RhiBufferResourceData indirectUpload{ std::as_bytes(std::span{ initialIndirect }), 0u };
        BufferCreateInfo indirectInfo{};
        indirectInfo.Domain = ResourceDomain::GPUOnly;
        indirectInfo.Flags = ResourceCreationFlags::UserDataAsNameString;
        indirectInfo.Usage = BufferUsageBits::IndirectBuffer | BufferUsageBits::StorageBuffer | BufferUsageBits::TransferDst;
        indirectInfo.Size = sizeof(VkDrawIndexedIndirectCommand);
        indirectInfo.Name = "Ocean_CdlodIndirectArgs";
        std::shared_ptr<GraphicsResourceReply> indirectReply = resources.CreateBuffer(indirectInfo, std::span{ &indirectUpload, 1 });

        if (indirectReply == nullptr || indirectReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return std::unexpected(std::string{ "creating Ocean_CdlodIndirectArgs failed" });
        }

        impl->IndirectDrawArgs = indirectReply->GetResource();

        impl->ParamsBuffers.reserve(frame_count);
        impl->ParamsPointers.reserve(frame_count);

        std::vector<std::string> paramsNames;
        paramsNames.reserve(frame_count);

        for (uint32_t frame = 0u; frame < frame_count; ++frame)
        {
            paramsNames.push_back(std::format("Ocean_CdlodParams_{}", frame));
            BufferCreateInfo paramsInfo{};
            paramsInfo.Domain = ResourceDomain::CPUToGPU;
            paramsInfo.Flags = ResourceCreationFlags::PersistentlyMapped | ResourceCreationFlags::UserDataAsNameString;
            paramsInfo.Usage = BufferUsageBits::ComputeShaderUBO;
            paramsInfo.Size = sizeof(OceanQuadtreeParams);
            paramsInfo.Name = paramsNames.back();
            std::shared_ptr<GraphicsResourceReply> paramsReply = resources.CreateBuffer(paramsInfo, {});

            if (paramsReply == nullptr || paramsReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return std::unexpected(std::format("creating Ocean_CdlodParams_{} failed", frame));
            }

            const GraphicsResource& paramsRes = impl->ParamsBuffers.emplace_back(paramsReply->GetResource());

            std::shared_ptr<PointerMessageReply> mapReply = resources.MapBuffer(paramsRes, sizeof(OceanQuadtreeParams), 0u);

            if (mapReply == nullptr || mapReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return std::unexpected(std::format("mapping Ocean_CdlodParams_{} failed", frame));
            }

            impl->ParamsPointers.push_back(mapReply->GetPointer());
        }

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);

        if (!index.has_value())
        {
            return std::unexpected(std::format("module {}: {}", k_ModuleName, rhi::ToString(index.error())));
        }

        impl->ManifestIndexPtr = *index;

        const uint32_t waveSize = PickWaveSize(device);

        const std::array<std::string_view, 1> updateEntryPoints{ k_UpdateEntryPoint };
        const std::array<std::string_view, 1> buildEntryPoints{ k_BuildEntryPoint };

        std::optional<lodestone::VariantKey> resolvedVariant;

        if (waveSize != 0u)
        {
            lodestone::ManifestQueryBuilder waveQuery = (*index)->Query()
                .Where("QUADTREE_USE_WAVE_OPS", true)
                .Where("QUADTREE_WAVE_SIZE", waveSize);
            std::expected<lodestone::VariantKey, lodestone::QueryError> waveVar = waveQuery.SingleFor(updateEntryPoints);
            if (waveVar.has_value())
            {
                resolvedVariant = *waveVar;
            }
        }

        if (!resolvedVariant.has_value())
        {
            lodestone::ManifestQueryBuilder scalarQuery = (*index)->Query()
                .Where("QUADTREE_USE_WAVE_OPS", false);
            std::expected<lodestone::VariantKey, lodestone::QueryError> scalarVar = scalarQuery.SingleFor(updateEntryPoints);
            if (!scalarVar.has_value())
            {
                return std::unexpected(std::format("no variant found for Quadtree (wave size {}, query error {})",
                    waveSize, static_cast<int>(scalarVar.error().Code)));
            }
            resolvedVariant = *scalarVar;
        }

        impl->UpdateVariant = *resolvedVariant;
        impl->BuildVariant = *resolvedVariant;

        rhi::ManifestProgramOptions updateOptions{};
        if (waveSize != 0u && resolvedVariant.has_value())
        {
            updateOptions.RequiredSubgroupSize = waveSize;
            updateOptions.RequireFullSubgroups = true;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> updateProgram =
            library.CreateProgram(device.Handle(), k_ModuleName, impl->UpdateVariant, updateEntryPoints, updateOptions);

        if (!updateProgram.has_value() && waveSize != 0u)
        {
            lodestone::ManifestQueryBuilder scalarQuery = (*index)->Query()
                .Where("QUADTREE_USE_WAVE_OPS", false);
            std::expected<lodestone::VariantKey, lodestone::QueryError> scalarVar = scalarQuery.SingleFor(updateEntryPoints);
            if (scalarVar.has_value())
            {
                impl->UpdateVariant = *scalarVar;
                impl->BuildVariant = *scalarVar;
                updateProgram = library.CreateProgram(device.Handle(), k_ModuleName, impl->UpdateVariant, updateEntryPoints);
            }
        }

        if (!updateProgram.has_value())
        {
            return std::unexpected(std::format("{}: {}", k_UpdateEntryPoint, rhi::ToString(updateProgram.error())));
        }

        impl->UpdateProgram = std::move(*updateProgram);

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> buildProgram =
            library.CreateProgram(device.Handle(), k_ModuleName, impl->BuildVariant, buildEntryPoints);

        if (!buildProgram.has_value())
        {
            return std::unexpected(std::format("{}: {}", k_BuildEntryPoint, rhi::ToString(buildProgram.error())));
        }

        impl->BuildProgram = std::move(*buildProgram);

        (void)rhi::RhiSystem::SetObjectName(
            VK_OBJECT_TYPE_PIPELINE_LAYOUT,
            impl->UpdateProgram->PipelineLayout().As<uint64_t>(),
            "Ocean_CdlodUpdatePipelineLayout");

        (void)rhi::RhiSystem::SetObjectName(
            VK_OBJECT_TYPE_PIPELINE_LAYOUT,
            impl->BuildProgram->PipelineLayout().As<uint64_t>(),
            "Ocean_CdlodBuildPipelineLayout");

        const uint32_t totalSets = 3u * frame_count;
        const std::array<VkDescriptorPoolSize, 2> poolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3u * frame_count },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12u * frame_count }
        };
        const VkDescriptorPoolCreateInfo poolInfo
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            nullptr,
            0,
            totalSets,
            static_cast<uint32_t>(poolSizes.size()),
            poolSizes.data()
        };

        if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &impl->DescriptorPool) != VK_SUCCESS)
        {
            return std::unexpected(std::string{ "CDLOD descriptor pool creation failed" });
        }

        (void)rhi::RhiSystem::SetObjectName(
            VK_OBJECT_TYPE_DESCRIPTOR_POOL,
            reinterpret_cast<uint64_t>(impl->DescriptorPool),
            "Ocean_CdlodDescriptorPool");

        const VkDescriptorSetLayout updateSetLayout = impl->UpdateProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
        const VkDescriptorSetLayout buildSetLayout = impl->BuildProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();

        impl->UpdateSetsPing.reserve(frame_count);
        impl->UpdateSetsPong.reserve(frame_count);
        impl->BuildSets.reserve(frame_count);

        for (uint32_t frame = 0u; frame < frame_count; ++frame)
        {
            VkDescriptorSet setPing{ VK_NULL_HANDLE };
            const VkDescriptorSetAllocateInfo pingAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &updateSetLayout };

            if (vkAllocateDescriptorSets(vkDevice, &pingAlloc, &setPing) != VK_SUCCESS)
            {
                return std::unexpected(std::string{ "CDLOD update ping set allocation failed" });
            }

            const BoundResources boundPing
            {
                { "QuadtreeParams", { impl->ParamsBuffers[frame] } },
                { "PrevNodes", { impl->NodesA } },
                { "CurrNodes", { impl->NodesB } },
                { "NodeCounts", { impl->NodeCounts } },
                { "SelectedPatches", { impl->SelectedPatches } },
            };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, setPing, **index, impl->UpdateVariant, updateEntryPoints, boundPing); error.has_value())
            {
                return std::unexpected(*error);
            }

            const std::string pingName = std::format("Ocean_CdlodUpdateSetPing_{}", frame);
            (void)rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(setPing), pingName.c_str());
            impl->UpdateSetsPing.push_back(setPing);

            VkDescriptorSet setPong{ VK_NULL_HANDLE };
            const VkDescriptorSetAllocateInfo pongAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &updateSetLayout };

            if (vkAllocateDescriptorSets(vkDevice, &pongAlloc, &setPong) != VK_SUCCESS)
            {
                return std::unexpected(std::string{ "CDLOD update pong set allocation failed" });
            }

            const BoundResources boundPong
            {
                { "QuadtreeParams", { impl->ParamsBuffers[frame] } },
                { "PrevNodes", { impl->NodesB } },
                { "CurrNodes", { impl->NodesA } },
                { "NodeCounts", { impl->NodeCounts } },
                { "SelectedPatches", { impl->SelectedPatches } },
            };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, setPong, **index, impl->UpdateVariant, updateEntryPoints, boundPong); error.has_value())
            {
                return std::unexpected(*error);
            }

            const std::string pongName = std::format("Ocean_CdlodUpdateSetPong_{}", frame);
            (void)rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(setPong), pongName.c_str());
            impl->UpdateSetsPong.push_back(setPong);

            VkDescriptorSet setBuild{ VK_NULL_HANDLE };
            const VkDescriptorSetAllocateInfo buildAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &buildSetLayout };

            if (vkAllocateDescriptorSets(vkDevice, &buildAlloc, &setBuild) != VK_SUCCESS)
            {
                return std::unexpected(std::string{ "CDLOD build set allocation failed" });
            }

            const BoundResources boundBuild
            {
                { "QuadtreeParams", { impl->ParamsBuffers[frame] } },
                { "CurrNodes", { impl->NodesA } },
                { "IndirectDrawArgs", { impl->IndirectDrawArgs } },
                { "NodeCounts", { impl->NodeCounts } },
            };

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, setBuild, **index, impl->BuildVariant, buildEntryPoints, boundBuild); error.has_value())
            {
                return std::unexpected(*error);
            }

            const std::string buildName = std::format("Ocean_CdlodBuildSet_{}", frame);
            (void)rhi::RhiSystem::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(setBuild), buildName.c_str());
            impl->BuildSets.push_back(setBuild);
        }

        return std::make_unique<OceanCdlod>(std::move(impl));
    }

    OceanCdlod::OceanCdlod(std::unique_ptr<OceanCdlodImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {
    }

    OceanCdlod::~OceanCdlod() = default;

    const OceanCdlodSettings& OceanCdlod::Settings() const noexcept
    {
        return impl->Settings;
    }

    void OceanCdlod::SetSettings(const OceanCdlodSettings& settings) noexcept
    {
        impl->Settings = settings;
    }

    void OceanCdlod::RecordUpdate(
        rhi::CommandBuffer& cmd,
        uint32_t frame_index,
        const OceanCdlodCameraState& camera,
        rhi::GpuTimerBlock* timer_block)
    {
        assert(frame_index < impl->ParamsBuffers.size());

        OceanQuadtreeParams params{};
        params.FrustumPlanes = camera.FrustumPlanes;
        params.CameraPosition = camera.CameraPosition;
        params.WorldExtent = impl->Settings.WorldExtent;
        const float snap = impl->Settings.WorldExtent / 64.0f;
        params.RootCenter[0] = std::round(camera.CameraPosition[0] / snap) * snap;
        params.RootCenter[1] = std::round(camera.CameraPosition[2] / snap) * snap;
        params.MaxWaveHeight = impl->Settings.MaxWaveHeight;
        params.MorphStartRatio = impl->Settings.MorphStartRatio;
        params.MaxLevel = impl->Settings.MaxLevel;
        params.BaseGridCount = impl->Settings.BaseGridCount;
        params.LodDistanceScale = impl->Settings.LodDistanceScale;
        params.MaxNodes = impl->AllocatedMaxNodes;
        params.MaxSelectedPatches = impl->AllocatedMaxSelectedPatches;

        std::memcpy(impl->ParamsPointers[frame_index], &params, sizeof(OceanQuadtreeParams));

        {
            std::optional<rhi::ScopedGpuTimer> selectTimer;
            if (timer_block != nullptr)
            {
                selectTimer.emplace(*timer_block, "CDLOD select");
            }

            RhiAssert(cmd.BindShaders(impl->UpdateProgram->Program()));

            const rhi::GlobalBarrier computeToCompute
            {
                rhi::PipelineStage::ComputeShader,
                rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::ComputeShader,
                rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
            };

            for (uint32_t level = 0u; level <= impl->Settings.MaxLevel; ++level)
            {
                const bool isPing = (level % 2u == 0u);
                const VkDescriptorSet currentSet = isPing ? impl->UpdateSetsPing[frame_index] : impl->UpdateSetsPong[frame_index];
                const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(currentSet) };
                cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, impl->UpdateProgram->PipelineLayout(), 0u, std::span{ &setHandle, 1 });

                PushConstants(cmd, *impl->UpdateProgram, QuadtreePushConstants{ level });

                const uint32_t maxNodesAtLevel = (level < 16u)
                    ? std::min<uint32_t>(1u << (2u * level), impl->AllocatedMaxNodes)
                    : impl->AllocatedMaxNodes;
                const uint32_t groups = std::max<uint32_t>(1u, (maxNodesAtLevel + 63u) / 64u);
                cmd.Dispatch(groups, 1u, 1u);

                cmd.Barrier(rhi::DependencyInfo{ std::span{ &computeToCompute, 1 }, {}, {} });
            }
        }

        {
            std::optional<rhi::ScopedGpuTimer> indirectArgsTimer;
            if (timer_block != nullptr)
            {
                indirectArgsTimer.emplace(*timer_block, "CDLOD indirect args");
            }

            RhiAssert(cmd.BindShaders(impl->BuildProgram->Program()));
            const rhi::DescriptorSetHandle buildSetHandle{ reinterpret_cast<uint64_t>(impl->BuildSets[frame_index]) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, impl->BuildProgram->PipelineLayout(), 0u, std::span{ &buildSetHandle, 1 });
            cmd.Dispatch(1u, 1u, 1u);
        }

        const rhi::GlobalBarrier cdlodToRender
        {
            rhi::PipelineStage::ComputeShader,
            rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::DrawIndirect | rhi::PipelineStage::VertexShader,
            rhi::Access::IndirectCommandRead | rhi::Access::ShaderStorageRead
        };
        cmd.Barrier(rhi::DependencyInfo{ std::span{ &cdlodToRender, 1 }, {}, {} });
    }

    const GraphicsResource& OceanCdlod::GridVertexBuffer() const noexcept
    {
        return impl->GridVertexBuffer;
    }

    const GraphicsResource& OceanCdlod::GridIndexBuffer() const noexcept
    {
        return impl->GridIndexBuffer;
    }

    const GraphicsResource& OceanCdlod::SelectedPatchesBuffer() const noexcept
    {
        return impl->SelectedPatches;
    }

    const GraphicsResource& OceanCdlod::IndirectDrawBuffer() const noexcept
    {
        return impl->IndirectDrawArgs;
    }

    uint32_t OceanCdlod::IndexCount() const noexcept
    {
        return k_TotalIndices;
    }
}
