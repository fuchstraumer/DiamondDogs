#include "ObjModel.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include <atomic>
#include <fstream>
#include <future>
#pragma warning(push, 0)
#include "easylogging++.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#include "mango/filesystem/file.hpp"
#define TINYOBJ_LOADER_OPT_IMPLEMENTATION
#include "tinyobjloader/experimental/tinyobj_loader_opt.h"
#pragma warning(pop)

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

    //rsrc_context.Update();

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
        // some profiling/debugging tools might present only one thread for use iirc, this should make that safe
        options.req_num_threads = std::max(static_cast<int>(std::thread::hardware_concurrency() / 2u), (int)1);
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

void ObjectModel::Render(const objRenderStateData& state)
{
    auto& rsrc_context = ResourceContext::Get();
    if ((EBO == nullptr) || rsrc_context.ResourceInTransferQueue(EBO) || !readyToRender)
    {
        // only checked for EBO: if queued for transfer still, return
        return;
    }
    // reset offset to begin
    cmdOffset = 0u;

    switch (state.type)
    {
    case render_type::PrePass:
        [[fallthrough]] ;
    case render_type::Shadow:
        // won't work until we figure out how to make shader system cooperate more
        // Bind(state.cmd, BindMode::VBO0AndEBO); // positions and indices only
        [[fallthrough]] ;
    case render_type::Opaque:
        [[fallthrough]] ;
    case render_type::Transparent:
        [[fallthrough]] ;
    case render_type::OpaqueAndTransparent:
        Bind(state.cmd, BindMode::All);
    default:
        break;
    }

    renderGeometry(state);
}

void ObjectModel::renderGeometry(const objRenderStateData& state)
{
    for (size_t i = 0; i < numMaterials; ++i)
    {
        if ((state.type != render_type::PrePass) && (state.type != render_type::Shadow))
        {
            materials[i].Bind(state.cmd, state.materialLayout, *state.binder);
        }
        else if (state.type == render_type::PrePass)
        {
            state.binder->Bind(state.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
        }

        // not rendering transparents, material is partially transparent at least so skip it
        if (state.type == render_type::Opaque && !materials[i].Opaque())
        {
            auto draw_ranges = indirectCommands.equal_range(i);
            cmdOffset += static_cast<uint32_t>(sizeof(VkDrawIndexedIndirectCommand) * std::distance(draw_ranges.first, draw_ranges.second));
            continue;
        }

        renderIdx(state.cmd, i, state.binder);
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

    AABB modelAABB;

    struct material_group_t
    {
        std::vector<uint32_t> indices;
        std::vector<glm::vec3> positions;
        std::vector<vtxAttrib> attributes;
        AABB materialGroupBounds;
    };

    std::map<uint32_t, material_group_t> groups;
    std::unordered_map<vertex_t, uint32_t> uniqueVertices;
    std::unordered_map<uint32_t, glm::vec3> vertexTangents;

    auto appendVert = [&groups, &uniqueVertices, &modelAABB, this](const vertex_t & vert, const int material_id, AABB& bounds)->uint32_t
    {
        auto& group = groups[material_id];
        uint32_t result = static_cast<uint32_t>(positions.size());
        group.positions.emplace_back(vert.Position);
        group.materialGroupBounds.Include(vert.Position);
        modelAABB.Include(vert.Position);
        group.attributes.emplace_back(vtxAttrib{ vert.Normal, glm::vec3(), vert.UV });
        return result;

    };

    size_t idx_offset{ 0u };

    for (size_t i = 0u; i < attrib.face_num_verts.size(); ++i)
    {
        AABB partAABB;
        const size_t ngon = attrib.face_num_verts[i];
        const size_t material_id = attrib.material_ids[i];
        assert(ngon == 3u);

        uint32_t tri_indices[3];

        for (size_t j = 0u; j < ngon; ++j)
        {
            tinyobj_opt::index_t idx = attrib.indices[idx_offset + j];
            vertex_t vtx;
            vtx.Position = glm::vec3{
                attrib.vertices[3 * idx.vertex_index + 0],
                attrib.vertices[3 * idx.vertex_index + 1],
                attrib.vertices[3 * idx.vertex_index + 2]
            };
            vtx.Normal = glm::vec3{
                attrib.normals[3 * idx.normal_index + 0],
                attrib.normals[3 * idx.normal_index + 1],
                attrib.normals[3 * idx.normal_index + 2]
            };
            vtx.UV = glm::vec2{
                attrib.texcoords[2 * idx.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
            };

            tri_indices[j] = appendVert(vtx, attrib.material_ids[i], partAABB);

        }

        // calculate tangents
        const glm::vec3& v0 = positions[tri_indices[0]];
        const glm::vec2& uv0 = attributes[tri_indices[0]].uv;
        const glm::vec3& v1 = positions[tri_indices[1]];
        const glm::vec2& uv1 = attributes[tri_indices[1]].uv;
        const glm::vec3& v2 = positions[tri_indices[2]];
        const glm::vec2& uv2 = attributes[tri_indices[2]].uv;

        float x0 = v1.x - v0.x;
        float x1 = v2.x - v0.x;
        float y0 = v1.y - v0.y;
        float y1 = v2.y - v0.y;
        float z0 = v1.z - v0.z;
        float z1 = v2.z - v0.z;

        float s0 = uv1.x - uv0.x;
        float s1 = uv2.x - uv0.x;
        float t0 = uv1.y - uv0.y;
        float t1 = uv2.y - uv0.y;

        float r = 1.0f / (s0 * t1 - s1 * t0);

        const glm::vec3 sDir{
            (t1 * x0 - t0 * x1) * r,
            (t1 * y0 - t0 * y1) * r,
            (t1 * z0 - t0 * z1) * r
        };

        for (const auto& idx : tri_indices)
        {
            if (vertexTangents.count(idx) == 0)
            {
                vertexTangents.emplace(idx, sDir);
            }
            else
            {
                auto& tangent = vertexTangents.at(idx);
                tangent += sDir;
            }
        }

        idx_offset += ngon;
    }

    assert(uniqueVertices.size() == positions.size());

    for (size_t i = 0u; i < attributes.size(); ++i)
    {
        const glm::vec3& normal = attributes[i].normal;
        const glm::vec3& tan0 = vertexTangents.at(static_cast<uint32_t>(i));
        attributes[i].tangent = glm::normalize(tan0 - normal * glm::dot(normal, tan0));
    }

    uint32_t material_idx = 0u;
    for (auto&& group : groups)
    {
        Part new_part;
        new_part.vertexOffset = 0;
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

    const std::string indirectDrawBufferName = modelName + std::string("_IndirectDrawBuffer");
    IndirectDrawBuffers = ResourceContext::Get().CreateBuffer(&indirect_buffer_info, nullptr, 1u, &indirect_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory | ResourceCreateUserDataAsString, (void*)indirectDrawBufferName.c_str());
    // This is all the work this object has to perform to be ready to render: once this is done, we just need our data transferred to be good to go
    readyToRender = true;
}