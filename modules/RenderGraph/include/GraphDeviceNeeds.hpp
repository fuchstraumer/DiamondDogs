#pragma once
#ifndef DIAMOND_DOGS_LOOM_GRAPH_DEVICE_NEEDS_HPP
#define DIAMOND_DOGS_LOOM_GRAPH_DEVICE_NEEDS_HPP
#include "DeviceRequirements.hpp"
#include <string_view>
#include <vector>

// What a graph asks of the device before the device exists: the input to bootstrap's GatherDeviceRequirements.
namespace loom
{

    class GraphBuilder;

    // Needs: one neutral capability need per capability that the graph's statements imply. Required for a pass
    // outside every path list and for the last path of a list (the fallback must run); Wanted for the other paths.
    // WantedOnly: the entry points that only a higher path uses, so that bootstrap makes their shader needs Wanted.
    // Both view into the builder's record: use them before the builder changes or goes.
    struct GraphDeviceNeeds
    {
        std::vector<bootstrap::Need> Needs;
        std::vector<bootstrap::EntryPointRef> WantedOnly;
    };

    // `bundle_file` is the bundle's file name as the content stage opened it (NeedSource::BundleFile). Pass the result
    // to GatherDeviceRequirements as extra_needs and wanted_only.
    [[nodiscard]] GraphDeviceNeeds GatherDeviceNeeds(const GraphBuilder& builder, std::string_view bundle_file);

}

#endif // !DIAMOND_DOGS_LOOM_GRAPH_DEVICE_NEEDS_HPP
