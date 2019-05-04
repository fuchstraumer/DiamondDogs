#include "ObjModel.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "easylogging++.h"
#include <atomic>
#include <fstream>
#pragma warning(push, 1)
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#define TINYOBJ_LOADER_OPT_IMPLEMENTATION
#include "tinyobjloader/experimental/tinyobj_loader_opt.h"
#pragma warning(pop)
#include "ResourceLoader.hpp"
#include <future>
#include "mango/filesystem/file.hpp"
#include "mango/core/thread.hpp"

namespace std {
    /** Hash method ovverride so that we can use vulpes::vertex_t in standard library containers. Used with tinyobj to remove/avoid duplicated vertices. \ingroup Objects */
    template<>
    struct hash<vertex_t> {
        size_t operator()(const vertex_t& vert) const {
            return (hash<glm::vec3>()(vert.Position)) ^ (hash<glm::vec3>()(vert.Normal) << 1) ^ (hash<glm::vec2>()(vert.UV) << 4);
        }
    };
}

static std::atomic<size_t> objModelInstanceCount{ 0u };

bool GetMultidrawIndirect() noexcept
{
    const auto* device = RenderingContext::Get().PhysicalDevice();
    return device->GetProperties().limits.maxDrawIndirectCount != 1u;
}

void ObjectModel::Bind(VkCommandBuffer cmd, BindMode mode)
{
    constexpr static VkDeviceSize Offsets[2]{ 0u, 0u };
    const VkBuffer vbos[2]{ (VkBuffer)VBO0->Handle, (VkBuffer)VBO1->Handle };
    switch (mode)
    {
    case BindMode::None:
        break;
    case BindMode::VBO0Only:
        vkCmdBindVertexBuffers(cmd, 0u, 1u, vbos, Offsets);
        break;
    case BindMode::VBO0AndEBO:
        vkCmdBindVertexBuffers(cmd, 0u, 1u, vbos, Offsets);
        vkCmdBindIndexBuffer(cmd, (VkBuffer)EBO->Handle, 0u, indicesAreUi16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        break;
    case BindMode::All:
        vkCmdBindVertexBuffers(cmd, 0u, 2u, vbos, Offsets);
        vkCmdBindIndexBuffer(cmd, (VkBuffer)EBO->Handle, 0u, indicesAreUi16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        break;
    default:
        throw std::domain_error("Invalid BindMode value!");
    };
}

void ObjectModel::CreateBuffers()
{
    objModelInstanceCount.fetch_add(1u);

    const auto* device = RenderingContext::Get().Device();
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
    const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const bool concurrent_sharing = transfer_idx != graphics_idx;
    const std::array<uint32_t, 2u> queue_family_indices{ transfer_idx, graphics_idx };

    indicesAreUi16 = indicesUi32.size() < std::numeric_limits<uint16_t>::max();

    gpu_resource_data_t indices_data{
        nullptr, 0u, 0u, VK_QUEUE_FAMILY_IGNORED
    };

    if (indicesAreUi16)
    {
        // did this so we know that we're clearing everything out of indicesUi32, 
        // and it's less unintuitive than reverse iterators
        std::reverse(indicesUi32.begin(), indicesUi32.end());
        while (!indicesUi32.empty())
        {
            indicesUi16.emplace_back(std::move(indicesUi32.back()));
            indicesUi32.pop_back();
        }
        indices_data.Data = indicesUi16.data();
        indices_data.DataSize = indicesUi16.size() * sizeof(uint16_t);
    }
    else
    {
        indices_data.Data = indicesUi32.data();
        indices_data.DataSize = indicesUi32.size() * sizeof(uint32_t);
    }

    auto& rsrc_context = ResourceContext::Get();
    const std::string BaseName{ "ObjModelMeshData_Instance" + std::to_string(objModelInstanceCount) };

    const VkBufferCreateInfo ebo_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        indices_data.DataSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        concurrent_sharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        concurrent_sharing ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        concurrent_sharing ? queue_family_indices.data() : nullptr
    };
    
    const std::string ebo_name{ BaseName + "_EBO" };
    EBO = rsrc_context.CreateBuffer(&ebo_create_info, nullptr, 1u, &indices_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory | ResourceCreateUserDataAsString, (void*)ebo_name.c_str());

    VkBufferCreateInfo vbo_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        0u,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        concurrent_sharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        concurrent_sharing ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        concurrent_sharing ? queue_family_indices.data() : nullptr
    };

    gpu_resource_data_t vbo_data{
        positions.data(), positions.size() * sizeof(glm::vec3), 0u, VK_QUEUE_FAMILY_IGNORED
    };

    vbo_create_info.size = vbo_data.DataSize;
    const std::string vbo0_name{ BaseName + "_VBO0" };
    VBO0 = rsrc_context.CreateBuffer(&vbo_create_info, nullptr, 1u, &vbo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory | ResourceCreateUserDataAsString, (void*)vbo0_name.c_str());

    vbo_data.Data = attributes.data();
    vbo_data.DataSize = attributes.size() * sizeof(vtxAttrib);
    vbo_create_info.size = vbo_data.DataSize;
    const std::string vbo1_name{ BaseName + "_VBO1" };
    VBO1 = rsrc_context.CreateBuffer(&vbo_create_info, nullptr, 1u, &vbo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory | ResourceCreateUserDataAsString, (void*)vbo1_name.c_str());

    // the upload buffers are a copy, so we can safely clear the cpu-side data at this point
    positions.clear();
    attributes.clear();
    indicesUi32.clear();
    indicesUi16.clear();

    positions.shrink_to_fit();
    attributes.shrink_to_fit();
    indicesUi32.shrink_to_fit();
    indicesUi16.shrink_to_fit();

}

