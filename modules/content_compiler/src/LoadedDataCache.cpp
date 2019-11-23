#include "LoadedDataCache.hpp"
#include <variant>
#include <unordered_map>
#include "mango/core/hash.hpp"

namespace std
{
    template<>
    struct hash<mango::XX3HASH128>
    {
        size_t operator()(const mango::XX3HASH128& hash_val) const noexcept
        {
            return hash<uint64_t>()(hash_val.data[0]) ^ hash<uint64_t>()(hash_val.data[1]);
        }
    };
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