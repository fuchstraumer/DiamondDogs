#include "MeshProcessing.hpp"
#include "LoadedDataCache.hpp"
#include <algorithm>

void sortMaterialRangesInRange(ObjectModelDataImpl* data, const size_t begin, const size_t end)
{
    auto materialRangeSorter = [](const MaterialRange& r0, const MaterialRange& r1)
    {
        if (int comparisonResult = strcmp(r0.MaterialName, r1.MaterialName); comparisonResult != 0)
        {
            return comparisonResult < 0;
        }
        else
        {
            return r0.startIndex < r1.startIndex;
        }
    };

    std::sort(data->materialRanges.begin() + begin, data->materialRanges.begin() + end, materialRangeSorter);

    std::vector<MaterialRange> sortedRanges;
    sortedRanges.reserve(end - begin);

    std::vector<uint32_t> indicesReordered;
}

void SortMeshMaterialRanges(const ccDataHandle handle)
{
    ObjectModelDataImpl* modelData = TryAndGetModelData(handle);
    if (!modelData)
    {
        return;
    }

    
}