ObjectModel::ObjectModel() : multiDrawIndirect(GetMultidrawIndirect())
{
    
}

void ObjectModel::LoadModelFromFile(const char* model_filename, const char* material_directory)
{

    tinyobj_opt::attrib_t attrib;
    std::vector<tinyobj_opt::shape_t> shapes;
    std::vector<tinyobj_opt::material_t> materials;

    {
        mango::filesystem::File model_file(model_filename);
        mango::Memory model_memory = model_file;

        tinyobj_opt::LoadOption options;
        options.req_num_threads = std::thread::hardware_concurrency() - 1;
        if (!tinyobj_opt::parseObj(&attrib, &shapes, &materials, (char*)model_memory, model_memory.size, options))
        {
            throw std::runtime_error("Failed to load .obj file!");
        }
    }

    numMaterials = materials.size();
    createMaterials(materials, material_directory);

    loadMeshes(shapes, attrib);
    generateIndirectDraws();

}

void ObjectModel::Render(VkCommandBuffer cmd, DescriptorBinder* binder, vtf_frame_data_t::render_type render_type, const VkPipelineLayout pipeline_layout)
{

    // reset offset to begin
    cmdOffset = 0u;

    switch (render_type)
    {
    case vtf_frame_data_t::render_type::PrePass:
        [[fallthrough]] ;
    case vtf_frame_data_t::render_type::Shadow:
        Bind(cmd, BindMode::VBO0AndEBO); // positions and indices only
        break;
    case vtf_frame_data_t::render_type::Opaque:
        [[fallthrough]] ;
    case vtf_frame_data_t::render_type::Transparent:
        [[fallthrough]] ;
    case vtf_frame_data_t::render_type::OpaqueAndTransparent:
        Bind(cmd, BindMode::All);
    default:
        break;
    }


}

