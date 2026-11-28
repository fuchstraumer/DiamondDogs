#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP
#define DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP

#include "GraphBuilder.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "MantlePlumes.hpp"
#include "PassBuilders.hpp"
#include "PlanetPresets.hpp"
#include "RecordContext.hpp"
#include "ResourceTypes.hpp"
#include "SequenceBuilder.hpp"
#include "ShaderLibrary.host.hpp"
#include "StepState.hpp"
#include "PlanetProjections.hpp"
#include "utility/Delegate.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

class ResourceContext;

namespace planet_loom
{
    using PresentMapParamsHost = planet_gen_shaders::PresentMapParams<
        planet_gen_shaders::CookProfile::Spirv,
        ::lodestone::LayoutRule::Std140>;

    struct RenderMapView
    {
        uint32_t Mode{ 0u };
        RenderProjection Projection{ RenderProjection::Equirectangular };
        float CenterLonDeg{ 0.0f };
        float CenterLatDeg{ 0.0f };
        float CropMinU{ 0.0f };
        float CropMinV{ 0.0f };
        float CropMaxU{ 1.0f };
        float CropMaxV{ 1.0f };
    };

    struct PlanetGraphConfig
    {
        uint32_t HealpixOrder{ 7u };
        uint32_t PlateCount{ 20u };
        uint32_t MapWidth{ 1024u };
        uint32_t MapHeight{ 512u };
        bool UseByteCrustFlags{ true };
        bool RenderOnTrigger{ false };
        bool InteractiveControl{ false };
        bool LiveMode{ false };
        bool EnableCapturePass{ false };
        bool EnableTelemetry{ false };
        bool EnableStateDump{ false };
        // The verify run sets it: the crust passes then do what the frozen reference does (subduction kills
        // per world point, and every gap gets new crust).
        bool FrozenReferenceCrust{ false };
    };

