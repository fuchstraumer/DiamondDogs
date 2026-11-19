#include "PlanetStep.hpp"
#include "CommandBuffer.hpp"
#include "GpuBuffers.hpp"
#include "GpuTimers.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetShaders.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace planet
{
    namespace
    {
        constexpr std::string_view k_LiveDispatchBuffer{ "LiveDispatch" };

        constexpr std::array<std::string_view, 25> k_ZeroedBuffers
        {
            "BestPlate", "BestCell", "SecondPlate", "SecondCell",
            "PairVotes", "LowVotes",
            "Pass3Plate", "Pass3Cell", "FrontSubduction", "FrontStrength", "FrontSpeed", "FrontSlabAge",
            "PlateLiveCursor", "PlateLiveOffset", "LiveDispatch",
            "PlateFrontPointCount", "PlateFrontPointCursor", "PlateFrontPointOffset", "FrontPoints",
            "CellFrontPoint", "CellDistance",
            "PlateCellCount", "PlateContinentalCount", "PlatePositionSum", "PlatePull",
        };

        // a GPU timer scope when the caller gave a timer block, and nothing when it did not
        class OptionalScope
        {
        public:
            OptionalScope(rhi::GpuTimerBlock* block, std::string_view name)
            {
                if (block != nullptr)
                {
                    scope.emplace(*block, name);
                }
            }

        private:
            std::optional<rhi::ScopedGpuTimer> scope;
        };
    }

    std::array<std::string_view, 6> StepCrustArrays(CrustFlagStorage storage) noexcept
    {
        const CrustFlagBuffers flags = CrustFlagBufferNames(storage);
        return { flags.Alive, flags.Continental, "CrustBirth", "CrustRelief", "CrustOrogeny", "CrustOrogenyTime" };
    }

    std::string InitialName(std::string_view name)
    {
        std::string initial{ k_InitialPrefix };
        initial += name;
        return initial;
    }

    std::span<const std::string_view> StepZeroedBuffers() noexcept
    {
        return k_ZeroedBuffers;
    }

    uint32_t LiveGroupCount(StepSize size) noexcept
    {
        return size.PlateCount * ((size.PointCount + k_LiveGroupCells - 1u) / k_LiveGroupCells);
    }

    PlanetGpuResult<std::unique_ptr<PlanetStep>> PlanetStep::Create(const PlanetShaders& shaders, const GpuBuffers& buffers, StepSize size)
    {
        if (size.PointCount == 0u || size.PlateCount == 0u)
        {
            std::println(stderr, "PlanetStep: a step needs points and plates ({} and {})", size.PointCount, size.PlateCount);
            return std::unexpected(PlanetGpuError::EmptyStep);
        }

        const std::optional<rhi::BufferHandle> liveDispatch = buffers.Handle(k_LiveDispatchBuffer);

        if (!liveDispatch.has_value())
        {
            std::println(stderr, "PlanetStep: no buffer {}", k_LiveDispatchBuffer);
            return std::unexpected(PlanetGpuError::BufferNotFound);
        }

        PlanetGpuResult<std::unique_ptr<PassBindings>> bound = shaders.Bind(buffers.Bound());

        if (!bound.has_value())
        {
            return std::unexpected(bound.error());
        }

        return std::make_unique<PlanetStep>(shaders, buffers, std::move(*bound), *liveDispatch, size);
    }

    PlanetStep::PlanetStep(const PlanetShaders& _shaders,
                           const GpuBuffers& _buffers,
                           std::unique_ptr<PassBindings>&& _bindings,
                           rhi::BufferHandle _live_dispatch,
                           StepSize _size) noexcept :
        shaders{ _shaders },
        buffers{ _buffers },
        bindings{ std::move(_bindings) },
        liveDispatch{ _live_dispatch },
        size{ _size }
    {}

    PlanetStep::~PlanetStep() = default;

    PlanetGpuError PlanetStep::RecordReset(rhi::CommandBuffer& command_buffer) const
    {
        // the copies and fills overwrite what earlier passes, copies and fills wrote or read
        const rhi::GlobalBarrier beforeReset
        {
            rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllTransfer | rhi::PipelineStage::DrawIndirect,
            rhi::Access::ShaderStorageWrite | rhi::Access::TransferWrite,
            rhi::PipelineStage::AllTransfer, rhi::Access::TransferRead | rhi::Access::TransferWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &beforeReset, 1 }, {}, {} });

        for (const std::string_view array : StepCrustArrays(shaders.CrustFlags()))
        {
            const PlanetGpuError copied = buffers.RecordCopy(command_buffer, InitialName(array), array);

            if (copied != PlanetGpuError::Success)
            {
                return copied;
            }
        }

        for (const std::string_view name : k_ZeroedBuffers)
        {
            const PlanetGpuError filled = buffers.RecordFill(command_buffer, name, 0u);

            if (filled != PlanetGpuError::Success)
            {
                return filled;
            }
        }

        const rhi::GlobalBarrier afterReset
        {
            rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &afterReset, 1 }, {}, {} });
        return PlanetGpuError::Success;
    }

    void PlanetStep::RecordStep(rhi::CommandBuffer& command_buffer) const
    {
        recordPasses(command_buffer, nullptr, StepTimerDetail::Passes);
    }

    void PlanetStep::RecordStep(rhi::CommandBuffer& command_buffer, rhi::GpuTimerBlock& timers, StepTimerDetail detail) const
    {
        recordPasses(command_buffer, &timers, detail);
    }

    StepSize PlanetStep::Size() const noexcept
    {
        return size;
    }

    // The live cell list comes right after pass 3, the last pass that changes which cells are alive. Pass 6 runs
    // before pass 5, as in tectonics.Step: the torque balance reads the continental flags before arc accretion
    // changes them. Its sums come from CountLiveCellsCS; SumPullCS adds the pull
    void PlanetStep::recordPasses(rhi::CommandBuffer& command_buffer, rhi::GpuTimerBlock* timers, StepTimerDetail detail) const
    {
        const uint32_t points = size.PointCount;
        const uint32_t liveThreads = LiveGroupCount(size) * k_LiveGroupSize;
        const OptionalScope step(timers, k_StepScope);

        {
            const OptionalScope pass(timers, k_StepScopes[0]);
            recordDispatch(command_buffer, timers, detail, "OwnersCS", points);
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[1]);
            recordDispatch(command_buffer, timers, detail, "CountVotesCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "SwapToMajorityCS", points);
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[2]);
            recordDispatch(command_buffer, timers, detail, "FillGapsCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "RemoveSubductedCS", points);
        }

        RecordComputeBarrier(command_buffer);

        // OffsetLiveCellsCS writes the group counts of the dispatches on the list, so an indirect barrier follows
        {
            const OptionalScope pass(timers, k_StepScopes[3]);
            recordDispatch(command_buffer, timers, detail, "CountLiveCellsCS", liveThreads);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "OffsetLiveCellsCS", 1u);
            RecordIndirectBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "ScatterLiveCellsCS", liveThreads);
        }

        RecordComputeBarrier(command_buffer);

        // OffsetFrontPointsCS is one workgroup: a thread count of 1 dispatches one group, whatever its size
        {
            const OptionalScope pass(timers, k_StepScopes[4]);
            recordDispatch(command_buffer, timers, detail, "CountFrontPointsCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "OffsetFrontPointsCS", 1u);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "ScatterFrontPointsCS", points);
            RecordComputeBarrier(command_buffer);
            recordListDispatch(command_buffer, timers, detail, "NearestFrontPointCS");
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[5]);
            recordDispatch(command_buffer, timers, detail, "SumPullCS", points);
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[6]);
            recordListDispatch(command_buffer, timers, detail, "UpliftCS");
        }
    }

    void PlanetStep::recordDispatch(rhi::CommandBuffer& command_buffer,
                                    rhi::GpuTimerBlock* timers,
                                    StepTimerDetail detail,
                                    std::string_view entry_point,
                                    uint32_t thread_count) const
    {
        rhi::GpuTimerBlock* dispatchTimers = detail == StepTimerDetail::Dispatches ? timers : nullptr;
        const OptionalScope scope(dispatchTimers, entry_point);
        shaders.RecordDispatch(command_buffer, *bindings, entry_point, thread_count);
    }

    void PlanetStep::recordListDispatch(rhi::CommandBuffer& command_buffer,
                                        rhi::GpuTimerBlock* timers,
                                        StepTimerDetail detail,
                                        std::string_view entry_point) const
    {
        rhi::GpuTimerBlock* dispatchTimers = detail == StepTimerDetail::Dispatches ? timers : nullptr;
        const OptionalScope scope(dispatchTimers, entry_point);
        shaders.RecordDispatchIndirect(command_buffer, *bindings, entry_point, liveDispatch, 0u);
    }
}
