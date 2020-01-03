#include "MeshProcessing.hpp"
#include "LoadedDataCache.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <algorithm>

void CalculateTangents(const ccDataHandle handle)
{
    ObjectModelDataImpl* data = TryAndGetModelData(handle);
    if (!data)
    {
        return;
    }

    std::vector<glm::vec3> tangents;
    const size_t floatsPerVert = data->vertexStride / sizeof(float);
    tangents.resize(data->vertexData.size() / floatsPerVert);

    const size_t numIndices = data->indices.size();
    for (size_t i = 0; i < numIndices; i += 3)
    {
        const uint32_t tri[3]{ data->indices[i + 0], data->indices[i + 1], data->indices[i + 2] };
        glm::vec3 v0 = glm::make_vec3(&data->vertexData[tri[0] * floatsPerVert]);
        glm::vec3 v1 = glm::make_vec3(&data->vertexData[tri[1] * floatsPerVert]);
        glm::vec3 v2 = glm::make_vec3(&data->vertexData[tri[2] * floatsPerVert]);

        glm::vec3 edge0 = v1 - v0;
        glm::vec3 edge1 = v2 - v0;
        glm::vec3 normal = glm::cross(edge0, edge1);

        glm::mat3x3 toUnitPos(edge0, edge1, normal);

        // uvs at offset 9 (or, so they have to be if we're doing this at least
        glm::vec2 uv0 = glm::make_vec2(&data->vertexData[tri[0] * floatsPerVert + 9u]);
        glm::vec2 uv1 = glm::make_vec2(&data->vertexData[tri[1] * floatsPerVert + 9u]);
        glm::vec2 uv2 = glm::make_vec2(&data->vertexData[tri[2] * floatsPerVert + 9u]);

        glm::vec2 uvEdge0 = uv1 - uv0;
        glm::vec2 uvEdge1 = uv2 - uv0;
        glm::mat3x3 toUnitUV(1.0f);
        toUnitUV[0].x = uvEdge0.x;
        toUnitUV[0].y = uvEdge0.y;
        toUnitUV[1].x = uvEdge1.x;
        toUnitUV[1].y = uvEdge1.y;

        auto UVtoPosition = glm::inverse(toUnitUV) * toUnitPos;
        glm::vec3 tangent = normalize(UVtoPosition[0]);

        tangents[tri[0]] += tangent;
        tangents[tri[1]] += tangent;
        tangents[tri[2]] += tangent;

    }

    const size_t numVerts = data->vertexData.size() / floatsPerVert;
    for (size_t i = 0; i < numVerts; ++i)
    {
        tangents[i] = glm::normalize(tangents[i]);
        const size_t currentTangent = i * floatsPerVert + 6u;
        float* tangentPtr = &data->vertexData[currentTangent];
        tangentPtr[0] = tangents[i].x;
        tangentPtr[1] = tangents[i].y;
        tangentPtr[2] = tangents[i].z;
    }
}

void SortMeshMaterialRanges(const ccDataHandle handle)
{
    ObjectModelDataImpl* modelData = TryAndGetModelData(handle);
    if (!modelData)
    {
        return;
    }

    auto materialRangeSorter = [](const MaterialRange& r0, const MaterialRange& r1)
    {
        if (r0.MaterialName != r1.MaterialName)
        {
            return r0.MaterialName < r1.MaterialName;
        }
        else
        {
            return r0.startIndex < r1.startIndex;
        }
    };

    std::sort(modelData->materialRanges.begin(), modelData->materialRanges.begin(), materialRangeSorter);

    std::vector<MaterialRange> mergedRanges;
    mergedRanges.reserve(modelData->materialRanges.size());

    std::vector<uint32_t> indicesReordered;
    indicesReordered.resize(modelData->indices.size());

    size_t indicesCopied = 0u;

    {
        const MaterialRange& firstRange = modelData->materialRanges.front();
        std::copy(modelData->indices.begin(), modelData->indices.begin() + firstRange.indexCount, indicesReordered.begin());
        mergedRanges.emplace_back(MaterialRange{ firstRange.MaterialName, 0u, firstRange.indexCount });
        indicesCopied += firstRange.indexCount;
    }

    const size_t numRanges = modelData->materialRanges.size();
    for (size_t i = 1; i < numRanges; ++i)
    {
        const MaterialRange& currentMtl = modelData->materialRanges[i];
        std::copy(
            modelData->indices.begin() + currentMtl.startIndex,
            modelData->indices.begin() + currentMtl.startIndex + currentMtl.indexCount,
            indicesReordered.begin() + indicesCopied);
        
        if (currentMtl.MaterialName == mergedRanges.back().MaterialName)
        {
            mergedRanges.back().indexCount += currentMtl.indexCount;
        }
        else
        {
            mergedRanges.emplace_back(MaterialRange{ currentMtl.MaterialName, static_cast<uint32_t>(indicesCopied), currentMtl.indexCount });
        }

        indicesCopied += currentMtl.indexCount;
    }

    mergedRanges.shrink_to_fit();
    modelData->indices.swap(indicesReordered);
    modelData->materialRanges.swap(mergedRanges);
}
