#pragma once
#ifndef DIAMOND_DOGS_LOOM_WEBGPU_REALIZE_HPP
#define DIAMOND_DOGS_LOOM_WEBGPU_REALIZE_HPP
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
    // Sizes each pass's runtime: groups, ring phases, cached bind groups and epochs.
    void CreatePassRuntimes(GraphImpl& graph);
    // The one params buffer, and each pass's slice of it. Needs the pass runtimes.
    [[nodiscard]] RealizeError CreateParamsBuffer(GraphImpl& graph);
    void CreateResources(GraphImpl& graph);
    void PrepareCoreState(GraphImpl& graph);

}

#endif // !DIAMOND_DOGS_LOOM_WEBGPU_REALIZE_HPP
