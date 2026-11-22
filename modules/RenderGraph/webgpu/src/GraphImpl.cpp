#include "GraphImpl.hpp"
#include "CompiledGraph.hpp"
#include "LoomGraph.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include <webgpu/webgpu_cpp.h>
#include <cstdint>
#include <utility>

namespace loom
{

    GraphImpl::GraphImpl(CompiledGraph&& _compiled, const RealizeInputs& inputs) :
        Compiled{ std::move(_compiled) },
        Resources{ inputs.Resources },
        Sink{ inputs.Sink }
    {
        const rhi::WebGpuRhiContextView view = rhi::UnwrapWebGpuRhiContext(inputs.Context);
        Device = view.Device;
        Queue = view.Queue;
    }

    // Does not block. The resource context destroys a resource when the device is done with it, and Dawn keeps the
    // pipelines, bind groups and buffers alive while a submitted command buffer uses them.
    GraphImpl::~GraphImpl()
    {
        const auto destroyOwned = [this](const rhi::ResourceFuture<GraphicsResource>& future)
        {
            const rhi::FutureState state = future.State();

            if (state != rhi::FutureState::Created && state != rhi::FutureState::Complete)
            {
                return;
            }

            [[maybe_unused]] rhi::OperationFuture destroyed = Resources.DestroyResource(future.Get());
        };

        for (const RowRuntime& row : Rows)
        {
            if (row.Owned)
            {
                destroyOwned(row.Future);
            }
        }

        // the graph makes every history copy
        for (const rhi::ResourceFuture<GraphicsResource>& copy : ExtraCopies)
        {
            destroyOwned(copy);
        }

        if (ParamsBuffer)
        {
            ParamsBuffer.Destroy();
        }
    }

    const rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(const GraphImpl& graph, int32_t copy) noexcept
    {
        const int32_t rowCount = static_cast<int32_t>(graph.Rows.size());
        return copy < rowCount ? graph.Rows[copy].Future : graph.ExtraCopies[copy - rowCount];
    }

}