void ObjectModel::renderGeometry(VkCommandBuffer cmd, DescriptorBinder* binder, vtf_frame_data_t::render_type render_type, const VkPipelineLayout pipeline_layout)
{
    for (size_t i = 0; i < numMaterials; ++i)
    {
        if ((render_type != vtf_frame_data_t::render_type::PrePass) && (render_type != vtf_frame_data_t::render_type::Shadow))
        {
            materials[i].Bind(cmd, pipeline_layout, *binder);
        }

        // not rendering transparents, material is partially transparent at least so skip it
        if (render_type == vtf_frame_data_t::render_type::Opaque && !materials[i].Opaque())
        {
            auto draw_ranges = indirectCommands.equal_range(i);
            cmdOffset += static_cast<uint32_t>(sizeof(VkDrawIndexedIndirectCommand) * std::distance(draw_ranges.first, draw_ranges.second));
            continue;
        }

        renderIdx(cmd, i, binder);
    }
}

void ObjectModel::renderIdx(VkCommandBuffer cmd, const size_t index, DescriptorBinder* binder)
{
    auto draw_ranges = indirectCommands.equal_range(index);
    const uint32_t num_commands = static_cast<uint32_t>(std::distance(draw_ranges.first, draw_ranges.second));
    if (multiDrawIndirect)
    {
        vkCmdDrawIndexedIndirect(cmd, (VkBuffer)IndirectDrawBuffers->Handle, cmdOffset, num_commands, sizeof(VkDrawIndexedIndirectCommand));
        cmdOffset += static_cast<uint32_t>(sizeof(VkDrawIndexedIndirectCommand) * num_commands);
    }
    else
    {
        for (uint32_t i = 0u; i < num_commands; ++i)
        {
            vkCmdDrawIndexedIndirect(cmd, (VkBuffer)IndirectDrawBuffers->Handle, cmdOffset, 1u, sizeof(VkDrawIndexedIndirectCommand));
            cmdOffset += static_cast<uint32_t>(sizeof(VkDrawIndexedIndirectCommand));
        }
    }
}

constexpr bool ObjectModel::Part::operator==(const Part& other) const noexcept
{
    return (idxCount == other.idxCount) && (mtlIdx == other.mtlIdx);
}

constexpr bool ObjectModel::Part::operator<(const Part& other) const noexcept
{
    return mtlIdx == other.mtlIdx ? idxCount < other.idxCount : mtlIdx < other.mtlIdx;
}

void ObjectModel::loadMeshes(const std::vector<tinyobj_opt::shape_t>& shapes, const tinyobj_opt::attrib_t& attrib)
{
    struct material_group_t
    {
        std::vector<glm::vec3> positions;
        std::vector<vtxAttrib> attributes;
        std::vector<uint32_t> indices;
    };

    std::map<int, material_group_t> groups;
    std::map<int, std::unordered_map<vertex_t, uint32_t>> uniqueVertices;

    auto appendVert = [&groups, &uniqueVertices](const vertex_t & vert, const int material_id, AABB& bounds)
    {

        auto& unique_verts = uniqueVertices[material_id];
        auto& group = groups[material_id];

        group.positions.emplace_back(vert.Position);
        group.attributes.emplace_back(vtxAttrib{ vert.Normal, vert.Tangent, vert.UV });
        group.indices.emplace_back(group.positions.size() - 1u);
    };

    size_t idx_offset{ 0u };

    for (size_t i = 0u; i < attrib.face_num_verts.size(); ++i)
    {
        const size_t ngon = attrib.face_num_verts[i];
        const size_t material_id = attrib.material_ids[i];
        assert(ngon == 3u);
        for (size_t j = 0u; j < ngon / 3u; ++j)
        {
            tinyobj_opt::index_t idx = attrib.indices[idx_offset + j];

            vertex_t vtx;
            vtx.Position = glm::vec3{
                attrib.vertices[3u * idx.vertex_index + 0u],
                attrib.vertices[3u * idx.vertex_index + 1u],
                attrib.vertices[3u * idx.vertex_index + 2u]
            };
            vtx.Normal = glm::vec3{
                attrib.normals[3u * idx.normal_index + 0u],
                attrib.normals[3u * idx.normal_index + 1u],
                attrib.normals[3u * idx.normal_index + 2u]
            };
            vtx.Tangent = glm::vec3(0.0f);
            vtx.UV = glm::vec2{
                attrib.texcoords[2u * idx.texcoord_index + 0u],
                1.0f - attrib.texcoords[2u * idx.texcoord_index + 1u]
            };

            appendVert(vtx, material_id, modelAABB);
            
        }

        idx_offset += ngon;
    }

    uint32_t material_idx = 0u;
    for (auto&& group : groups)
    {
        Part new_part;
        new_part.vertexOffset = static_cast<int32_t>(positions.size());
        std::copy(group.second.positions.begin(), group.second.positions.end(), std::back_inserter(positions));
        std::copy(group.second.attributes.begin(), group.second.attributes.end(), std::back_inserter(attributes));
        new_part.startIdx = static_cast<uint32_t>(indicesUi32.size());
        new_part.idxCount = static_cast<uint32_t>(group.second.indices.size());
        std::copy(group.second.indices.begin(), group.second.indices.end(), std::back_inserter(indicesUi32));
        new_part.mtlIdx = material_idx;
        ++material_idx;
        parts.insert(new_part);
    }

    CreateBuffers();

}

