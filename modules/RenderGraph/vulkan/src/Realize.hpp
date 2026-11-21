#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_REALIZE_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_REALIZE_HPP
#include "LoomGraph.hpp"
#include <cstdint>

// Realize.cpp: checks that this backend can run the graph, then creates what the graph runs on. Graph::Realize
// (LoomGraph.cpp) calls these in order.
namespace loom
{
    struct CompiledData;
    struct GraphImpl;

    [[nodiscard]] RealizeError CheckSupported(const CompiledData& data);
    [[nodiscard]] RealizeError CreatePipelines(GraphImpl& graph);
    [[nodiscard]] RealizeError CreateDescriptors(GraphImpl& graph, int32_t frame_slots);
    [[nodiscard]] RealizeError CreateFrameSlots(GraphImpl& graph, int32_t frame_slots);
    void CreateResources(GraphImpl& graph);
    void PrepareCoreState(GraphImpl& graph);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_REALIZE_HPP