    // Imported uniform buffers
    inline constexpr loom::ResourceKey<loom::Buffer> k_StepParams{ "Planet.StepParams" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_Pass3Constants{ "Planet.Pass3Constants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_UpliftConstants{ "Planet.UpliftConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_ShapingConstants{ "Planet.ShapingConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_MovePlatesConstants{ "Planet.MovePlatesConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlumeConstants{ "Planet.PlumeConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_RenderMapConstants{ "Planet.RenderMapConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PresentConstants{ "Planet.PresentConstants" };

    // Imported persistent simulation state buffers
    inline constexpr loom::ResourceKey<loom::Buffer> k_CrustFlags{ "Planet.CrustFlags" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CrustState{ "Planet.CrustState" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_Rotations{ "Planet.Rotations" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PoleRates{ "Planet.PoleRates" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateRift{ "Planet.PlateRift" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateCapCenters{ "Planet.PlateCapCenters" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateCapSpread{ "Planet.PlateCapSpread" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PreviousPlate{ "Planet.PreviousPlate" };

    // Declared persistent counter buffers (zeroed once before frame 0, cleared every step by MovePlatesAndClearCountersCS)
    inline constexpr loom::ResourceKey<loom::Buffer> k_PairVotes{ "Planet.PairVotes" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LowVotes{ "Planet.LowVotes" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollisionPairCount{ "Planet.CollisionPairCount" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollisionPairSpeedSum{ "Planet.CollisionPairSpeedSum" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollisionPairDeepCount{ "Planet.CollisionPairDeepCount" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateLiveCursor{ "Planet.PlateLiveCursor" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateFrontPointCount{ "Planet.PlateFrontPointCount" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateFrontPointCursor{ "Planet.PlateFrontPointCursor" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateContinentalCount{ "Planet.PlateContinentalCount" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateCounters{ "Planet.PlateCounters" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlatePull{ "Planet.PlatePull" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateIncubation{ "Planet.PlateIncubation" };

    // Declared scratch (headless) / persistent-in-live-mode buffers
    inline constexpr loom::ResourceKey<loom::Buffer> k_Owners{ "Planet.Owners" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontData{ "Planet.FrontData" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollisionJob{ "Planet.CollisionJob" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollideDispatch{ "Planet.CollideDispatch" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CollideScratchCounters{ "Planet.CollideScratchCounters" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_SuturePositionSum{ "Planet.SuturePositionSum" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_EscapeHistogram{ "Planet.EscapeHistogram" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_BkeParent{ "Planet.BkeParent" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_BkeMask{ "Planet.BkeMask" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_BkeSeedFlag{ "Planet.BkeSeedFlag" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_SuturePoints{ "Planet.SuturePoints" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_SutureDistance{ "Planet.SutureDistance" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_GapSource{ "Planet.GapSource" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveGroupCounts{ "Planet.LiveGroupCounts" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateLiveOffset{ "Planet.PlateLiveOffset" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveDispatch{ "Planet.LiveDispatch" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveCell{ "Planet.LiveCell" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateFrontPointOffset{ "Planet.PlateFrontPointOffset" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontTileDispatch{ "Planet.FrontTileDispatch" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontPoints{ "Planet.FrontPoints" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontTileCaps{ "Planet.FrontTileCaps" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CellFronts{ "Planet.CellFronts" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_RiftJob{ "Planet.RiftJob" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_ErodedRelief{ "Planet.ErodedRelief" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_RiftDispatch{ "Planet.RiftDispatch" };

    // Optional GPU telemetry and native 1D HEALPix state dump buffers
    inline constexpr loom::ResourceKey<loom::Buffer> k_TelemetryCounters{ "Planet.TelemetryCounters" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_TelemetryCellSnap{ "Planet.TelemetryCellSnap" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_HealpixStateDump{ "Planet.HealpixStateDump" };
    inline constexpr uint32_t k_TelemetryCounterWordCount = 160u;
    inline constexpr uint32_t k_HealpixDumpWordsPerCell = 6u;

    namespace telemetry_slots
    {
        inline constexpr uint32_t k_StepsObserved = 0u;
        inline constexpr uint32_t k_CollisionsTriggered = 1u;
        inline constexpr uint32_t k_TerraneCellsTransferred = 2u;
        inline constexpr uint32_t k_OceanCellsBorn = 3u;
        inline constexpr uint32_t k_SubductionOverlapPoints = 4u;
        inline constexpr uint32_t k_UpliftOceanToContFlips = 5u;
        inline constexpr uint32_t k_UpliftVolcanicToContFlips = 6u;
        inline constexpr uint32_t k_UpliftVolcanicOverwritten = 7u;
        inline constexpr uint32_t k_PlumeOceanToContFlips = 8u;
        inline constexpr uint32_t k_PlumeVolcanicCellsActive = 9u;
        inline constexpr uint32_t k_RiftsCompleted = 10u;
        inline constexpr uint32_t k_RiftsBlockedNoFreeSlot = 11u;
        inline constexpr uint32_t k_RiftsBlockedNotWarm = 12u;
        inline constexpr uint32_t k_RiftsBlockedTooSmall = 13u;
        inline constexpr uint32_t k_RiftsBlockedPickFailed = 14u;
        inline constexpr uint32_t k_CollapseCellsActive = 15u;
        inline constexpr uint32_t k_SnapOverflowCells = 16u;

        inline constexpr uint32_t k_UpliftHeightAddedMetersLo = 18u;
        inline constexpr uint32_t k_UpliftHeightAddedMetersHi = 19u;
        inline constexpr uint32_t k_UpliftLinearErosionMetersLo = 20u;
        inline constexpr uint32_t k_UpliftLinearErosionMetersHi = 21u;
        inline constexpr uint32_t k_PlumeHeightAddedMetersLo = 22u;
        inline constexpr uint32_t k_PlumeHeightAddedMetersHi = 23u;
        inline constexpr uint32_t k_PlumeOceanicErosionMetersLo = 24u;
        inline constexpr uint32_t k_PlumeOceanicErosionMetersHi = 25u;
        inline constexpr uint32_t k_ErosionDiffusionAddedMetersLo = 26u;
        inline constexpr uint32_t k_ErosionDiffusionAddedMetersHi = 27u;
        inline constexpr uint32_t k_ErosionRemovedMetersLo = 28u;
        inline constexpr uint32_t k_ErosionRemovedMetersHi = 29u;

        inline constexpr uint32_t k_PlateFrameLiveCells = 32u;
        inline constexpr uint32_t k_WorldCells = 33u;
        inline constexpr uint32_t k_ContinentalCells = 34u;
        inline constexpr uint32_t k_OceanicCells = 35u;
        inline constexpr uint32_t k_VolcanicOceanicCells = 36u;
        inline constexpr uint32_t k_AndeanCells = 37u;
        inline constexpr uint32_t k_HimalayanCells = 38u;
        inline constexpr uint32_t k_IslandArcCells = 39u;
        inline constexpr uint32_t k_SubductionFrontCells = 40u;
        inline constexpr uint32_t k_CollisionFrontCells = 41u;
        inline constexpr uint32_t k_YoungOceanCellsUnder5My = 42u;
        inline constexpr uint32_t k_OceanAgeSumTenthsMy = 43u;
        inline constexpr uint32_t k_LandBelow05KmCells = 44u;
        inline constexpr uint32_t k_LandAbove5KmCells = 45u;
        inline constexpr uint32_t k_ReliefMaxMm = 46u;
        inline constexpr uint32_t k_LastStepCollisionFired = 47u;
        inline constexpr uint32_t k_LastStepRiftOutcome = 48u;
        inline constexpr uint32_t k_OceanAgeSumTenthsMyHi = 49u;

        inline constexpr uint32_t k_PlateCellCountBase = 64u;
        inline constexpr uint32_t k_PlateContCountBase = 96u;
        inline constexpr uint32_t k_ReliefHistBase = 128u;
        inline constexpr uint32_t k_ReliefHistBins = 16u;
    }

    // Output map image, sampler, and swapchain frame target
    inline constexpr loom::ResourceKey<loom::Image> k_MapOutput{ "Planet.MapOutput" };
    inline constexpr loom::ResourceKey<loom::Sampler> k_MapSampler{ "Planet.MapSampler" };
    inline constexpr loom::FrameTargetKey k_Screen{ "Planet.Screen" };

    // Sequence and passes
    inline constexpr loom::SequenceKey k_StepSequence{ "Planet.StepSequence" };
    inline constexpr loom::PassKey k_InitZeroCountersPass{ "Planet.InitZeroCounters" };
    inline constexpr loom::PassKey k_OwnersPass{ "Planet.Pass.Owners" };
    inline constexpr loom::PassKey k_RenderMapPass{ "Planet.RenderMap" };
    inline constexpr loom::PassKey k_DumpHealpixStatePass{ "Planet.DumpHealpixState" };

    // The same dump, placed inside the step right after a pass that kills, creates or reassigns cells. A stage dump
    // runs one simulation step and captures the state at that point of it. Note: stages before RemoveSubductedCS
    // (after_swap_to_majority, after_fill_gaps) capture k_FrontData from step N-1; after_remove_subducted and later
    // capture step N's fronts.
    inline constexpr loom::PassKey k_DumpAfterSwapToMajorityPass{ "Planet.DumpHealpixState.AfterSwapToMajority" };
    inline constexpr loom::PassKey k_DumpAfterFillGapsPass{ "Planet.DumpHealpixState.AfterFillGaps" };
    inline constexpr loom::PassKey k_DumpAfterRemoveSubductedPass{ "Planet.DumpHealpixState.AfterRemoveSubducted" };
    inline constexpr loom::PassKey k_DumpAfterCollisionPass{ "Planet.DumpHealpixState.AfterCollision" };
    inline constexpr loom::PassKey k_DumpAfterErosionPass{ "Planet.DumpHealpixState.AfterErosion" };
    inline constexpr loom::PassKey k_DumpAfterRiftPass{ "Planet.DumpHealpixState.AfterRift" };

    struct DumpStage
    {
        std::string_view Name;
        loom::PassKey Pass;
    };

    inline constexpr std::array<DumpStage, 6> k_DumpStages
    {
        DumpStage{ "after_swap_to_majority", k_DumpAfterSwapToMajorityPass },
        DumpStage{ "after_fill_gaps", k_DumpAfterFillGapsPass },
        DumpStage{ "after_remove_subducted", k_DumpAfterRemoveSubductedPass },
        DumpStage{ "after_collision", k_DumpAfterCollisionPass },
        DumpStage{ "after_erosion", k_DumpAfterErosionPass },
        DumpStage{ "after_rift", k_DumpAfterRiftPass },
    };

    inline constexpr loom::PassKey k_CaptureMapPass{ "Planet.CaptureMap" };
    inline constexpr loom::PassKey k_PresentMapPass{ "Planet.PresentMap" };
    inline constexpr loom::PassKey k_UiPass{ "Planet.Ui" };

    /** Returns all pass keys registered by RegisterPlanetGraph, for GPU timer hash lookup. */
    [[nodiscard]] std::span<const loom::PassKey> AllPlanetPassKeys() noexcept;


    /** Records vkCmdFillBuffer(0) on all 12 persistent counter buffers (and live display / telemetry buffers when configured). */
    class ZeroCountersRecorder
    {
    public:
        void SetIncludeLiveDisplayBuffers(bool include) noexcept;
        void SetIncludeTelemetryBuffers(bool include) noexcept;
        void Record(loom::RecordContext& ctx);

    private:
        bool includeLiveDisplayBuffers{ false };
        bool includeTelemetryBuffers{ false };
    };

    /** Optional callbacks supplied to RegisterPlanetGraph when capture or ImGui passes are active. */
    struct PlanetGraphCallbacks
    {
        ZeroCountersRecorder* ZeroCounters{ nullptr };
        Delegate<void(loom::RecordContext&)> CaptureCallback{};
        Delegate<void(loom::RecordContext&)> UiCallback{};
    };

    /** Declares/imports all resources and adds the PlanetGen simulation + render/present sequence to `graph`. */
    void RegisterPlanetGraph(loom::GraphBuilder& graph,
                             const PlanetGraphConfig& config,
                             const PlanetGraphCallbacks& callbacks);

    /** Triggers all simulation passes (OwnersCS .. MovePlatesAndClearCountersCS) for a single OnTrigger live step. */
    void TriggerSimulationStep(loom::Graph& graph);

    /** Triggers the optional strip-able GPU telemetry passes for one OnTrigger live step. */
    void TriggerTelemetryStep(loom::Graph& graph);

    /** Triggers zeroing of live display/counter buffers and initial OwnersCS evaluation for step-0 live view. */
    void TriggerInitialLiveView(loom::Graph& graph);

    /** Owns the imported host/GPU buffer handles supplied to a realized PlanetGen Loom graph. */
    struct PlanetGraphImports
    {
        GraphicsResource StepParams;
        GraphicsResource Pass3Constants;
        GraphicsResource UpliftConstants;
        GraphicsResource ShapingConstants;
        GraphicsResource MovePlatesConstants;
        GraphicsResource PlumeConstants;
        GraphicsResource RenderMapConstants;
        GraphicsResource PresentConstants;

        GraphicsResource CrustFlags;
        GraphicsResource CrustState;
        GraphicsResource Rotations;
        GraphicsResource PoleRates;
        GraphicsResource PlateRift;
        GraphicsResource PlateCapCenters;
        GraphicsResource PlateCapSpread;
        GraphicsResource PreviousPlate;

        // DecideRiftCS splits plates only when this is set, and only into free plate slots (AddRiftSlots)
        bool RiftEnabled{ false };

        [[nodiscard]] bool SupplyAll(loom::Graph& graph,
                                     ResourceContext& resources,
                                     const planet::HealpixInitialState& initial,
                                     float dt,
                                     uint32_t order,
                                     uint32_t width,
                                     uint32_t height,
                                     const RenderMapView& view,
                                     bool use_bytes,
                                     const PlumeSystemState& plumes,
                                     const ShapingState& shaping);

        [[nodiscard]] bool SupplyPresentConstants(loom::Graph& graph,
                                                  ResourceContext& resources,
                                                  const PresentMapParamsHost& params);

        [[nodiscard]] bool UpdateUniforms(ResourceContext& resources,
                                          float time,
                                          uint32_t width,
                                          uint32_t height,
                                          const RenderMapView& view,
                                          const PlumeSystemState& plumes,
                                          const ShapingState& shaping);

        [[nodiscard]] bool UpdatePresentConstants(ResourceContext& resources,
                                                  const PresentMapParamsHost& params);

        [[nodiscard]] bool UpdateStepDt(ResourceContext& resources,
                                        const planet::HealpixInitialState& initial,
                                        float dt,
                                        uint32_t order);

        [[nodiscard]] bool ResetSimulationState(loom::Graph& graph,
                                                ResourceContext& resources,
                                                const planet::HealpixInitialState& initial,
                                                float dt,
                                                uint32_t order,
                                                uint32_t width,
                                                uint32_t height,
                                                const RenderMapView& view,
                                                bool use_bytes,
                                                const PlumeSystemState& plumes,
                                                const ShapingState& shaping);

        void Destroy(ResourceContext& resources);
    };
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP