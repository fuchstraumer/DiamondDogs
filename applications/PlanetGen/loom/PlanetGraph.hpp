#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP
#define DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP

#include "GraphBuilder.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "RecordContext.hpp"
#include "ResourceTypes.hpp"
#include "SequenceBuilder.hpp"
#include "ShaderLibrary.host.hpp"
#include "StepState.hpp"
#include <array>
#include <cstdint>
#include <string_view>

class ResourceContext;

namespace planet_loom
{
    struct PlanetGraphConfig
    {
        uint32_t HealpixOrder{ 7u };
        uint32_t PlateCount{ 20u };
        uint32_t MapWidth{ 1024u };
        uint32_t MapHeight{ 512u };
        bool UseByteCrustFlags{ true };
        bool RenderOnTrigger{ false };
    };

    // Imported uniform buffers
    inline constexpr loom::ResourceKey<loom::Buffer> k_StepParams{ "Planet.StepParams" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_Pass3Constants{ "Planet.Pass3Constants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_UpliftConstants{ "Planet.UpliftConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_MovePlatesConstants{ "Planet.MovePlatesConstants" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_RenderMapConstants{ "Planet.RenderMapConstants" };

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

    // Declared frame scratch buffers (first-written each frame with AccessModifier::WriteOnly)
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
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveGroupCounts{ "Planet.LiveGroupCounts" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateLiveOffset{ "Planet.PlateLiveOffset" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveDispatch{ "Planet.LiveDispatch" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_LiveCell{ "Planet.LiveCell" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_PlateFrontPointOffset{ "Planet.PlateFrontPointOffset" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontTileDispatch{ "Planet.FrontTileDispatch" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontPoints{ "Planet.FrontPoints" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_FrontTileCaps{ "Planet.FrontTileCaps" };
    inline constexpr loom::ResourceKey<loom::Buffer> k_CellFronts{ "Planet.CellFronts" };

    // Output map image
    inline constexpr loom::ResourceKey<loom::Image> k_MapOutput{ "Planet.MapOutput" };

    // Sequence and passes
    inline constexpr loom::SequenceKey k_StepSequence{ "Planet.StepSequence" };
    inline constexpr loom::PassKey k_InitZeroCountersPass{ "Planet.InitZeroCounters" };
    inline constexpr loom::PassKey k_RenderMapPass{ "Planet.RenderMap" };

    /** Returns all pass keys registered by RegisterPlanetGraph, for GPU timer hash lookup. */
    [[nodiscard]] const std::array<loom::PassKey, 33>& AllPlanetPassKeys() noexcept;

    /** Records vkCmdFillBuffer(0) on all 11 persistent counter buffers before frame 0. */
    class ZeroCountersRecorder
    {
    public:
        void Record(loom::RecordContext& ctx);
    };

    /** Declares/imports all resources and adds the 33-pass PlanetGen simulation + render sequence to `graph`. */
    void RegisterPlanetGraph(loom::GraphBuilder& graph,
                             const PlanetGraphConfig& config,
                             ZeroCountersRecorder& zero_recorder);

    /** Owns the imported host/GPU buffer handles supplied to a realized PlanetGen Loom graph. */
    struct PlanetGraphImports
    {
        GraphicsResource StepParams;
        GraphicsResource Pass3Constants;
        GraphicsResource UpliftConstants;
        GraphicsResource MovePlatesConstants;
        GraphicsResource RenderMapConstants;

        GraphicsResource CrustFlags;
        GraphicsResource CrustState;
        GraphicsResource Rotations;
        GraphicsResource PoleRates;
        GraphicsResource PlateRift;
        GraphicsResource PlateCapCenters;
        GraphicsResource PlateCapSpread;
        GraphicsResource PreviousPlate;

        [[nodiscard]] bool SupplyAll(loom::Graph& graph,
                                     ResourceContext& resources,
                                     const planet::HealpixInitialState& initial,
                                     float dt,
                                     uint32_t order,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t mode,
                                     bool use_bytes);

        [[nodiscard]] bool UpdateUniforms(ResourceContext& resources,
                                          float time,
                                          uint32_t width,
                                          uint32_t height,
                                          uint32_t mode);

        void Destroy(ResourceContext& resources);
    };
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_GRAPH_HPP
