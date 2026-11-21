#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_PIPELINES_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_PIPELINES_HPP

// Pipelines.cpp: one CompiledPipeline per Layout pipeline, with the code copied out of the bundle (G4).
namespace loom
{
    struct CompiledData;
    struct ResolvedGraph;

    namespace compile
    {
        void CopyPipelines(const ResolvedGraph& resolved, CompiledData& data);
    }
}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_PIPELINES_HPP
