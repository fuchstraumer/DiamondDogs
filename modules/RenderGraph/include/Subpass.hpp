#pragma once
#ifndef RENDERGRAPH_SUBPASS_HPP
#define RENDERGRAPH_SUBPASS_HPP
#include <cstdint>

class RenderGraphBuilder;
struct RenderGraphCmdBuffer;
class RenderGraph;

struct RendergraphSubpass_API
{
    void (*SetupPass)(void* instance, RenderGraphBuilder* graphBuilder);
    void (*ExecutePass)(void* instance, const uint64_t sort_key, RenderGraph* graph);
};

#endif //!RENDERGRAPH_SUBPASS_HPP
