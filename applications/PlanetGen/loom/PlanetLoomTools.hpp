#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_TOOLS_HPP
#define DIAMOND_DOGS_PLANET_LOOM_TOOLS_HPP

#include "MantlePlumes.hpp"
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
        std::function<void(std::string_view field_path)> OnSimChanged;
        std::function<void()> OnPlumesChanged;
        std::function<std::optional<std::string>(int32_t steps, float dt)> Step;
        std::function<std::optional<std::string>()> Reset;
        std::function<CaptureOutput(int32_t x,
                                      int32_t y,
                                      int32_t width,
                                      int32_t height,
                                      uint32_t downsample,
                                      int32_t mode,
                                      std::string_view name)> CaptureArea;
        std::function<TimingsOutput()> GetTimings;
    };

    /** Returns canonical debug layer name for mode 0..6, or "unknown". */
    [[nodiscard]] std::string_view RenderModeName(int32_t mode) noexcept;

    /** Parses a layer name or "0".."6" string into mode index, or std::nullopt on failure. */
    [[nodiscard]] std::optional<int32_t> ParseRenderModeName(std::string_view text) noexcept;

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
