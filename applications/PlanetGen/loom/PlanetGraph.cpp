#include "PlanetGraph.hpp"
#include "CommandBuffer.hpp"
#include "Declarations.hpp"
#include "GraphBuilder.hpp"
#include "Healpix.hpp"
#include "LoomGraph.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "PassStatements.hpp"
#include "PlanetStep.hpp"
#include "RecordContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiHandle.hpp"
#include "RhiResourceFlags.hpp"
#include "SequenceBuilder.hpp"
#include "ShaderLibrary.host.hpp"
#include "ShaderLibraryTypes.hpp"
#include "StepState.hpp"
#include "math/MathStorage.hpp"
#include "utility/Delegate.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace planet_loom
{
    namespace
    {
        namespace shaders = planet_gen_shaders;
        namespace pl = planet_gen_shaders::PlanetLab;

        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

        using StepParamsHost = shaders::OwnersParams<shaders::CookProfile::Spirv, ::lodestone::LayoutRule::Std140>;
        using Pass3ParamsHost = shaders::Pass3Params<shaders::CookProfile::Spirv, ::lodestone::LayoutRule::Std140>;
        using UpliftParamsHost = shaders::UpliftParams<shaders::CookProfile::Spirv, ::lodestone::LayoutRule::Std140>;
        using MovePlatesParamsHost = shaders::MovePlatesParams<shaders::CookProfile::Spirv, ::lodestone::LayoutRule::Std140>;
        using RenderMapParamsHost = shaders::RenderMapParams<shaders::CookProfile::Spirv, ::lodestone::LayoutRule::Std140>;

        inline constexpr std::array<loom::ResourceKey<loom::Buffer>, 12> k_PersistentCounters
        {
            k_PairVotes,
            k_LowVotes,
            k_CollisionPairCount,
            k_CollisionPairSpeedSum,
            k_CollisionPairDeepCount,
            k_PlateLiveCursor,
            k_PlateFrontPointCount,
            k_PlateFrontPointCursor,
            k_PlateContinentalCount,
            k_PlateCounters,
            k_PlatePull,
            k_PlateIncubation,
        };

        inline constexpr loom::PassKey k_CountVotesPass{ "Planet.Pass.CountVotes" };
        inline constexpr loom::PassKey k_SwapToMajorityPass{ "Planet.Pass.SwapToMajority" };
        inline constexpr loom::PassKey k_FillGapsPass{ "Planet.Pass.FillGaps" };
        inline constexpr loom::PassKey k_RemoveSubductedPass{ "Planet.Pass.RemoveSubducted" };
        inline constexpr loom::PassKey k_DetectCollisionsPass{ "Planet.Pass.DetectCollisions" };
        inline constexpr loom::PassKey k_SelectCollisionPairPass{ "Planet.Pass.SelectCollisionPair" };
        inline constexpr loom::PassKey k_BkeTileInitAndLocalUnionPass{ "Planet.Pass.BkeTileInitAndLocalUnion" };
        inline constexpr loom::PassKey k_BkeStitchSeamsPass{ "Planet.Pass.BkeStitchSeams" };
        inline constexpr loom::PassKey k_BkeFlattenPass{ "Planet.Pass.BkeFlatten" };
        inline constexpr loom::PassKey k_MarkTerraneSeedsPass{ "Planet.Pass.MarkTerraneSeeds" };
        inline constexpr loom::PassKey k_CountTerranePass{ "Planet.Pass.CountTerrane" };
        inline constexpr loom::PassKey k_DecideTransferPass{ "Planet.Pass.DecideTransfer" };
        inline constexpr loom::PassKey k_TransferTerraneDirectPass{ "Planet.Pass.TransferTerraneDirect" };
        inline constexpr loom::PassKey k_ClearSourceTerranePass{ "Planet.Pass.ClearSourceTerrane" };
        inline constexpr loom::PassKey k_ComputeSutureCapSpreadPass{ "Planet.Pass.ComputeSutureCapSpread" };
        inline constexpr loom::PassKey k_NearestSuturePointPass{ "Planet.Pass.NearestSuturePoint" };
        inline constexpr loom::PassKey k_EscapeHistogramPass{ "Planet.Pass.EscapeHistogram" };
        inline constexpr loom::PassKey k_EscapePrefixSumPass{ "Planet.Pass.EscapePrefixSum" };
        inline constexpr loom::PassKey k_ApplyEscapeAndSurgePass{ "Planet.Pass.ApplyEscapeAndSurge" };
        inline constexpr loom::PassKey k_RefreshOwnersAfterCollisionPass{ "Planet.Pass.RefreshOwnersAfterCollision" };
        inline constexpr loom::PassKey k_CountLiveCellsPass{ "Planet.Pass.CountLiveCells" };
        inline constexpr loom::PassKey k_OffsetLiveCellsPass{ "Planet.Pass.OffsetLiveCells" };
        inline constexpr loom::PassKey k_ScatterLiveCellsPass{ "Planet.Pass.ScatterLiveCells" };
        inline constexpr loom::PassKey k_CountFrontPointsPass{ "Planet.Pass.CountFrontPoints" };
        inline constexpr loom::PassKey k_OffsetFrontPointsPass{ "Planet.Pass.OffsetFrontPoints" };
        inline constexpr loom::PassKey k_ScatterFrontPointsPass{ "Planet.Pass.ScatterFrontPoints" };
        inline constexpr loom::PassKey k_SortFrontPointsPass{ "Planet.Pass.SortFrontPoints" };
        inline constexpr loom::PassKey k_BuildFrontTileCapsPass{ "Planet.Pass.BuildFrontTileCaps" };
        inline constexpr loom::PassKey k_SumPullPass{ "Planet.Pass.SumPull" };
        inline constexpr loom::PassKey k_FrontDistanceAndUpliftPass{ "Planet.Pass.FrontDistanceAndUplift" };
        inline constexpr loom::PassKey k_ApplyMantlePlumesPass{ "Planet.Pass.ApplyMantlePlumes" };
        inline constexpr loom::PassKey k_ErodeReliefPass{ "Planet.Pass.ErodeRelief" };
        inline constexpr loom::PassKey k_ApplyErosionPass{ "Planet.Pass.ApplyErosion" };
        inline constexpr loom::PassKey k_DecideRiftPass{ "Planet.Pass.DecideRift" };
        inline constexpr loom::PassKey k_SplitPlatePass{ "Planet.Pass.SplitPlate" };
        inline constexpr loom::PassKey k_RefreshOwnersAfterRiftPass{ "Planet.Pass.RefreshOwnersAfterRift" };
        inline constexpr loom::PassKey k_MovePlatesAndClearCountersPass{ "Planet.Pass.MovePlatesAndClearCounters" };

        inline constexpr std::array<loom::PassKey, 38> k_SimulationPassKeys
        {
            k_OwnersPass,
            k_CountVotesPass,
            k_SwapToMajorityPass,
            k_FillGapsPass,
            k_RemoveSubductedPass,
            k_DetectCollisionsPass,
            k_SelectCollisionPairPass,
            k_BkeTileInitAndLocalUnionPass,
            k_BkeStitchSeamsPass,
            k_BkeFlattenPass,
            k_MarkTerraneSeedsPass,
            k_CountTerranePass,
            k_DecideTransferPass,
            k_TransferTerraneDirectPass,
            k_ClearSourceTerranePass,
            k_ComputeSutureCapSpreadPass,
            k_NearestSuturePointPass,
            k_EscapeHistogramPass,
            k_EscapePrefixSumPass,
            k_ApplyEscapeAndSurgePass,
            k_RefreshOwnersAfterCollisionPass,
            k_CountLiveCellsPass,
            k_OffsetLiveCellsPass,
            k_ScatterLiveCellsPass,
            k_CountFrontPointsPass,
            k_OffsetFrontPointsPass,
            k_ScatterFrontPointsPass,
            k_SortFrontPointsPass,
            k_BuildFrontTileCapsPass,
            k_SumPullPass,
            k_FrontDistanceAndUpliftPass,
            k_ApplyMantlePlumesPass,
            k_ErodeReliefPass,
            k_ApplyErosionPass,
            k_DecideRiftPass,
            k_SplitPlatePass,
            k_RefreshOwnersAfterRiftPass,
            k_MovePlatesAndClearCountersPass,
        };

        inline constexpr std::array<loom::PassKey, 43> k_AllPassKeys
        {
            k_InitZeroCountersPass,
            k_OwnersPass,
            k_CountVotesPass,
            k_SwapToMajorityPass,
            k_FillGapsPass,
            k_RemoveSubductedPass,
            k_DetectCollisionsPass,
            k_SelectCollisionPairPass,
            k_BkeTileInitAndLocalUnionPass,
            k_BkeStitchSeamsPass,
            k_BkeFlattenPass,
            k_MarkTerraneSeedsPass,
            k_CountTerranePass,
            k_DecideTransferPass,
            k_TransferTerraneDirectPass,
            k_ClearSourceTerranePass,
            k_ComputeSutureCapSpreadPass,
            k_NearestSuturePointPass,
            k_EscapeHistogramPass,
            k_EscapePrefixSumPass,
            k_ApplyEscapeAndSurgePass,
            k_RefreshOwnersAfterCollisionPass,
            k_CountLiveCellsPass,
            k_OffsetLiveCellsPass,
            k_ScatterLiveCellsPass,
            k_CountFrontPointsPass,
            k_OffsetFrontPointsPass,
            k_ScatterFrontPointsPass,
            k_SortFrontPointsPass,
            k_BuildFrontTileCapsPass,
            k_SumPullPass,
            k_FrontDistanceAndUpliftPass,
            k_ApplyMantlePlumesPass,
            k_ErodeReliefPass,
            k_ApplyErosionPass,
            k_DecideRiftPass,
            k_SplitPlatePass,
            k_RefreshOwnersAfterRiftPass,
            k_MovePlatesAndClearCountersPass,
            k_RenderMapPass,
            k_CaptureMapPass,
            k_PresentMapPass,
            k_UiPass,
        };

        void DeclareBuffer(loom::GraphBuilder& graph,
                           loom::ResourceKey<loom::Buffer> key,
                           loom::ResourceLifetime lifetime,
                           planet::BufferExtent extent,
                           planet::StepSize size,
                           loom::CpuAccess cpu = loom::CpuAccess::None);

        [[nodiscard]] bool CreateAndSupplyStorage(loom::Graph& graph,
                                                  ResourceContext& resources,
                                                  loom::ResourceKey<loom::Buffer> key,
                                                  std::span<const std::byte> data,
                                                  GraphicsResource& out_handle);

        [[nodiscard]] bool ResetStorageBuffer(ResourceContext& resources,
                                              const rhi::TransferWait& wait,
                                              bool has_wait,
                                              GraphicsResource handle,
                                              std::string_view name,
                                              std::span<const std::byte> data);

        template<typename T>
        [[nodiscard]] bool CreateAndSupplyUniform(loom::Graph& graph,
                                                  ResourceContext& resources,
                                                  loom::ResourceKey<loom::Buffer> key,
                                                  BufferUsageBits usage,
                                                  const T& value,
                                                  GraphicsResource& out_handle);

        template<typename T>
        [[nodiscard]] bool UpdateUniformBuffer(ResourceContext& resources,
                                               GraphicsResource handle,
                                               std::string_view name,
                                               const T& value);

        void DestroyHandle(ResourceContext& resources, GraphicsResource& handle);

        void DeclareBuffer(loom::GraphBuilder& graph,
                           loom::ResourceKey<loom::Buffer> key,
                           loom::ResourceLifetime lifetime,
                           planet::BufferExtent extent,
                           planet::StepSize size,
                           loom::CpuAccess cpu)
        {
            const int64_t bytes = static_cast<int64_t>(planet::StepBufferWordCount(extent, size) * sizeof(uint32_t));
            graph.Declare(loom::BufferDeclaration
            {
                .Key = key,
                .Lifetime = lifetime,
                .Size = loom::SizeSource::Bytes(bytes),
                .Cpu = cpu,
                .HistoryDepth = 0,
                .NoHistoryAction = loom::OnNoHistory::Invalid,
            });
        }

        bool CreateAndSupplyStorage(loom::Graph& graph,
                                    ResourceContext& resources,
                                    loom::ResourceKey<loom::Buffer> key,
                                    std::span<const std::byte> data,
                                    GraphicsResource& out_handle)
        {
            const RhiBufferResourceData initial{ data, 0u };
            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = BufferUsageBits::ComputeShaderUAV |
                         BufferUsageBits::TransferSource |
                         BufferUsageBits::TransferDestination;
            info.Size = data.size();
            info.Name = key.Name;

            rhi::ResourceFuture<GraphicsResource> future = resources.CreateBuffer(info, std::span{ &initial, 1u });

            if (resources.Wait(future, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                std::println(stderr, "PlanetGraphImports: failed to create storage buffer {}", key.Name);
                return false;
            }

            out_handle = future.Get();
            graph.Supply(key, std::move(future));
            return true;
        }

        bool ResetStorageBuffer(ResourceContext& resources,
                                const rhi::TransferWait& wait,
                                bool has_wait,
                                GraphicsResource handle,
                                std::string_view name,
                                std::span<const std::byte> data)
        {
            const RhiBufferResourceData piece{ data, 0u };
            const std::span<const rhi::TransferWait> waits =
                has_wait ? std::span{ &wait, 1u } : std::span<const rhi::TransferWait>{};
            rhi::OperationFuture future = resources.SetBufferData(handle, std::span{ &piece, 1u }, waits);

            if (resources.Wait(future, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                std::println(stderr, "PlanetGraphImports: SetBufferData failed on {}", name);
                return false;
            }

            return true;
        }

        template<typename T>
        bool CreateAndSupplyUniform(loom::Graph& graph,
                                    ResourceContext& resources,
                                    loom::ResourceKey<loom::Buffer> key,
                                    BufferUsageBits usage,
                                    const T& value,
                                    GraphicsResource& out_handle)
        {
            const RhiBufferResourceData initial{ std::as_bytes(std::span{ &value, 1u }), 0u };
            BufferCreateInfo info{};
            info.Domain = ResourceDomain::CPUToGPU;
            info.Usage = usage;
            info.Size = sizeof(T);
            info.Name = key.Name;

            rhi::ResourceFuture<GraphicsResource> future = resources.CreateBuffer(info, std::span{ &initial, 1u });

            if (resources.Wait(future, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                std::println(stderr, "PlanetGraphImports: failed to create uniform buffer {}", key.Name);
                return false;
            }

            out_handle = future.Get();
            graph.Supply(key, std::move(future));
            return true;
        }

        template<typename T>
        bool UpdateUniformBuffer(ResourceContext& resources,
                                 GraphicsResource handle,
                                 std::string_view name,
                                 const T& value)
        {
            rhi::ResourceFuture<void*> mapped = resources.MapBuffer(handle, sizeof(T), 0u);

            if (resources.Wait(mapped, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                std::println(stderr, "PlanetGraphImports: MapBuffer failed on {}", name);
                return false;
            }

            std::memcpy(mapped.Get(), &value, sizeof(T));
            rhi::OperationFuture unmapped = resources.UnmapBuffer(handle, sizeof(T), 0u);

            if (resources.Wait(unmapped, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                std::println(stderr, "PlanetGraphImports: UnmapBuffer failed on {}", name);
                return false;
            }

            return true;
        }

        void DestroyHandle(ResourceContext& resources, GraphicsResource& handle)
        {
            if (!handle)
            {
                return;
            }

            rhi::OperationFuture destroyed = resources.DestroyResource(handle);
            [[maybe_unused]] const rhi::FutureState state = resources.Wait(destroyed, k_TimeoutNs);
            handle = GraphicsResource::Null();
        }
    }

    const std::array<loom::PassKey, 43>& AllPlanetPassKeys() noexcept
    {
        return k_AllPassKeys;
    }

    void ZeroCountersRecorder::SetIncludeLiveDisplayBuffers(bool include) noexcept
    {
        includeLiveDisplayBuffers = include;
    }

    void ZeroCountersRecorder::Record(loom::RecordContext& ctx)
    {
        rhi::CommandBuffer& cmd = ctx.CommandBuffer();

        for (const loom::ResourceKey<loom::Buffer>& key : k_PersistentCounters)
        {
            cmd.FillBuffer(rhi::BufferHandle{ ctx.BufferOf(key).VkHandle }, 0u, ~0ull, 0u);
        }

        if (includeLiveDisplayBuffers)
        {
            cmd.FillBuffer(rhi::BufferHandle{ ctx.BufferOf(k_FrontData).VkHandle }, 0u, ~0ull, 0u);
            cmd.FillBuffer(rhi::BufferHandle{ ctx.BufferOf(k_CellFronts).VkHandle }, 0u, ~0ull, 0u);
        }
    }

    void TriggerSimulationStep(loom::Graph& graph)
    {
        for (const loom::PassKey& pass : k_SimulationPassKeys)
        {
            graph.Trigger(pass);
        }
    }

    void TriggerInitialLiveView(loom::Graph& graph)
    {
        graph.Trigger(k_InitZeroCountersPass);
        graph.Trigger(k_OwnersPass);
    }

    void RegisterPlanetGraph(loom::GraphBuilder& graph,
                             const PlanetGraphConfig& config,
                             const PlanetGraphCallbacks& callbacks)
    {
        const uint32_t pointCount = static_cast<uint32_t>(planet::HealpixPixelCount(config.HealpixOrder));
        const planet::StepSize size{ pointCount, config.PlateCount };
        const int32_t points = static_cast<int32_t>(pointCount);
        const int32_t plates = static_cast<int32_t>(config.PlateCount);
        const int32_t liveThreads = static_cast<int32_t>(planet::LiveGroupCount(size) * planet::k_LiveGroupSize);

        graph.Pin("CRUST_FLAG_BYTES", loom::AxisValue::Boolean(config.UseByteCrustFlags));

        // Imported uniform buffers
        graph.Import(loom::BufferImportDeclaration{ .Key = k_StepParams, .ByteSize = static_cast<int64_t>(sizeof(StepParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Pass3Constants, .ByteSize = static_cast<int64_t>(sizeof(Pass3ParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_UpliftConstants, .ByteSize = static_cast<int64_t>(sizeof(UpliftParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_ShapingConstants, .ByteSize = static_cast<int64_t>(sizeof(ShapingParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_MovePlatesConstants, .ByteSize = static_cast<int64_t>(sizeof(MovePlatesParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PlumeConstants, .ByteSize = static_cast<int64_t>(sizeof(PlumeParamsHost)) });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_RenderMapConstants, .ByteSize = static_cast<int64_t>(sizeof(RenderMapParamsHost)) });

        if (config.LiveMode)
        {
            graph.Import(loom::BufferImportDeclaration{ .Key = k_PresentConstants, .ByteSize = static_cast<int64_t>(sizeof(PresentMapParamsHost)) });
        }

        // 8 imported persistent state buffers
        const int64_t flagBytes = static_cast<int64_t>(pointCount) * static_cast<int64_t>(config.PlateCount) *
                                  (config.UseByteCrustFlags ? 1 : 4);
        const int64_t stateBytes = static_cast<int64_t>(pointCount) * static_cast<int64_t>(config.PlateCount) * 16;
        const int64_t rotBytes = static_cast<int64_t>(config.PlateCount) * 3 * 16;
        const int64_t plateVec4Bytes = static_cast<int64_t>(config.PlateCount) * 16;
        const int64_t plateWordBytes = static_cast<int64_t>(config.PlateCount) * 4;
        const int64_t pointIntBytes = static_cast<int64_t>(pointCount) * 4;

        graph.Import(loom::BufferImportDeclaration{ .Key = k_CrustFlags, .ByteSize = flagBytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_CrustState, .ByteSize = stateBytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_Rotations, .ByteSize = rotBytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PoleRates, .ByteSize = plateVec4Bytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PlateRift, .ByteSize = plateVec4Bytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PlateCapCenters, .ByteSize = plateVec4Bytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PlateCapSpread, .ByteSize = plateWordBytes });
        graph.Import(loom::BufferImportDeclaration{ .Key = k_PreviousPlate, .ByteSize = pointIntBytes });

        // 11 declared persistent counter buffers
        DeclareBuffer(graph, k_PairVotes, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesSquared, size);
        DeclareBuffer(graph, k_LowVotes, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesSquared, size);
        DeclareBuffer(graph, k_CollisionPairCount, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesSquared, size);
        DeclareBuffer(graph, k_CollisionPairSpeedSum, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesSquared, size);
        DeclareBuffer(graph, k_CollisionPairDeepCount, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesSquared, size);
        DeclareBuffer(graph, k_PlateLiveCursor, loom::ResourceLifetime::Persistent, planet::BufferExtent::Plates, size);
        DeclareBuffer(graph, k_PlateFrontPointCount, loom::ResourceLifetime::Persistent, planet::BufferExtent::Plates, size);
        DeclareBuffer(graph, k_PlateFrontPointCursor, loom::ResourceLifetime::Persistent, planet::BufferExtent::Plates, size);
        DeclareBuffer(graph, k_PlateContinentalCount, loom::ResourceLifetime::Persistent, planet::BufferExtent::Plates, size);
        DeclareBuffer(graph, k_PlateCounters, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesTimes4, size);
        DeclareBuffer(graph, k_PlatePull, loom::ResourceLifetime::Persistent, planet::BufferExtent::PlatesTimes3, size);
        DeclareBuffer(graph, k_PlateIncubation, loom::ResourceLifetime::Persistent, planet::BufferExtent::Plates, size);

        const bool interactive = config.LiveMode || config.InteractiveControl;

        // When interactive, RenderMapCS reads Owners/FrontData/CellFronts on paused frames so keep them persistent.
        const loom::ResourceLifetime displayLifetime =
            interactive ? loom::ResourceLifetime::Persistent : loom::ResourceLifetime::Frame;

        // 21 declared scratch / live-persistent buffers
        DeclareBuffer(graph, k_Owners, displayLifetime, planet::BufferExtent::PointsTimes4, size, loom::CpuAccess::Readback);
        DeclareBuffer(graph, k_FrontData, displayLifetime, planet::BufferExtent::PointsTimes4, size);
        DeclareBuffer(graph, k_CollisionJob, loom::ResourceLifetime::Frame, planet::BufferExtent::CollideJob, size, loom::CpuAccess::Readback);
        DeclareBuffer(graph, k_CollideDispatch, loom::ResourceLifetime::Frame, planet::BufferExtent::CollideDispatch, size);
        DeclareBuffer(graph, k_CollideScratchCounters, loom::ResourceLifetime::Frame, planet::BufferExtent::CollideCounters, size);
        DeclareBuffer(graph, k_SuturePositionSum, loom::ResourceLifetime::Frame, planet::BufferExtent::SignedVec3, size);
        DeclareBuffer(graph, k_EscapeHistogram, loom::ResourceLifetime::Frame, planet::BufferExtent::EscapeHistogram, size);
        DeclareBuffer(graph, k_BkeParent, loom::ResourceLifetime::Frame, planet::BufferExtent::HalfPoints, size);
        DeclareBuffer(graph, k_BkeMask, loom::ResourceLifetime::Frame, planet::BufferExtent::HalfPoints, size);
        DeclareBuffer(graph, k_BkeSeedFlag, loom::ResourceLifetime::Frame, planet::BufferExtent::HalfPoints, size);
        DeclareBuffer(graph, k_SuturePoints, loom::ResourceLifetime::Frame, planet::BufferExtent::PointsTimes4, size);
        DeclareBuffer(graph, k_SutureDistance, loom::ResourceLifetime::Frame, planet::BufferExtent::Points, size);
        DeclareBuffer(graph, k_LiveGroupCounts, loom::ResourceLifetime::Frame, planet::BufferExtent::LiveGroups, size);
        DeclareBuffer(graph, k_PlateLiveOffset, loom::ResourceLifetime::Frame, planet::BufferExtent::PlatesPlusOne, size);
        DeclareBuffer(graph, k_LiveDispatch, loom::ResourceLifetime::Frame, planet::BufferExtent::IndirectDispatch, size);
        DeclareBuffer(graph, k_LiveCell, loom::ResourceLifetime::Frame, planet::BufferExtent::Cells, size);
        DeclareBuffer(graph, k_PlateFrontPointOffset, loom::ResourceLifetime::Frame, planet::BufferExtent::PlatesPlusOne, size, loom::CpuAccess::Readback);
        DeclareBuffer(graph, k_FrontTileDispatch, loom::ResourceLifetime::Frame, planet::BufferExtent::IndirectDispatch, size);
        DeclareBuffer(graph, k_FrontPoints, loom::ResourceLifetime::Frame, planet::BufferExtent::PointsTimes4, size);
        DeclareBuffer(graph, k_FrontTileCaps, loom::ResourceLifetime::Frame, planet::BufferExtent::FrontTilesTimes4, size);
        DeclareBuffer(graph, k_CellFronts, displayLifetime, planet::BufferExtent::CellsTimes2, size);
        DeclareBuffer(graph, k_RiftJob, loom::ResourceLifetime::Frame, planet::BufferExtent::CollideJob, size);
        DeclareBuffer(graph, k_ErodedRelief, loom::ResourceLifetime::Frame, planet::BufferExtent::Cells, size);
        DeclareBuffer(graph, k_RiftDispatch, loom::ResourceLifetime::Frame, planet::BufferExtent::IndirectDispatch, size);

        // RenderMap output image
        graph.Declare(loom::ImageDeclaration
        {
            .Key = k_MapOutput,
            .Lifetime = loom::ResourceLifetime::Persistent,
            .Format = lodestone::TextureFormat::RGBA8Unorm,
            .Extent = loom::ExtentSource::Fixed(static_cast<int32_t>(config.MapWidth),
                                                static_cast<int32_t>(config.MapHeight),
                                                1),
            .Mips = loom::MipSource::One(),
            .Layers = loom::LayerSource::Fixed(1),
            .Cpu = loom::CpuAccess::Readback,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid,
        });

        if (config.LiveMode)
        {
            graph.Import(k_Screen);
            const loom::OutputFallback fallback = loom::OutputFallback::Clear(0.02f, 0.03f, 0.05f, 1.0f);
            graph.Output(loom::OutputDeclaration{ .Target = k_Screen, .OnSkip = fallback });

            SamplerCreateInfo mapSampler{};
            mapSampler.MagFilter = ImageFilteringMode::Linear;
            mapSampler.MinFilter = ImageFilteringMode::Linear;
            mapSampler.MipFilter = ImageFilteringMode::Nearest;
            mapSampler.AddressU = SamplerCreateInfo::AddressMode::Repeat;
            mapSampler.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
            mapSampler.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
            mapSampler.Name = "Planet.MapSampler";
            graph.Declare(loom::SamplerDeclaration{ .Key = k_MapSampler, .CreateInfo = mapSampler });
        }

        constexpr loom::QueueAffinity k_Queue = loom::QueueAffinity::Graphics;
        const loom::RunPolicy initPolicy = interactive ? loom::RunPolicy::OnTrigger : loom::RunPolicy::Once;
        const loom::RunPolicy simPolicy = interactive ? loom::RunPolicy::OnTrigger : loom::RunPolicy::EveryFrame;
        constexpr std::string_view k_Module = "PlanetLab";

        if (callbacks.ZeroCounters != nullptr)
        {
            callbacks.ZeroCounters->SetIncludeLiveDisplayBuffers(interactive);
        }

        const auto buildSequence = [&](loom::SequenceBuilder& seq)
        {
            // 0. Zero persistent counters (plus interactive display buffers)
            const auto initPass = [interactive, &callbacks](loom::RecordBuilder& pass)
            {
                for (const loom::ResourceKey<loom::Buffer>& counter : k_PersistentCounters)
                {
                    pass.Uses(counter, loom::ResourceAccess::Write, loom::BufferUse::TransferDestination);
                }

                if (interactive)
                {
                    pass.Uses(k_FrontData, loom::ResourceAccess::Write, loom::BufferUse::TransferDestination);
                    pass.Uses(k_CellFronts, loom::ResourceAccess::Write, loom::BufferUse::TransferDestination);
                }

                if (callbacks.ZeroCounters != nullptr)
                {
                    pass.Record(Delegate<void(loom::RecordContext&)>::Create<ZeroCountersRecorder, &ZeroCountersRecorder::Record>(callbacks.ZeroCounters));
                }
            };
            seq.AddRecord(k_InitZeroCountersPass, initPolicy, k_Queue, initPass);

            // 1. OwnersCS
            seq.AddDispatch(k_OwnersPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::OwnersCS;
                pass.Shader(k_Module, "OwnersCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CrustStateIn, k_CrustState);
                pass.Bind(ep::Owners, k_Owners, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateCapSpreadIn, k_PlateCapSpread);
                pass.Bind(ep::PreviousPlateIn, k_PreviousPlate);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchThreads(points, 1, 1);
            });

            // 2. CountVotesCS
            seq.AddDispatch(k_CountVotesPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::CountVotesCS;
                pass.Shader(k_Module, "CountVotesCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::LowVotes, k_LowVotes);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PairVotes, k_PairVotes);
                pass.DispatchThreads(points, 1, 1);
            });

            // 3. SwapToMajorityCS
            seq.AddDispatch(k_SwapToMajorityPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::SwapToMajorityCS;
                pass.Shader(k_Module, "SwapToMajorityCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::LowVotesIn, k_LowVotes);
                pass.Bind(ep::Owners, k_Owners);
                pass.Bind(ep::PairVotesIn, k_PairVotes);
                pass.DispatchThreads(points, 1, 1);
            });

            // 4. FillGapsCS
            seq.AddDispatch(k_FillGapsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::FillGapsCS;
                pass.Shader(k_Module, "FillGapsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::Pass3Constants, k_Pass3Constants);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::Owners, k_Owners);
                pass.Bind(ep::PreviousPlate, k_PreviousPlate);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchThreads(points, 1, 1);
            });

            // 5. RemoveSubductedCS
            seq.AddDispatch(k_RemoveSubductedPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::RemoveSubductedCS;
                pass.Shader(k_Module, "RemoveSubductedCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::Pass3Constants, k_Pass3Constants);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CrustStateIn, k_CrustState);
                pass.Bind(ep::FrontData, k_FrontData, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PoleRatesIn, k_PoleRates);
                pass.DispatchThreads(points, 1, 1);
            });

            // 6. DetectCollisionsCS
            seq.AddDispatch(k_DetectCollisionsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::DetectCollisionsCS;
                pass.Shader(k_Module, "DetectCollisionsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CollisionPairCount, k_CollisionPairCount);
                pass.Bind(ep::CollisionPairDeepCount, k_CollisionPairDeepCount);
                pass.Bind(ep::CollisionPairSpeedSum, k_CollisionPairSpeedSum);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PoleRatesIn, k_PoleRates);
                pass.DispatchThreads(points, 1, 1);
            });

            // 7. SelectCollisionPairCS
            seq.AddDispatch(k_SelectCollisionPairPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::SelectCollisionPairCS;
                pass.Shader(k_Module, "SelectCollisionPairCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::CollideDispatch, k_CollideDispatch, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::CollisionJob, k_CollisionJob, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::CollisionPairCountIn, k_CollisionPairCount);
                pass.Bind(ep::CollisionPairDeepCountIn, k_CollisionPairDeepCount);
                pass.Bind(ep::CollisionPairSpeedSumIn, k_CollisionPairSpeedSum);
                pass.Bind(ep::EscapeHistogram, k_EscapeHistogram, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::SuturePositionSum, k_SuturePositionSum, loom::AccessModifier::WriteOnly);
                pass.DispatchThreads(1, 1, 1);
            });

            // 8. BkeTileInitAndLocalUnionCS
            seq.AddDispatch(k_BkeTileInitAndLocalUnionPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::BkeTileInitAndLocalUnionCS;
                pass.Shader(k_Module, "BkeTileInitAndLocalUnionCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsPackedIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::BkeParent, k_BkeParent, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::BkeSeedFlag, k_BkeSeedFlag, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.DispatchIndirect(k_CollideDispatch, 0);
            });

            // 9. BkeStitchSeamsCS
            seq.AddDispatch(k_BkeStitchSeamsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::BkeStitchSeamsCS;
                pass.Shader(k_Module, "BkeStitchSeamsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeParent, k_BkeParent);
                pass.DispatchIndirect(k_CollideDispatch, 0);
            });

            // 10. BkeFlattenCS
            seq.AddDispatch(k_BkeFlattenPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::BkeFlattenCS;
                pass.Shader(k_Module, "BkeFlattenCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeParent, k_BkeParent);
                pass.DispatchIndirect(k_CollideDispatch, 12);
            });

            // 11. MarkTerraneSeedsCS
            seq.AddDispatch(k_MarkTerraneSeedsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::MarkTerraneSeedsCS;
                pass.Shader(k_Module, "MarkTerraneSeedsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::BkeParent, k_BkeParent, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeSeedFlag, k_BkeSeedFlag);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 12. CountTerraneCS
            seq.AddDispatch(k_CountTerranePass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::CountTerraneCS;
                pass.Shader(k_Module, "CountTerraneCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeParent, k_BkeParent, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeSeedFlag, k_BkeSeedFlag, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters);
                pass.DispatchIndirect(k_CollideDispatch, 12);
            });

            // 13. DecideTransferCS
            seq.AddDispatch(k_DecideTransferPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::DecideTransferCS;
                pass.Shader(k_Module, "DecideTransferCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJob, k_CollisionJob);
                pass.DispatchIndirect(k_CollideDispatch, 36);
            });

            // 14. TransferTerraneDirectCS
            seq.AddDispatch(k_TransferTerraneDirectPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::TransferTerraneDirectCS;
                pass.Shader(k_Module, "TransferTerraneDirectCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeParent, k_BkeParent, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeSeedFlag, k_BkeSeedFlag, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.Bind(ep::SuturePoints, k_SuturePoints, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::SuturePositionSum, k_SuturePositionSum);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 15. ClearSourceTerraneCS
            seq.AddDispatch(k_ClearSourceTerranePass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ClearSourceTerraneCS;
                pass.Shader(k_Module, "ClearSourceTerraneCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::BkeMask, k_BkeMask, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeParent, k_BkeParent, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::BkeSeedFlag, k_BkeSeedFlag, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJob, k_CollisionJob);
                pass.Bind(ep::SuturePositionSum, k_SuturePositionSum, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 16. ComputeSutureCapSpreadCS
            seq.AddDispatch(k_ComputeSutureCapSpreadPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ComputeSutureCapSpreadCS;
                pass.Shader(k_Module, "ComputeSutureCapSpreadCS");
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJob, k_CollisionJob);
                pass.Bind(ep::SuturePoints, k_SuturePoints, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 36);
            });

            // 17. NearestSuturePointCS
            seq.AddDispatch(k_NearestSuturePointPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::NearestSuturePointCS;
                pass.Shader(k_Module, "NearestSuturePointCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::SutureDistance, k_SutureDistance, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::SuturePoints, k_SuturePoints, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 18. EscapeHistogramCS
            seq.AddDispatch(k_EscapeHistogramPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::EscapeHistogramCS;
                pass.Shader(k_Module, "EscapeHistogramCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::EscapeHistogram, k_EscapeHistogram);
                pass.Bind(ep::SutureDistance, k_SutureDistance, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 19. EscapePrefixSumCS
            seq.AddDispatch(k_EscapePrefixSumPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::EscapePrefixSumCS;
                pass.Shader(k_Module, "EscapePrefixSumCS");
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJob, k_CollisionJob);
                pass.Bind(ep::EscapeHistogram, k_EscapeHistogram, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 36);
            });

            // 20. ApplyEscapeAndSurgeCS
            seq.AddDispatch(k_ApplyEscapeAndSurgePass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ApplyEscapeAndSurgeCS;
                pass.Shader(k_Module, "ApplyEscapeAndSurgeCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::Pass3Constants, k_Pass3Constants);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CollideScratchCounters, k_CollideScratchCounters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::SutureDistance, k_SutureDistance, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 21. RefreshOwnersAfterCollisionCS
            seq.AddDispatch(k_RefreshOwnersAfterCollisionPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::RefreshOwnersAfterCollisionCS;
                pass.Shader(k_Module, "RefreshOwnersAfterCollisionCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CollisionJobIn, k_CollisionJob);
                pass.Bind(ep::Owners, k_Owners);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters);
                pass.Bind(ep::PreviousPlate, k_PreviousPlate);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchIndirect(k_CollideDispatch, 24);
            });

            // 22. CountLiveCellsCS
            seq.AddDispatch(k_CountLiveCellsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::CountLiveCellsCS;
                pass.Shader(k_Module, "CountLiveCellsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsPackedIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::LiveGroupCounts, k_LiveGroupCounts, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateCapSpreadIn, k_PlateCapSpread);
                pass.Bind(ep::PlateContinentalCount, k_PlateContinentalCount);
                pass.Bind(ep::PlateCounters, k_PlateCounters);
                pass.DispatchThreads(liveThreads, 1, 1);
            });

            // 23. OffsetLiveCellsCS
            seq.AddDispatch(k_OffsetLiveCellsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::OffsetLiveCellsCS;
                pass.Shader(k_Module, "OffsetLiveCellsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::LiveDispatch, k_LiveDispatch, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters);
                pass.Bind(ep::PlateCapSpread, k_PlateCapSpread);
                pass.Bind(ep::PlateCountersIn, k_PlateCounters);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::WriteOnly);
                pass.DispatchThreads(1, 1, 1);
            });

            // 24. ScatterLiveCellsCS
            seq.AddDispatch(k_ScatterLiveCellsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ScatterLiveCellsCS;
                pass.Shader(k_Module, "ScatterLiveCellsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsPackedIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::LiveGroupCounts, k_LiveGroupCounts, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateCapSpread, k_PlateCapSpread);
                pass.Bind(ep::PlateLiveCursor, k_PlateLiveCursor);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchThreads(liveThreads, 1, 1);
            });

            // 25. CountFrontPointsCS (after RefreshOwnersAfterCollisionCS so PlateFrontPointCount matches post-collision Owners)
            seq.AddDispatch(k_CountFrontPointsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::CountFrontPointsCS;
                pass.Shader(k_Module, "CountFrontPointsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontDataIn, k_FrontData);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PlateFrontPointCount, k_PlateFrontPointCount);
                pass.DispatchThreads(points, 1, 1);
            });

            // 26. OffsetFrontPointsCS
            seq.AddDispatch(k_OffsetFrontPointsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::OffsetFrontPointsCS;
                pass.Shader(k_Module, "OffsetFrontPointsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontTileDispatch, k_FrontTileDispatch, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::PlateFrontPointCountIn, k_PlateFrontPointCount);
                pass.Bind(ep::PlateFrontPointOffset, k_PlateFrontPointOffset, loom::AccessModifier::WriteOnly);
                pass.DispatchThreads(1, 1, 1);
            });

            // 27. ScatterFrontPointsCS
            seq.AddDispatch(k_ScatterFrontPointsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ScatterFrontPointsCS;
                pass.Shader(k_Module, "ScatterFrontPointsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontDataIn, k_FrontData);
                pass.Bind(ep::FrontPoints, k_FrontPoints, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PlateFrontPointCursor, k_PlateFrontPointCursor);
                pass.Bind(ep::PlateFrontPointOffset, k_PlateFrontPointOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchThreads(points, 1, 1);
            });

            // 28. SortFrontPointsCS
            seq.AddDispatch(k_SortFrontPointsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::SortFrontPointsCS;
                pass.Shader(k_Module, "SortFrontPointsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontPoints, k_FrontPoints);
                pass.Bind(ep::PlateFrontPointOffset, k_PlateFrontPointOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchThreads(plates * static_cast<int32_t>(planet::k_SortGroupSize), 1, 1);
            });

            // 29. BuildFrontTileCapsCS
            seq.AddDispatch(k_BuildFrontTileCapsPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::BuildFrontTileCapsCS;
                pass.Shader(k_Module, "BuildFrontTileCapsCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontPoints, k_FrontPoints, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::FrontTileCaps, k_FrontTileCaps, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::PlateFrontPointOffset, k_PlateFrontPointOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_FrontTileDispatch, 0);
            });

            // 30. SumPullCS
            seq.AddDispatch(k_SumPullPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::SumPullCS;
                pass.Shader(k_Module, "SumPullCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::FrontDataIn, k_FrontData);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.Bind(ep::PlateCountersIn, k_PlateCounters);
                pass.Bind(ep::PlatePull, k_PlatePull);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchThreads(points, 1, 1);
            });

            // 31. FrontDistanceAndUpliftCS
            seq.AddDispatch(k_FrontDistanceAndUpliftPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::FrontDistanceAndUpliftCS;
                pass.Shader(k_Module, "FrontDistanceAndUpliftCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::UpliftConstants, k_UpliftConstants);
                pass.Bind(ep::ShapingConstants, k_ShapingConstants);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CellFronts, k_CellFronts, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::FrontDataIn, k_FrontData);
                pass.Bind(ep::FrontPoints, k_FrontPoints, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::FrontTileCaps, k_FrontTileCaps, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateFrontPointOffset, k_PlateFrontPointOffset, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchIndirect(k_LiveDispatch, 0);
            });

            // 32. ApplyMantlePlumesCS (after FrontDistanceAndUpliftCS, before MovePlatesAndClearCountersCS)
            seq.AddDispatch(k_ApplyMantlePlumesPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ApplyMantlePlumesCS;
                pass.Shader(k_Module, "ApplyMantlePlumesCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::PlumeConstants, k_PlumeConstants);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::Rotations, k_Rotations);
                pass.DispatchIndirect(k_LiveDispatch, 0);
            });

            // 33. ErodeReliefCS: diffusion of land height and orogenic collapse, into ErodedRelief (Erosion.slang)
            seq.AddDispatch(k_ErodeReliefPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ErodeReliefCS;
                pass.Shader(k_Module, "ErodeReliefCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::UpliftConstants, k_UpliftConstants);
                pass.Bind(ep::ShapingConstants, k_ShapingConstants);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CrustStateIn, k_CrustState);
                pass.Bind(ep::ErodedRelief, k_ErodedRelief, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_LiveDispatch, 0);
            });

            // 34. ApplyErosionCS: copies ErodedRelief into CrustState
            seq.AddDispatch(k_ApplyErosionPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::ApplyErosionCS;
                pass.Shader(k_Module, "ApplyErosionCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::ErodedReliefIn, k_ErodedRelief);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.DispatchIndirect(k_LiveDispatch, 0);
            });

            // 35. DecideRiftCS: one wave picks at most one plate to split, and a free slot for its new half
            seq.AddDispatch(k_DecideRiftPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::DecideRiftCS;
                pass.Shader(k_Module, "DecideRiftCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::MovePlatesConstants, k_MovePlatesConstants);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::LiveCell, k_LiveCell, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateCapCenters, k_PlateCapCenters);
                pass.Bind(ep::PlateContinentalCount, k_PlateContinentalCount);
                pass.Bind(ep::PlateCountersIn, k_PlateCounters);
                pass.Bind(ep::PlateIncubation, k_PlateIncubation);
                pass.Bind(ep::PlateLiveOffset, k_PlateLiveOffset, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::PlateRiftRW, k_PlateRift);
                pass.Bind(ep::PoleRatesRW, k_PoleRates);
                pass.Bind(ep::RiftDispatch, k_RiftDispatch, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::RiftJob, k_RiftJob, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::RotationsRW, k_Rotations);
                pass.DispatchThreads(1, 1, 1);
            });

            // 36. SplitPlateCS: moves one side of the cut line to the new plate (zero groups when no plate rifts)
            seq.AddDispatch(k_SplitPlatePass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::SplitPlateCS;
                pass.Shader(k_Module, "SplitPlateCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlags, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWords, k_CrustFlags);
                pass.Bind(ep::CrustState, k_CrustState);
                pass.Bind(ep::RiftJobIn, k_RiftJob);
                pass.DispatchIndirect(k_RiftDispatch, 0);
            });

            // 37. RefreshOwnersAfterRiftCS: points that showed a moved cell show the new plate
            seq.AddDispatch(k_RefreshOwnersAfterRiftPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::RefreshOwnersAfterRiftCS;
                pass.Shader(k_Module, "RefreshOwnersAfterRiftCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::Owners, k_Owners);
                pass.Bind(ep::PreviousPlate, k_PreviousPlate);
                pass.Bind(ep::RiftJobIn, k_RiftJob);
                pass.DispatchIndirect(k_RiftDispatch, 0);
            });

            // 38. MovePlatesAndClearCountersCS
            seq.AddDispatch(k_MovePlatesAndClearCountersPass, simPolicy, k_Queue, [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::MovePlatesAndClearCountersCS;
                pass.Shader(k_Module, "MovePlatesAndClearCountersCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::MovePlatesConstants, k_MovePlatesConstants);
                pass.Bind(ep::CollisionPairCount, k_CollisionPairCount);
                pass.Bind(ep::CollisionPairDeepCount, k_CollisionPairDeepCount);
                pass.Bind(ep::CollisionPairSpeedSum, k_CollisionPairSpeedSum);
                pass.Bind(ep::LowVotes, k_LowVotes);
                pass.Bind(ep::PairVotes, k_PairVotes);
                pass.Bind(ep::PlateContinentalCount, k_PlateContinentalCount);
                pass.Bind(ep::PlateCounters, k_PlateCounters);
                pass.Bind(ep::PlateFrontPointCount, k_PlateFrontPointCount);
                pass.Bind(ep::PlateFrontPointCursor, k_PlateFrontPointCursor);
                pass.Bind(ep::PlateLiveCursor, k_PlateLiveCursor);
                pass.Bind(ep::PlatePull, k_PlatePull);
                pass.Bind(ep::PlateRiftIn, k_PlateRift);
                pass.Bind(ep::PoleRatesRW, k_PoleRates);
                pass.Bind(ep::RotationsRW, k_Rotations);
                pass.DispatchThreads(1, 1, 1);
            });

            // 34. RenderMapCS (always EveryFrame when interactive so debug layers update while paused)
            const loom::RunPolicy renderPolicy = (!interactive && config.RenderOnTrigger)
                ? loom::RunPolicy::OnTrigger
                : loom::RunPolicy::EveryFrame;

            const auto renderMap = [&](loom::DispatchBuilder& pass)
            {
                namespace ep = pl::RenderMapCS;
                pass.Shader(k_Module, "RenderMapCS");
                pass.Bind(ep::StepParams, k_StepParams);
                pass.Bind(ep::RenderMapConstants, k_RenderMapConstants);
                pass.Bind(ep::PlumeConstants, k_PlumeConstants);
                pass.Bind(ep::ShapingConstants, k_ShapingConstants);
                pass.Bind(ep::SomeVariants::CrustFlagsIn, k_CrustFlags);
                pass.Bind(ep::SomeVariants::CrustFlagsWordsIn, k_CrustFlags);
                pass.Bind(ep::CellFronts, k_CellFronts, loom::AccessModifier::ReadOnly);
                pass.Bind(ep::CrustStateIn, k_CrustState);
                pass.Bind(ep::FrontDataIn, k_FrontData);
                pass.Bind(ep::MapOutput, k_MapOutput, loom::AccessModifier::WriteOnly);
                pass.Bind(ep::OwnersIn, k_Owners);
                pass.DispatchThreadsOver(k_MapOutput);
            };
            seq.AddDispatch(k_RenderMapPass, renderPolicy, k_Queue, renderMap);

            // 35. Optional CaptureMapPass (OnTrigger PNG / region capture)
            if (config.EnableCapturePass && callbacks.CaptureCallback)
            {
                const auto captureMap = [&callbacks](loom::RecordBuilder& pass)
                {
                    pass.Uses(k_MapOutput, loom::ResourceAccess::Read, loom::ImageUse::TransferSource);
                    pass.Record(callbacks.CaptureCallback);
                };
                seq.AddRecord(k_CaptureMapPass, loom::RunPolicy::OnTrigger, k_Queue, captureMap);
            }

            // 36 & 37. Swapchain presentation + ImGui overlay in LiveMode
            if (config.LiveMode)
            {
                const auto presentMap = [](loom::DrawBuilder& pass)
                {
                    pass.Shaders("PlanetPresent", "PresentMapVS", "PresentMapPS");
                    pass.Bind("PresentConstants", k_PresentConstants);
                    pass.Bind("MapInput", k_MapOutput);
                    pass.Bind("MapSampler", k_MapSampler);
                    pass.Color(0, k_Screen, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
                    pass.Draw(3, 1, 0, 0);
                };
                seq.AddDraw(k_PresentMapPass, loom::RunPolicy::EveryFrame, k_Queue, presentMap);

                if (callbacks.UiCallback)
                {
                    const auto uiPass = [&callbacks](loom::RecordBuilder& pass)
                    {
                        pass.Uses(k_Screen, loom::ResourceAccess::ReadWrite, loom::ImageUse::ColorTarget);
                        pass.Record(callbacks.UiCallback);
                    };
                    seq.AddRecord(k_UiPass, loom::RunPolicy::EveryFrame, k_Queue, uiPass);
                }
            }
        };

        graph.AddSequence(k_StepSequence, buildSequence);
    }

    bool PlanetGraphImports::SupplyAll(loom::Graph& graph,
                                       ResourceContext& resources,
                                       const planet::HealpixInitialState& initial,
                                       float dt,
                                       uint32_t order,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t mode,
                                       bool use_bytes,
                                       const PlumeSystemState& plumes,
                                       const ShapingState& shaping)
    {
        const size_t expectedFlagBytes = static_cast<size_t>(initial.PointCount) *
                                         static_cast<size_t>(initial.PlateCount) *
                                         (use_bytes ? 1u : 4u);

        if (initial.CrustFlags.size() != expectedFlagBytes)
        {
            std::println(stderr, "PlanetGraphImports: CrustFlags size {} does not match expected {}",
                         initial.CrustFlags.size(), expectedFlagBytes);
            return false;
        }

        const StepParamsHost stepParams
        {
            .PointCount = initial.PointCount,
            .PlateCount = initial.PlateCount,
            .TimeStepMy = dt,
            .HealpixOrder = order,
        };
        const Pass3ParamsHost pass3Params{ .Time = initial.InitialTime };
        const UpliftParamsHost upliftParams{ .Time = initial.InitialTime };
        const ShapingParamsHost shapingParams = PackShapingUniform(shaping);
        const MovePlatesParamsHost movePlatesParams{ .Time = initial.InitialTime, .RiftEnabled = RiftEnabled ? 1u : 0u };
        const PlumeParamsHost plumeParams = PackPlumeUniform(plumes, initial.InitialTime);
        const RenderMapParamsHost renderMapParams
        {
            .Width = width,
            .Height = height,
            .Time = initial.InitialTime,
            .Mode = mode,
        };

        const std::vector<math::Float4> zeroRift(initial.PlateCount, math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f });

        return CreateAndSupplyUniform(graph, resources, k_StepParams, BufferUsageBits::ComputeShaderUBO, stepParams, StepParams) &&
               CreateAndSupplyUniform(graph, resources, k_Pass3Constants, BufferUsageBits::ComputeShaderUBO, pass3Params, Pass3Constants) &&
               CreateAndSupplyUniform(graph, resources, k_UpliftConstants, BufferUsageBits::ComputeShaderUBO, upliftParams, UpliftConstants) &&
               CreateAndSupplyUniform(graph, resources, k_ShapingConstants, BufferUsageBits::ComputeShaderUBO, shapingParams, ShapingConstants) &&
               CreateAndSupplyUniform(graph, resources, k_MovePlatesConstants, BufferUsageBits::ComputeShaderUBO, movePlatesParams, MovePlatesConstants) &&
               CreateAndSupplyUniform(graph, resources, k_PlumeConstants, BufferUsageBits::ComputeShaderUBO, plumeParams, PlumeConstants) &&
               CreateAndSupplyUniform(graph, resources, k_RenderMapConstants, BufferUsageBits::ComputeShaderUBO, renderMapParams, RenderMapConstants) &&
               CreateAndSupplyStorage(graph, resources, k_CrustFlags, initial.CrustFlags, CrustFlags) &&
               CreateAndSupplyStorage(graph, resources, k_CrustState, std::as_bytes(std::span{ initial.CrustState }), CrustState) &&
               CreateAndSupplyStorage(graph, resources, k_Rotations, std::as_bytes(std::span{ initial.Rotations }), Rotations) &&
               CreateAndSupplyStorage(graph, resources, k_PoleRates, std::as_bytes(std::span{ initial.PoleRates }), PoleRates) &&
               CreateAndSupplyStorage(graph, resources, k_PlateRift, std::as_bytes(std::span{ zeroRift }), PlateRift) &&
               CreateAndSupplyStorage(graph, resources, k_PlateCapCenters, std::as_bytes(std::span{ initial.Caps.Centres }), PlateCapCenters) &&
               CreateAndSupplyStorage(graph, resources, k_PlateCapSpread, std::as_bytes(std::span{ initial.Caps.Spreads }), PlateCapSpread) &&
               CreateAndSupplyStorage(graph, resources, k_PreviousPlate, std::as_bytes(std::span{ initial.PreviousPlate }), PreviousPlate);
    }

    bool PlanetGraphImports::SupplyPresentConstants(loom::Graph& graph,
                                                    ResourceContext& resources,
                                                    const PresentMapParamsHost& params)
    {
        constexpr BufferUsageBits k_PresentUsage =
            BufferUsageBits::VertexShaderUBO | BufferUsageBits::FragmentShaderUBO;
        return CreateAndSupplyUniform(graph, resources, k_PresentConstants, k_PresentUsage, params, PresentConstants);
    }

    bool PlanetGraphImports::UpdateUniforms(ResourceContext& resources,
                                            float time,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t mode,
                                            const PlumeSystemState& plumes,
                                            const ShapingState& shaping)
    {
        const Pass3ParamsHost pass3Params{ .Time = time };
        const UpliftParamsHost upliftParams{ .Time = time };
        const ShapingParamsHost shapingParams = PackShapingUniform(shaping);
        const MovePlatesParamsHost movePlatesParams{ .Time = time, .RiftEnabled = RiftEnabled ? 1u : 0u };
        const PlumeParamsHost plumeParams = PackPlumeUniform(plumes, time);
        const RenderMapParamsHost renderMapParams
        {
            .Width = width,
            .Height = height,
            .Time = time,
            .Mode = mode,
        };

        return UpdateUniformBuffer(resources, Pass3Constants, k_Pass3Constants.Name, pass3Params) &&
               UpdateUniformBuffer(resources, UpliftConstants, k_UpliftConstants.Name, upliftParams) &&
               UpdateUniformBuffer(resources, ShapingConstants, k_ShapingConstants.Name, shapingParams) &&
               UpdateUniformBuffer(resources, MovePlatesConstants, k_MovePlatesConstants.Name, movePlatesParams) &&
               UpdateUniformBuffer(resources, PlumeConstants, k_PlumeConstants.Name, plumeParams) &&
               UpdateUniformBuffer(resources, RenderMapConstants, k_RenderMapConstants.Name, renderMapParams);
    }

    bool PlanetGraphImports::UpdatePresentConstants(ResourceContext& resources,
                                                    const PresentMapParamsHost& params)
    {
        return UpdateUniformBuffer(resources, PresentConstants, k_PresentConstants.Name, params);
    }

    bool PlanetGraphImports::UpdateStepDt(ResourceContext& resources,
                                          const planet::HealpixInitialState& initial,
                                          float dt,
                                          uint32_t order)
    {
        const StepParamsHost stepParams
        {
            .PointCount = initial.PointCount,
            .PlateCount = initial.PlateCount,
            .TimeStepMy = dt,
            .HealpixOrder = order,
        };
        return UpdateUniformBuffer(resources, StepParams, k_StepParams.Name, stepParams);
    }

    bool PlanetGraphImports::ResetSimulationState(loom::Graph& graph,
                                                  ResourceContext& resources,
                                                  const planet::HealpixInitialState& initial,
                                                  float dt,
                                                  uint32_t order,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  uint32_t mode,
                                                  bool use_bytes,
                                                  const PlumeSystemState& plumes,
                                                  const ShapingState& shaping)
    {
        const size_t expectedFlagBytes = static_cast<size_t>(initial.PointCount) *
                                         static_cast<size_t>(initial.PlateCount) *
                                         (use_bytes ? 1u : 4u);

        if (initial.CrustFlags.size() != expectedFlagBytes)
        {
            return false;
        }

        const rhi::GpuWait last = graph.LastSubmit();
        const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
        const std::vector<math::Float4> zeroRift(initial.PlateCount, math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f });

        return UpdateStepDt(resources, initial, dt, order) &&
               UpdateUniforms(resources, initial.InitialTime, width, height, mode, plumes, shaping) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, CrustFlags, k_CrustFlags.Name, initial.CrustFlags) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, CrustState, k_CrustState.Name, std::as_bytes(std::span{ initial.CrustState })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, Rotations, k_Rotations.Name, std::as_bytes(std::span{ initial.Rotations })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, PoleRates, k_PoleRates.Name, std::as_bytes(std::span{ initial.PoleRates })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, PlateRift, k_PlateRift.Name, std::as_bytes(std::span{ zeroRift })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, PlateCapCenters, k_PlateCapCenters.Name, std::as_bytes(std::span{ initial.Caps.Centres })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, PlateCapSpread, k_PlateCapSpread.Name, std::as_bytes(std::span{ initial.Caps.Spreads })) &&
               ResetStorageBuffer(resources, afterGraph, last.Submitted, PreviousPlate, k_PreviousPlate.Name, std::as_bytes(std::span{ initial.PreviousPlate }));
    }

    void PlanetGraphImports::Destroy(ResourceContext& resources)
    {
        DestroyHandle(resources, StepParams);
        DestroyHandle(resources, Pass3Constants);
        DestroyHandle(resources, UpliftConstants);
        DestroyHandle(resources, ShapingConstants);
        DestroyHandle(resources, MovePlatesConstants);
        DestroyHandle(resources, PlumeConstants);
        DestroyHandle(resources, RenderMapConstants);
        DestroyHandle(resources, PresentConstants);
        DestroyHandle(resources, CrustFlags);
        DestroyHandle(resources, CrustState);
        DestroyHandle(resources, Rotations);
        DestroyHandle(resources, PoleRates);
        DestroyHandle(resources, PlateRift);
        DestroyHandle(resources, PlateCapCenters);
        DestroyHandle(resources, PlateCapSpread);
        DestroyHandle(resources, PreviousPlate);
    }
}
