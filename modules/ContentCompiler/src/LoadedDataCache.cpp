#include "LoadedDataCache.hpp"
#include <variant>
#include <unordered_map>
#include "mango/core/hash.hpp"

ObjectModelDataImpl::operator ObjectModelData() const
{
    ObjectModelData result;
    result.vertexDataSize = static_cast<uint32_t>(sizeof(float) * vertexData.size());
    result.vertexStride = static_cast<uint32_t>(vertexStride);
    result.vertexData = vertexData.data();
    result.vertexAttributeCount = static_cast<uint32_t>(vertexMetadata.size());
    result.vertexAttribMetadata = vertexMetadata.data();
    result.indexDataSize = static_cast<uint32_t>(sizeof(uint32_t) * indices.size());
    result.indexData = indices.data();
    result.numMaterials = static_cast<uint32_t>(materialRanges.size());
    result.materialRanges = materialRanges.data();
    result.numPrimGroups = static_cast<uint32_t>(primitiveGroups.size());
    result.primitiveGroups = primitiveGroups.data();
    return result;
}

static std::unordered_map<ccDataHandle, ObjectModelDataImpl> objectModelDatum;

ObjectModelDataImpl* TryAndGetModelData(const ccDataHandle handle)
{
    auto iter = objectModelDatum.find(handle);
    if (iter != objectModelDatum.end())
    {
        return &iter->second;
    }
    else
    {
        return nullptr;
    }
}

void AddObjectModelData(const ccDataHandle handle, ObjectModelDataImpl data)
{
    objectModelDatum.emplace(handle, data);
}