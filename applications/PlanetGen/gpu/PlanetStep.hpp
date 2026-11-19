#pragma once
#ifndef DIAMOND_DOGS_PLANET_STEP_HPP
#define DIAMOND_DOGS_PLANET_STEP_HPP
#include "PlanetGpuErrors.hpp"
#include "PlanetShaders.hpp"
#include "RhiHandle.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rhi
{
    struct CommandBuffer;
    class GpuTimerBlock;
}

namespace planet
{
    class GpuBuffers;

    /** The entry points of one step, passes 1 to 6. Create PlanetShaders with these. */
    inline constexpr std::array<std::string_view, 14> k_StepEntryPoints
    {
        "OwnersCS", "CountVotesCS", "SwapToMajorityCS", "FillGapsCS", "RemoveSubductedCS",
        "CountLiveCellsCS", "OffsetLiveCellsCS", "ScatterLiveCellsCS",
        "CountFrontPointsCS", "OffsetFrontPointsCS", "ScatterFrontPointsCS", "NearestFrontPointCS",
        "SumPullCS", "UpliftCS",
    };

    /** The live cell passes (LiveCells.slang): a group of 256 threads covers 1024 cells of one plate. */
    inline constexpr uint32_t k_LiveGroupSize = 256u;
    inline constexpr uint32_t k_LiveGroupCells = 1024u;

    /** The GPU timer scope of each pass, in the order that RecordStep runs them. */
    inline constexpr std::array<std::string_view, 7> k_StepScopes
    {
        "1 Owners", "2 One plate on top", "3 Crust", "3b Live cells", "4 Front distance", "6 Torque pull", "5 Uplift",
    };

    /** The scope around the whole step, outside the pass scopes. */
    inline constexpr std::string_view k_StepScope{ "Step" };

    /** The crust arrays that the passes change in place. RecordReset restores each from its copy with the prefix
     *  k_InitialPrefix, which the caller creates with the same size. */
    inline constexpr std::string_view k_InitialPrefix{ "Initial." };

    /** How fine the GPU timer scopes of RecordStep are. Dispatches adds a scope inside each pass for each dispatch,
     *  named by its entry point; its extra timestamps cost a little time of their own. */
    enum class StepTimerDetail : uint8_t
    {
        Invalid = 0,
        Passes,
        Dispatches,
    };

    struct StepSize
    {
        uint32_t PointCount{ 0u };
        uint32_t PlateCount{ 0u };
    };

    /** Records passes 1 to 6 of one step into a command buffer, on buffers that GpuBuffers holds by their shader names.
     *  The buffers and the shaders must live longer than the step. README.md lists the buffers and the pass order. */
    class PlanetStep
    {
    public:
        /** @brief Binds `buffers` to every entry point of k_StepEntryPoints. Fails when a buffer that a pass reads is
         *  missing, or when the shaders lack one of those entry points. */
        [[nodiscard]] static PlanetGpuResult<std::unique_ptr<PlanetStep>> Create(const PlanetShaders& shaders,
                                                                                  const GpuBuffers& buffers,
                                                                                  StepSize size);

        PlanetStep(const PlanetShaders& _shaders,
                   const GpuBuffers& _buffers,
                   std::unique_ptr<PassBindings>&& _bindings,
                   rhi::BufferHandle _live_dispatch,
                   StepSize _size) noexcept;
        ~PlanetStep();
        PlanetStep(const PlanetStep&) = delete;
        PlanetStep& operator=(const PlanetStep&) = delete;

        /** @brief Puts the buffers back in the state of the start of the step: the crust arrays from their initial
         *  copies, and every output and counter to zero. With the barriers before (from any earlier compute or copy)
         *  and after (to the compute of the step). Returns the first missing buffer as an error. */
        [[nodiscard]] PlanetGpuError RecordReset(rhi::CommandBuffer& command_buffer) const;

        /** @brief Records the passes, with a barrier between each two dispatches. */
        void RecordStep(rhi::CommandBuffer& command_buffer) const;

        /** @brief Records the passes as RecordStep does, with a GPU timer scope for the step and for each pass
         *  (k_StepScope, k_StepScopes), and for each dispatch when `detail` is Dispatches. */
        void RecordStep(rhi::CommandBuffer& command_buffer, rhi::GpuTimerBlock& timers, StepTimerDetail detail) const;

        [[nodiscard]] StepSize Size() const noexcept;

    private:
        void recordPasses(rhi::CommandBuffer& command_buffer, rhi::GpuTimerBlock* timers, StepTimerDetail detail) const;
        void recordDispatch(rhi::CommandBuffer& command_buffer,
                            rhi::GpuTimerBlock* timers,
                            StepTimerDetail detail,
                            std::string_view entry_point,
                            uint32_t thread_count) const;
        void recordListDispatch(rhi::CommandBuffer& command_buffer,
                                rhi::GpuTimerBlock* timers,
                                StepTimerDetail detail,
                                std::string_view entry_point) const;

        const PlanetShaders& shaders;
        const GpuBuffers& buffers;
        std::unique_ptr<PassBindings> bindings;
        // the group counts of the dispatches on the live cell list, which OffsetLiveCellsCS writes
        rhi::BufferHandle liveDispatch;
        StepSize size;
    };

    /** @brief The crust arrays that RecordReset restores, in the flag storage `storage`. */
    [[nodiscard]] std::array<std::string_view, 6> StepCrustArrays(CrustFlagStorage storage) noexcept;

    /** @brief The plate caps that pass 3b writes for the next step's OwnersCS. They carry from one step to the next, so
     *  RecordReset restores them from their initial copies, as it does the crust arrays. */
    [[nodiscard]] std::span<const std::string_view> StepCarriedBuffers() noexcept;

    /** @brief The name of the initial copy of the crust array `name`: k_InitialPrefix + name. */
    [[nodiscard]] std::string InitialName(std::string_view name);

    /** @brief The workgroups of CountLiveCellsCS and ScatterLiveCellsCS: each plate gets its own row of groups. */
    [[nodiscard]] uint32_t LiveGroupCount(StepSize size) noexcept;

    /** @brief The outputs and counters that RecordReset fills with zero. */
    [[nodiscard]] std::span<const std::string_view> StepZeroedBuffers() noexcept;
}

#endif // !DIAMOND_DOGS_PLANET_STEP_HPP