void ObjectModel::createMaterials(const std::vector<tinyobj_opt::material_t>& mtls, const char* search_dir)
{
    
    materials.reserve(numMaterials);
    for (const auto& mtl : mtls)
    {
        materials.emplace_back(mtl, search_dir);
    }
   
}

void ObjectModel::generateIndirectDraws()
{
    for (const auto& idx_group : parts)
    {
        assert(idx_group.mtlIdx < numMaterials + 1u);
        if (indirectCommands.count(idx_group.mtlIdx))
        {
            auto curr_range = indirectCommands.equal_range(idx_group.mtlIdx);
            auto same_idx_entry = std::find_if(curr_range.first, curr_range.second, [idx_group](const decltype(indirectCommands)::value_type & elem) {
                return elem.second.indexCount == idx_group.idxCount;
            });
            if (same_idx_entry == indirectCommands.end())
            {
                indirectCommands.emplace(idx_group.mtlIdx, VkDrawIndexedIndirectCommand{ idx_group.idxCount, 1u, idx_group.startIdx, idx_group.vertexOffset, 0u });
            }
            else
            {
                same_idx_entry->second.instanceCount++;
            }
        }
        else
        {
            indirectCommands.emplace(idx_group.mtlIdx, VkDrawIndexedIndirectCommand{ idx_group.idxCount, 1u, idx_group.startIdx, idx_group.vertexOffset, 0u });
        }
    }

    const auto* device = RenderingContext::Get().Device();
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
    const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const bool concurrent_sharing = transfer_idx != graphics_idx;
    const std::array<uint32_t, 2u> queue_family_indices{ transfer_idx, graphics_idx };

    const VkBufferCreateInfo indirect_buffer_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(VkDrawIndexedIndirectCommand) * indirectCommands.size(),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        concurrent_sharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        concurrent_sharing ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        concurrent_sharing ? queue_family_indices.data() : nullptr
    };

    std::vector<VkDrawIndexedIndirectCommand> indirect_cmds_data_buffer;
    indirect_cmds_data_buffer.reserve(indirectCommands.size());
    for (const auto& draw_cmd : indirectCommands)
    {
        indirect_cmds_data_buffer.emplace_back(draw_cmd.second);
    }

    const gpu_resource_data_t indirect_data{ indirect_cmds_data_buffer.data(), indirect_buffer_info.size, 0u, VK_QUEUE_FAMILY_IGNORED };

    IndirectDrawBuffers = ResourceContext::Get().CreateBuffer(&indirect_buffer_info, nullptr, 1u, &indirect_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory, nullptr);

}