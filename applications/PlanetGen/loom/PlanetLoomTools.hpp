#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_TOOLS_HPP
#define DIAMOND_DOGS_PLANET_LOOM_TOOLS_HPP

#include "MantlePlumes.hpp"
#include "PlanetPresets.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace planet_loom
{
    struct PlanetSimState
    {
        bool Paused{ false };
        float TimeStepMy{ 0.5f };
        int32_t RenderMode{ 0 };
        int32_t RenderProjection{ 0 };
        float ProjectionCenterLonDeg{ 0.0f };
        float ProjectionCenterLatDeg{ 0.0f };
        float CropMinU{ 0.0f };
        float CropMinV{ 0.0f };
        float CropMaxU{ 1.0f };
        float CropMaxV{ 1.0f };
        float Time{ 0.0f };
        int32_t Step{ 0 };
        int32_t Width{ 1024 };
        int32_t Height{ 512 };
        int32_t Order{ 7 };
        bool PlumesEnabled{ true };
        bool PlumeDriftEnabled{ true };
        float PlumeGlobalStrength{ 1.0f };
        float PlumeOceanicErosionRate{ 0.015f };
        float PlumeSwellScale{ 1.0f };
        float PlumeAccretionThresholdKm{ 0.0f };
        int32_t PlumeCount{ 0 };
        float ViewCenterX{ 0.5f };
        float ViewCenterY{ 0.5f };
        float ViewZoom{ 1.0f };
    };

    struct CaptureOutput
    {
        bool Succeeded{ false };
        std::string Error;
        std::string Path;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
    };

    struct TelemetryOutput
    {
        bool Available{ false };
        std::string Error;
        int32_t Step{ 0 };
        float Time{ 0.0f };
        int32_t StepsObserved{ 0 };
        std::string Verdict{ "PASS" };
        std::vector<std::string> Flags;

        uint32_t UpliftOceanToContFlips{ 0u };
        uint32_t UpliftVolcanicToContFlips{ 0u };
        uint32_t UpliftVolcanicOverwritten{ 0u };
        uint32_t PlumeOceanToContFlips{ 0u };
        uint32_t PlumeVolcanicCellsActive{ 0u };

        uint32_t CollisionsTriggered{ 0u };
        uint32_t TerraneCellsTransferred{ 0u };
        uint32_t OceanCellsBorn{ 0u };
        uint32_t SubductionOverlapPoints{ 0u };
        uint32_t SnapOverflowCells{ 0u };

        uint32_t RiftsCompleted{ 0u };
        uint32_t RiftsBlockedNoFreeSlot{ 0u };
        uint32_t RiftsBlockedNotWarm{ 0u };
        uint32_t RiftsBlockedTooSmall{ 0u };
        uint32_t RiftsBlockedPickFailed{ 0u };
        std::string LastRiftOutcome{ "none" };

        double UpliftVolumeAddedKm3{ 0.0 };
        double UpliftLinearErosionKm3{ 0.0 };
        double PlumeVolumeAddedKm3{ 0.0 };
        double PlumeOceanicErosionKm3{ 0.0 };
        double ErosionDiffusionAddedKm3{ 0.0 };
        double ErosionRemovedKm3{ 0.0 };
        uint32_t CollapseCellsActive{ 0u };

        uint32_t PlateFrameLiveCells{ 0u };
        uint32_t WorldCells{ 0u };
        uint32_t ContinentalCells{ 0u };
        uint32_t OceanicCells{ 0u };
        uint32_t VolcanicOceanicCells{ 0u };
        uint32_t AndeanCells{ 0u };
        uint32_t HimalayanCells{ 0u };
        uint32_t IslandArcCells{ 0u };
        uint32_t SubductionFrontCells{ 0u };
        uint32_t CollisionFrontCells{ 0u };

        float LandFraction{ 0.0f };
        float OceanAgeMeanMy{ 0.0f };
        float YoungRidgeFracUnder5My{ 0.0f };
        float LandBelow05KmFrac{ 0.0f };
        float LandAbove5KmFrac{ 0.0f };
        float ReliefP50Km{ 0.0f };
        float ReliefP95Km{ 0.0f };
        float ReliefP99Km{ 0.0f };
        float ReliefMaxKm{ 0.0f };

        int32_t ActivePlates{ 0 };
        int32_t FreeRiftSlots{ 0 };
        int32_t TinyPlatesUnder1Pct{ 0 };
        float LargestPlateShare{ 0.0f };
        float Hhi{ 0.0f };
        float EffectivePlateCount{ 0.0f };
        std::vector<int32_t> PlateCellCounts;
        std::vector<int32_t> PlateContinentalCounts;
    };

    struct DumpStateOutput
    {
        bool Succeeded{ false };
        std::string Error;
        std::string Path;
        uint64_t Bytes{ 0u };
        int32_t Order{ 0 };
        uint32_t PointCount{ 0u };
        uint32_t PlateSlotCount{ 0u };
        uint32_t ActivePlateCount{ 0u };
        int32_t Step{ 0 };
        float Time{ 0.0f };
    };

    struct PassTimingEntry
    {
        std::string Name;
        double TimeUs{ 0.0 };
    };

    struct TimingsOutput
    {
        bool HasData{ false };
        double TotalUs{ 0.0 };
        double TopLevelUs{ 0.0 };
        uint64_t DroppedScopes{ 0u };
        std::vector<PassTimingEntry> Passes;
    };

    struct PlanetToolHost
    {
        PlanetSimState& Sim;
        PlumeSystemState& Plumes;
        ShapingState& Shaping;
        std::function<void(std::string_view field_path)> OnSimChanged;
        std::function<void()> OnPlumesChanged;
        std::function<void(std::string_view field_path)> OnShapingChanged;
        std::function<std::optional<std::string>(ShapingPreset preset)> ApplyShapingPreset;
        std::function<std::optional<std::string>(int32_t steps, float dt)> Step;
        std::function<std::optional<std::string>()> Reset;
        std::function<CaptureOutput(int32_t x,
                                      int32_t y,
                                      int32_t width,
                                      int32_t height,
                                      uint32_t downsample,
                                      int32_t mode,
                                      int32_t projection,
                                      std::string_view name)> CaptureArea;
        std::function<TimingsOutput()> GetTimings;
        std::function<TelemetryOutput()> GetTelemetry;
        std::function<DumpStateOutput(std::string_view name)> DumpState;
    };

    /** Returns canonical debug layer name for mode 0..8, or "unknown". */
    [[nodiscard]] std::string_view RenderModeName(int32_t mode) noexcept;

    /** Parses a layer name or "0".."8" string into mode index, or std::nullopt on failure. */
    [[nodiscard]] std::optional<int32_t> ParseRenderModeName(std::string_view text) noexcept;

    /** Returns canonical projection name for projection 0..5, or "unknown". */
    [[nodiscard]] std::string_view RenderProjectionName(int32_t projection) noexcept;

    /** Parses a projection name or "0".."5" string into projection index, or std::nullopt on failure. */
    [[nodiscard]] std::optional<int32_t> ParseRenderProjectionName(std::string_view text) noexcept;

    /** Copies plume system flags/parameters between reflected PlanetSimState and PlumeSystemState. */
    void SyncSimFromPlumes(PlanetSimState& sim, const PlumeSystemState& plumes) noexcept;
    void SyncPlumesFromSim(PlumeSystemState& plumes, const PlanetSimState& sim) noexcept;

    /** Owns AgentTools registry + TCP JSON-RPC server for live/headless PlanetGenLoom control. */
    class PlanetLoomToolServer
    {
    public:
        explicit PlanetLoomToolServer(PlanetToolHost& host);
        ~PlanetLoomToolServer();

        PlanetLoomToolServer(const PlanetLoomToolServer&) = delete;
        PlanetLoomToolServer& operator=(const PlanetLoomToolServer&) = delete;

        [[nodiscard]] bool Start(int32_t port);
        [[nodiscard]] int32_t Port() const noexcept;
        void Pump();
        void Stop();
        [[nodiscard]] bool QuitRequested() const noexcept;

    private:
        struct State;

        std::unique_ptr<State> state;
    };
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_TOOLS_HPP
