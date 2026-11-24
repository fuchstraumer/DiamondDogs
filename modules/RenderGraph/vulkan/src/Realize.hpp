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

    [[nodiscard]] RealizeError CheckSupported(const CompiledData& data, const RealizeInputs& inputs);
    [[nodiscard]] RealizeError CreatePipelines(GraphImpl& graph);
    [[nodiscard]] RealizeError CreateDescriptors(GraphImpl& graph, int32_t frame_slots);
    [[nodiscard]] RealizeError CreateFrameSlots(GraphImpl& graph, int32_t frame_slots);
    // Step 7: binds each frame target, and loads the dynamic-state entry points when the graph draws
    [[nodiscard]] RealizeError PrepareTargets(GraphImpl& graph, const RealizeInputs& inputs);
    // Every graph-declared resource but the OfTarget images: those wait for their target's first extent
    void CreateResources(GraphImpl& graph);
    void PrepareCoreState(GraphImpl& graph);

    // An OfTarget image of `row`, at `extent`. Tick makes them when a target's allocation extent changes.
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource> CreateTargetImage(GraphImpl& graph, int32_t row, int32_t copy,
                                                                         Extent2D extent);
    // The frame target that an OfTarget image row follows, or -1
    [[nodiscard]] int32_t TargetOfImage(const CompiledData& data, int32_t row) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_REALIZE_HPP
