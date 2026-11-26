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
        constexpr std::string_view k_FrontTileDispatchBuffer{ "FrontTileDispatch" };
        constexpr std::string_view k_CollideDispatchBuffer{ "CollideDispatch" };

        // pass 3 and 3b write these, and the next step's OwnersCS / FillGapsCS read them
        constexpr std::array<std::string_view, 3> k_CarriedBuffers{ "PlateCapCenters", "PlateCapSpread", "PreviousPlate" };

        constexpr std::array<StepBufferSpec, 31> k_ZeroedBuffers
        {
            StepBufferSpec{ "PairVotes", BufferExtent::PlatesSquared },
            StepBufferSpec{ "LowVotes", BufferExtent::PlatesSquared },
            StepBufferSpec{ "PlateLiveCursor", BufferExtent::Plates },
            StepBufferSpec{ "PlateLiveOffset", BufferExtent::PlatesPlusOne },
            StepBufferSpec{ "LiveDispatch", BufferExtent::IndirectDispatch },
            StepBufferSpec{ "LiveGroupCounts", BufferExtent::LiveGroups },
            StepBufferSpec{ "PlateFrontPointCount", BufferExtent::Plates },
            StepBufferSpec{ "PlateFrontPointCursor", BufferExtent::Plates },
            StepBufferSpec{ "PlateFrontPointOffset", BufferExtent::PlatesPlusOne },
            StepBufferSpec{ "FrontPoints", BufferExtent::PointsTimes4 },
            StepBufferSpec{ "FrontTileCaps", BufferExtent::FrontTilesTimes4 },
            StepBufferSpec{ "FrontTileDispatch", BufferExtent::IndirectDispatch },
            StepBufferSpec{ "PlateContinentalCount", BufferExtent::Plates },
            StepBufferSpec{ "PlateCounters", BufferExtent::PlatesTimes4 },
            StepBufferSpec{ "PlatePull", BufferExtent::PlatesTimes3 },
            StepBufferSpec{ "Owners", BufferExtent::PointsTimes4 },
            StepBufferSpec{ "FrontData", BufferExtent::PointsTimes4 },
            StepBufferSpec{ "CellFronts", BufferExtent::CellsTimes2 },
            StepBufferSpec{ "CollisionPairCount", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionPairSpeedSum", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionPairDeepCount", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionJob", BufferExtent::CollideJob },
            StepBufferSpec{ "CollideDispatch", BufferExtent::CollideDispatch },
            StepBufferSpec{ "BkeParent", BufferExtent::HalfPoints },
            StepBufferSpec{ "BkeMask", BufferExtent::HalfPoints },
            StepBufferSpec{ "BkeSeedFlag", BufferExtent::HalfPoints },
            StepBufferSpec{ "CollideScratchCounters", BufferExtent::CollideCounters },
            StepBufferSpec{ "SuturePositionSum", BufferExtent::SignedVec3 },
            StepBufferSpec{ "SuturePoints", BufferExtent::PointsTimes4 },
            StepBufferSpec{ "SutureDistance", BufferExtent::Points },
            StepBufferSpec{ "EscapeHistogram", BufferExtent::EscapeHistogram },
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

    std::array<std::string_view, 2> StepCrustArrays(CrustFlagStorage storage) noexcept
    {
        return { CrustFlagBufferNames(storage), "CrustState" };
    }

    std::string InitialName(std::string_view name)
    {
        std::string initial{ k_InitialPrefix };
        initial += name;
        return initial;
    }

    size_t StepBufferWordCount(BufferExtent extent, StepSize size) noexcept
    {
        const size_t points = static_cast<size_t>(size.PointCount);
        const size_t plates = static_cast<size_t>(size.PlateCount);

        switch (extent)
        {
        case BufferExtent::Points:
            return points;
        case BufferExtent::PointsTimes4:
            return points * 4u;
        case BufferExtent::Plates:
            return plates;
        case BufferExtent::PlatesPlusOne:
            return plates + 1u;
        case BufferExtent::PlatesSquared:
            return plates * plates;
        case BufferExtent::PlatesTimes3:
            return plates * 3u;
        case BufferExtent::PlatesTimes4:
            return plates * 4u;
        case BufferExtent::Cells:
            return plates * points;
        case BufferExtent::CellsTimes2:
            return plates * points * 2u;
        case BufferExtent::LiveGroups:
            return static_cast<size_t>(LiveGroupCount(size));
        case BufferExtent::FrontTilesTimes4:
            return ((points + 63u) / 64u) * 4u;
        case BufferExtent::IndirectDispatch:
            return 3u;
        case BufferExtent::HalfPoints:
            return points / 2u;
        case BufferExtent::CollideJob:
            return 16u;
        case BufferExtent::CollideDispatch:
            return 12u;
        case BufferExtent::CollideCounters:
            return 8u;
        case BufferExtent::SignedVec3:
            return 3u;
        case BufferExtent::EscapeHistogram:
            return 64u;
        default:
            return 0u;
        }
    }

    std::span<const StepBufferSpec> StepZeroedBuffers() noexcept
    {
        return k_ZeroedBuffers;
    }

    std::span<const std::string_view> StepCarriedBuffers() noexcept
    {
        return k_CarriedBuffers;
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

        const std::optional<rhi::BufferHandle> frontTileDispatch = buffers.Handle(k_FrontTileDispatchBuffer);

        if (!frontTileDispatch.has_value())
        {
            std::println(stderr, "PlanetStep: no buffer {}", k_FrontTileDispatchBuffer);
            return std::unexpected(PlanetGpuError::BufferNotFound);
        }

        const std::optional<rhi::BufferHandle> collideDispatch = buffers.Handle(k_CollideDispatchBuffer);

        if (!collideDispatch.has_value())
        {
            std::println(stderr, "PlanetStep: no buffer {}", k_CollideDispatchBuffer);
            return std::unexpected(PlanetGpuError::BufferNotFound);
        }

        PlanetGpuResult<std::unique_ptr<PassBindings>> bound = shaders.Bind(buffers.Bound());

        if (!bound.has_value())
        {
            return std::unexpected(bound.error());
        }

        return std::make_unique<PlanetStep>(shaders, buffers, std::move(*bound), *liveDispatch, *frontTileDispatch, *collideDispatch, size);
    }

    PlanetStep::PlanetStep(const PlanetShaders& _shaders,
                           const GpuBuffers& _buffers,
                           std::unique_ptr<PassBindings>&& _bindings,
                           rhi::BufferHandle _live_dispatch,
                           rhi::BufferHandle _front_tile_dispatch,
                           rhi::BufferHandle _collide_dispatch,
                           StepSize _size) noexcept :
        shaders{ _shaders },
        buffers{ _buffers },
        bindings{ std::move(_bindings) },
        liveDispatch{ _live_dispatch },
        frontTileDispatch{ _front_tile_dispatch },
        collideDispatch{ _collide_dispatch },
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

        for (const std::string_view carried : k_CarriedBuffers)
        {
            const PlanetGpuError copied = buffers.RecordCopy(command_buffer, InitialName(carried), carried);

            if (copied != PlanetGpuError::Success)
            {
                return copied;
            }
        }

        for (const StepBufferSpec& spec : k_ZeroedBuffers)
        {
            const PlanetGpuError filled = buffers.RecordFill(command_buffer, spec.Name, 0u);

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

    void PlanetStep::RecordLiveStep(rhi::CommandBuffer& command_buffer) const
    {
        RecordStep(command_buffer);
        RecordComputeBarrier(command_buffer);
        shaders.RecordDispatch(command_buffer, *bindings, "MovePlatesAndClearCountersCS", 1u);
    }

    void PlanetStep::RecordDispatch2D(rhi::CommandBuffer& command_buffer,
                                      std::string_view entry_point,
                                      uint32_t width,
                                      uint32_t height) const
    {
        shaders.RecordDispatch2D(command_buffer, *bindings, entry_point, width, height);
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

        {
            const OptionalScope pass(timers, k_StepScopes[3]);
            recordDispatch(command_buffer, timers, detail, "DetectCollisionsCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "SelectCollisionPairCS", 1u);
            RecordIndirectBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "BkeTileInitAndLocalUnionCS", collideDispatch, 0u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "BkeStitchSeamsCS", collideDispatch, 0u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "BkeFlattenCS", collideDispatch, 12u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "MarkTerraneSeedsCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "CountTerraneCS", collideDispatch, 12u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "DecideTransferCS", collideDispatch, 36u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "TransferTerraneDirectCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "ClearSourceTerraneCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "ComputeSutureCapSpreadCS", collideDispatch, 36u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "NearestSuturePointCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "EscapeHistogramCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "EscapePrefixSumCS", collideDispatch, 36u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "ApplyEscapeAndSurgeCS", collideDispatch, 24u);
            RecordComputeBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "RefreshOwnersAfterCollisionCS", collideDispatch, 24u);
        }

        RecordComputeBarrier(command_buffer);

        // OffsetLiveCellsCS writes the group counts of the dispatches on the list, so an indirect barrier follows
        {
            const OptionalScope pass(timers, k_StepScopes[4]);
            recordDispatch(command_buffer, timers, detail, "CountLiveCellsCS", liveThreads);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "OffsetLiveCellsCS", 1u);
            RecordIndirectBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "ScatterLiveCellsCS", liveThreads);
        }

        RecordComputeBarrier(command_buffer);

        // OffsetFrontPointsCS is one workgroup: a thread count of 1 dispatches one group, whatever its size
        {
            const OptionalScope pass(timers, k_StepScopes[5]);
            recordDispatch(command_buffer, timers, detail, "CountFrontPointsCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "OffsetFrontPointsCS", 1u);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "ScatterFrontPointsCS", points);
            RecordComputeBarrier(command_buffer);
            recordDispatch(command_buffer, timers, detail, "SortFrontPointsCS", size.PlateCount * k_SortGroupSize);
            RecordIndirectBarrier(command_buffer);
            recordIndirectDispatch(command_buffer, timers, detail, "BuildFrontTileCapsCS", frontTileDispatch);
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[6]);
            recordDispatch(command_buffer, timers, detail, "SumPullCS", points);
        }

        RecordComputeBarrier(command_buffer);

        {
            const OptionalScope pass(timers, k_StepScopes[7]);
            recordIndirectDispatch(command_buffer, timers, detail, "FrontDistanceAndUpliftCS", liveDispatch);
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

    void PlanetStep::recordIndirectDispatch(rhi::CommandBuffer& command_buffer,
                                            rhi::GpuTimerBlock* timers,
                                            StepTimerDetail detail,
                                            std::string_view entry_point,
                                            rhi::BufferHandle arguments,
                                            uint64_t byte_offset) const
    {
        rhi::GpuTimerBlock* dispatchTimers = detail == StepTimerDetail::Dispatches ? timers : nullptr;
        const OptionalScope scope(dispatchTimers, entry_point);
        shaders.RecordDispatchIndirect(command_buffer, *bindings, entry_point, arguments, byte_offset);
    }
}
