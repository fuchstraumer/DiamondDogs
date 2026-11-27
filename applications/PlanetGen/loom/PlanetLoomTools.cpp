#include "PlanetLoomTools.hpp"
#include "PlanetProjections.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "ToolServer.hpp"
#include "reflect/Describe.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace planet_loom
{
    struct PauseParams
    {
        bool Paused{ true };
    };

    struct StepParams
    {
        int32_t Steps{ 1 };
        float Dt{ 0.0f };
    };

    struct SimStatusReply
    {
        int32_t Step{ 0 };
        float Time{ 0.0f };
        bool Paused{ false };
        int32_t RenderMode{ 0 };
        std::string Layer;
    };

    struct SetLayerParams
    {
        std::string Layer;
    };

    struct LayerReply
    {
        int32_t RenderMode{ 0 };
        std::string Layer;
        std::string Choices;
    };

    struct SetProjectionParams
    {
        std::string Projection;
        float CenterLonDeg{ std::numeric_limits<float>::quiet_NaN() };
        float CenterLatDeg{ std::numeric_limits<float>::quiet_NaN() };
        float CropMinU{ std::numeric_limits<float>::quiet_NaN() };
        float CropMinV{ std::numeric_limits<float>::quiet_NaN() };
        float CropMaxU{ std::numeric_limits<float>::quiet_NaN() };
        float CropMaxV{ std::numeric_limits<float>::quiet_NaN() };
    };

    struct ProjectionReply
    {
        int32_t RenderProjection{ 0 };
        std::string Projection;
        float CenterLonDeg{ 0.0f };
        float CenterLatDeg{ 0.0f };
        float CropMinU{ 0.0f };
        float CropMinV{ 0.0f };
        float CropMaxU{ 1.0f };
        float CropMaxV{ 1.0f };
        std::string Choices;
    };

    struct DumpStateParams
    {
        std::string Name{ "state" };
        // "end": the state now, with no step. A stage name (after_fill_gaps, ...): runs one step and captures the
        // state right after that pass inside it.
        std::string Stage{ "end" };
    };

    struct DumpStateReply
    {
        std::string Path;
        uint64_t Bytes{ 0u };
        int32_t Order{ 0 };
        uint32_t PointCount{ 0u };
        uint32_t PlateSlotCount{ 0u };
        uint32_t ActivePlateCount{ 0u };
        int32_t Step{ 0 };
        float Time{ 0.0f };
        std::string Stage;
    };

    struct PlumeEntry
    {
        int32_t Index{ 0 };
        float LatDeg{ 0.0f };
        float LonDeg{ 0.0f };
        float ConduitRadiusKm{ 95.0f };
        float SwellRadiusKm{ 500.0f };
        float PeakSwellKm{ 1.3f };
        float BuoyancyFlux{ 6.0f };
        float VolcanicUpliftRate{ 2.0f };
        float DriftAzimuthDeg{ 90.0f };
        float DriftSpeedKmPerMy{ 12.0f };
        float BirthTimeMy{ 0.0f };
        float LifetimeMy{ 0.0f };
        float PulsePeriodMy{ 12.0f };
        float PulseAmplitude{ 0.45f };
    };

    struct PlumeListReply
    {
        bool Enabled{ true };
        bool DriftEnabled{ true };
        float GlobalStrength{ 1.0f };
        float OceanicErosionRate{ 0.015f };
        float SwellScale{ 1.0f };
        float AccretionThresholdKm{ 0.0f };
        int32_t Count{ 0 };
        std::vector<PlumeEntry> Plumes;
    };

    struct PlumeAddParams
    {
        float LatDeg{ 0.0f };
        float LonDeg{ 0.0f };
        float ConduitRadiusKm{ 95.0f };
        float SwellRadiusKm{ 500.0f };
        float PeakSwellKm{ 1.3f };
        float BuoyancyFlux{ 6.0f };
        float VolcanicUpliftRate{ 2.0f };
        float DriftAzimuthDeg{ 90.0f };
        float DriftSpeedKmPerMy{ 12.0f };
        float BirthTimeMy{ 0.0f };
        float LifetimeMy{ 0.0f };
        float PulsePeriodMy{ 12.0f };
        float PulseAmplitude{ 0.45f };
    };

    struct PlumeUpdateParams
    {
        int32_t Index{ 0 };
        float LatDeg{ std::numeric_limits<float>::quiet_NaN() };
        float LonDeg{ std::numeric_limits<float>::quiet_NaN() };
        float ConduitRadiusKm{ std::numeric_limits<float>::quiet_NaN() };
        float SwellRadiusKm{ std::numeric_limits<float>::quiet_NaN() };
        float PeakSwellKm{ std::numeric_limits<float>::quiet_NaN() };
        float BuoyancyFlux{ std::numeric_limits<float>::quiet_NaN() };
        float VolcanicUpliftRate{ std::numeric_limits<float>::quiet_NaN() };
        float DriftAzimuthDeg{ std::numeric_limits<float>::quiet_NaN() };
        float DriftSpeedKmPerMy{ std::numeric_limits<float>::quiet_NaN() };
        float BirthTimeMy{ std::numeric_limits<float>::quiet_NaN() };
        float LifetimeMy{ std::numeric_limits<float>::quiet_NaN() };
        float PulsePeriodMy{ std::numeric_limits<float>::quiet_NaN() };
        float PulseAmplitude{ std::numeric_limits<float>::quiet_NaN() };
    };

    struct PlumeRemoveParams
    {
        int32_t Index{ 0 };
    };

    struct PlumeMutateReply
    {
        int32_t Index{ -1 };
        int32_t PlumeCount{ 0 };
    };

    struct CaptureParams
    {
        int32_t Downsample{ 1 };
        int32_t Mode{ -1 };
        int32_t Projection{ -1 };
        std::string Name{ "map" };
    };

    struct CaptureRegionParams
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        int32_t Width{ 256 };
        int32_t Height{ 256 };
        int32_t Mode{ -1 };
        int32_t Projection{ -1 };
        std::string Name{ "region" };
    };

    struct CaptureReply
    {
        std::string Path;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t Step{ 0 };
        float Time{ 0.0f };
    };

    struct SetShapingPresetParams
    {
        std::string Preset;
    };

    struct ShapingPresetReply
    {
        std::string Preset;
        bool SlabDipEnabled{ false };
        bool CoastalNoiseEnabled{ false };
        bool AtollShapingEnabled{ false };
    };

    struct TimingsReply
    {
        bool HasData{ false };
        double TotalUs{ 0.0 };
        double TopLevelUs{ 0.0 };
        uint64_t DroppedScopes{ 0u };
        std::vector<PassTimingEntry> Passes;
    };
}

template<>
struct foundation::reflect::Describe<planet_loom::PlanetSimState>
{
    using State = planet_loom::PlanetSimState;

    static constexpr std::string_view Name{ "planet.SimState" };
    static constexpr std::string_view Doc{ "Live PlanetGen Loom simulation, render layer, projection, view, and mantle plume state." };
    static constexpr std::tuple Fields
    {
        Field<State, bool>{ "Paused", &State::Paused, { .Doc = "Pause or resume continuous live stepping." } },
        Field<State, float>{ "TimeStepMy", &State::TimeStepMy, { .Unit = "My", .Range = FieldRange{ 1.0e-4, 20.0 } } },
        Field<State, int32_t>{ "RenderMode", &State::RenderMode, { .Range = FieldRange{ 0.0, 8.0 }, .Doc = "0=elevation, 1=plates, 2=age, 3=relief, 4=fronts, 5=voronoi, 6=plumes, 7=atlas, 8=raw." } },
        Field<State, int32_t>{ "RenderProjection", &State::RenderProjection, { .Range = FieldRange{ 0.0, 5.0 }, .Doc = "0=equirectangular, 1=equal_earth, 2=mollweide, 3=orthographic, 4=polar_north, 5=polar_south." } },
        Field<State, float>{ "ProjectionCenterLonDeg", &State::ProjectionCenterLonDeg, { .Unit = "deg", .Range = FieldRange{ -360.0, 360.0 } } },
        Field<State, float>{ "ProjectionCenterLatDeg", &State::ProjectionCenterLatDeg, { .Unit = "deg", .Range = FieldRange{ -90.0, 90.0 } } },
        Field<State, float>{ "CropMinU", &State::CropMinU, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "CropMinV", &State::CropMinV, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "CropMaxU", &State::CropMaxU, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "CropMaxV", &State::CropMaxV, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "Time", &State::Time, { .Unit = "My", .ReadOnly = true } },
        Field<State, int32_t>{ "Step", &State::Step, { .ReadOnly = true } },
        Field<State, int32_t>{ "Width", &State::Width, { .Unit = "px", .ReadOnly = true } },
        Field<State, int32_t>{ "Height", &State::Height, { .Unit = "px", .ReadOnly = true } },
        Field<State, int32_t>{ "Order", &State::Order, { .ReadOnly = true } },
        Field<State, bool>{ "PlumesEnabled", &State::PlumesEnabled },
        Field<State, bool>{ "PlumeDriftEnabled", &State::PlumeDriftEnabled },
        Field<State, float>{ "PlumeGlobalStrength", &State::PlumeGlobalStrength, { .Range = FieldRange{ 0.0, 10.0 } } },
        Field<State, float>{ "PlumeOceanicErosionRate", &State::PlumeOceanicErosionRate, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "PlumeSwellScale", &State::PlumeSwellScale, { .Range = FieldRange{ 0.0, 10.0 } } },
        Field<State, float>{ "PlumeAccretionThresholdKm", &State::PlumeAccretionThresholdKm, { .Unit = "km", .Range = FieldRange{ 0.0, 20.0 } } },
        Field<State, int32_t>{ "PlumeCount", &State::PlumeCount, { .ReadOnly = true } },
        Field<State, float>{ "ViewCenterX", &State::ViewCenterX, { .Range = FieldRange{ -2.0, 2.0 } } },
        Field<State, float>{ "ViewCenterY", &State::ViewCenterY, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "ViewZoom", &State::ViewZoom, { .Range = FieldRange{ 0.25, 32.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PauseParams>
{
    using Params = planet_loom::PauseParams;
    static constexpr std::string_view Name{ "planet.PauseParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, bool>{ "Paused", &Params::Paused, { .Required = true } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::StepParams>
{
    using Params = planet_loom::StepParams;
    static constexpr std::string_view Name{ "planet.StepParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, int32_t>{ "Steps", &Params::Steps, { .Range = FieldRange{ 0.0, 100000.0 } } },
        Field<Params, float>{ "Dt", &Params::Dt, { .Unit = "My", .Range = FieldRange{ 0.0, 20.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::SimStatusReply>
{
    using Reply = planet_loom::SimStatusReply;
    static constexpr std::string_view Name{ "planet.SimStatusReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, int32_t>{ "Step", &Reply::Step },
        Field<Reply, float>{ "Time", &Reply::Time, { .Unit = "My" } },
        Field<Reply, bool>{ "Paused", &Reply::Paused },
        Field<Reply, int32_t>{ "RenderMode", &Reply::RenderMode },
        Field<Reply, std::string>{ "Layer", &Reply::Layer },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::SetLayerParams>
{
    using Params = planet_loom::SetLayerParams;
    static constexpr std::string_view Name{ "planet.SetLayerParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, std::string>{ "Layer", &Params::Layer, { .Doc = "elevation|plates|age|relief|fronts|voronoi|plumes or 0..6.", .Required = true } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::LayerReply>
{
    using Reply = planet_loom::LayerReply;
    static constexpr std::string_view Name{ "planet.LayerReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, int32_t>{ "RenderMode", &Reply::RenderMode },
        Field<Reply, std::string>{ "Layer", &Reply::Layer },
        Field<Reply, std::string>{ "Choices", &Reply::Choices },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeEntry>
{
    using Entry = planet_loom::PlumeEntry;
    static constexpr std::string_view Name{ "planet.PlumeEntry" };
    static constexpr std::tuple Fields
    {
        Field<Entry, int32_t>{ "Index", &Entry::Index },
        Field<Entry, float>{ "LatDeg", &Entry::LatDeg, { .Unit = "deg" } },
        Field<Entry, float>{ "LonDeg", &Entry::LonDeg, { .Unit = "deg" } },
        Field<Entry, float>{ "ConduitRadiusKm", &Entry::ConduitRadiusKm, { .Unit = "km" } },
        Field<Entry, float>{ "SwellRadiusKm", &Entry::SwellRadiusKm, { .Unit = "km" } },
        Field<Entry, float>{ "PeakSwellKm", &Entry::PeakSwellKm, { .Unit = "km" } },
        Field<Entry, float>{ "BuoyancyFlux", &Entry::BuoyancyFlux },
        Field<Entry, float>{ "VolcanicUpliftRate", &Entry::VolcanicUpliftRate },
        Field<Entry, float>{ "DriftAzimuthDeg", &Entry::DriftAzimuthDeg, { .Unit = "deg" } },
        Field<Entry, float>{ "DriftSpeedKmPerMy", &Entry::DriftSpeedKmPerMy, { .Unit = "km/My" } },
        Field<Entry, float>{ "BirthTimeMy", &Entry::BirthTimeMy, { .Unit = "My" } },
        Field<Entry, float>{ "LifetimeMy", &Entry::LifetimeMy, { .Unit = "My" } },
        Field<Entry, float>{ "PulsePeriodMy", &Entry::PulsePeriodMy, { .Unit = "My" } },
        Field<Entry, float>{ "PulseAmplitude", &Entry::PulseAmplitude },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeListReply>
{
    using Reply = planet_loom::PlumeListReply;
    static constexpr std::string_view Name{ "planet.PlumeListReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, bool>{ "Enabled", &Reply::Enabled },
        Field<Reply, bool>{ "DriftEnabled", &Reply::DriftEnabled },
        Field<Reply, float>{ "GlobalStrength", &Reply::GlobalStrength },
        Field<Reply, float>{ "OceanicErosionRate", &Reply::OceanicErosionRate },
        Field<Reply, float>{ "SwellScale", &Reply::SwellScale },
        Field<Reply, float>{ "AccretionThresholdKm", &Reply::AccretionThresholdKm, { .Unit = "km" } },
        Field<Reply, int32_t>{ "Count", &Reply::Count },
        Field<Reply, std::vector<planet_loom::PlumeEntry>>{ "Plumes", &Reply::Plumes },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeAddParams>
{
    using Params = planet_loom::PlumeAddParams;
    static constexpr std::string_view Name{ "planet.PlumeAddParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, float>{ "LatDeg", &Params::LatDeg, { .Unit = "deg", .Range = FieldRange{ -90.0, 90.0 }, .Required = true } },
        Field<Params, float>{ "LonDeg", &Params::LonDeg, { .Unit = "deg", .Range = FieldRange{ -360.0, 360.0 }, .Required = true } },
        Field<Params, float>{ "ConduitRadiusKm", &Params::ConduitRadiusKm, { .Unit = "km", .Range = FieldRange{ 5.0, 2000.0 } } },
        Field<Params, float>{ "SwellRadiusKm", &Params::SwellRadiusKm, { .Unit = "km", .Range = FieldRange{ 10.0, 5000.0 } } },
        Field<Params, float>{ "PeakSwellKm", &Params::PeakSwellKm, { .Unit = "km", .Range = FieldRange{ 0.0, 10.0 } } },
        Field<Params, float>{ "BuoyancyFlux", &Params::BuoyancyFlux, { .Range = FieldRange{ 0.0, 100.0 } } },
        Field<Params, float>{ "VolcanicUpliftRate", &Params::VolcanicUpliftRate, { .Range = FieldRange{ 0.0, 50.0 } } },
        Field<Params, float>{ "DriftAzimuthDeg", &Params::DriftAzimuthDeg, { .Unit = "deg" } },
        Field<Params, float>{ "DriftSpeedKmPerMy", &Params::DriftSpeedKmPerMy, { .Unit = "km/My", .Range = FieldRange{ 0.0, 200.0 } } },
        Field<Params, float>{ "BirthTimeMy", &Params::BirthTimeMy, { .Unit = "My" } },
        Field<Params, float>{ "LifetimeMy", &Params::LifetimeMy, { .Unit = "My", .Range = FieldRange{ 0.0, 10000.0 } } },
        Field<Params, float>{ "PulsePeriodMy", &Params::PulsePeriodMy, { .Unit = "My", .Range = FieldRange{ 0.0, 1000.0 } } },
        Field<Params, float>{ "PulseAmplitude", &Params::PulseAmplitude, { .Range = FieldRange{ 0.0, 2.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeUpdateParams>
{
    using Params = planet_loom::PlumeUpdateParams;
    static constexpr std::string_view Name{ "planet.PlumeUpdateParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, int32_t>{ "Index", &Params::Index, { .Range = FieldRange{ 0.0, 63.0 }, .Required = true } },
        Field<Params, float>{ "LatDeg", &Params::LatDeg, { .Unit = "deg" } },
        Field<Params, float>{ "LonDeg", &Params::LonDeg, { .Unit = "deg" } },
        Field<Params, float>{ "ConduitRadiusKm", &Params::ConduitRadiusKm, { .Unit = "km" } },
        Field<Params, float>{ "SwellRadiusKm", &Params::SwellRadiusKm, { .Unit = "km" } },
        Field<Params, float>{ "PeakSwellKm", &Params::PeakSwellKm, { .Unit = "km" } },
        Field<Params, float>{ "BuoyancyFlux", &Params::BuoyancyFlux },
        Field<Params, float>{ "VolcanicUpliftRate", &Params::VolcanicUpliftRate },
        Field<Params, float>{ "DriftAzimuthDeg", &Params::DriftAzimuthDeg, { .Unit = "deg" } },
        Field<Params, float>{ "DriftSpeedKmPerMy", &Params::DriftSpeedKmPerMy, { .Unit = "km/My" } },
        Field<Params, float>{ "BirthTimeMy", &Params::BirthTimeMy, { .Unit = "My" } },
        Field<Params, float>{ "LifetimeMy", &Params::LifetimeMy, { .Unit = "My" } },
        Field<Params, float>{ "PulsePeriodMy", &Params::PulsePeriodMy, { .Unit = "My" } },
        Field<Params, float>{ "PulseAmplitude", &Params::PulseAmplitude },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeRemoveParams>
{
    using Params = planet_loom::PlumeRemoveParams;
    static constexpr std::string_view Name{ "planet.PlumeRemoveParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, int32_t>{ "Index", &Params::Index, { .Range = FieldRange{ 0.0, 63.0 }, .Required = true } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PlumeMutateReply>
{
    using Reply = planet_loom::PlumeMutateReply;
    static constexpr std::string_view Name{ "planet.PlumeMutateReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, int32_t>{ "Index", &Reply::Index },
        Field<Reply, int32_t>{ "PlumeCount", &Reply::PlumeCount },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::CaptureParams>
{
    using Params = planet_loom::CaptureParams;
    static constexpr std::string_view Name{ "planet.CaptureParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, int32_t>{ "Downsample", &Params::Downsample, { .Range = FieldRange{ 1.0, 64.0 } } },
        Field<Params, int32_t>{ "Mode", &Params::Mode, { .Range = FieldRange{ -1.0, 8.0 } } },
        Field<Params, int32_t>{ "Projection", &Params::Projection, { .Range = FieldRange{ -1.0, 5.0 } } },
        Field<Params, std::string>{ "Name", &Params::Name },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::CaptureRegionParams>
{
    using Params = planet_loom::CaptureRegionParams;
    static constexpr std::string_view Name{ "planet.CaptureRegionParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, int32_t>{ "X", &Params::X, { .Unit = "px" } },
        Field<Params, int32_t>{ "Y", &Params::Y, { .Unit = "px" } },
        Field<Params, int32_t>{ "Width", &Params::Width, { .Unit = "px" } },
        Field<Params, int32_t>{ "Height", &Params::Height, { .Unit = "px" } },
        Field<Params, int32_t>{ "Mode", &Params::Mode, { .Range = FieldRange{ -1.0, 8.0 } } },
        Field<Params, int32_t>{ "Projection", &Params::Projection, { .Range = FieldRange{ -1.0, 5.0 } } },
        Field<Params, std::string>{ "Name", &Params::Name },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::CaptureReply>
{
    using Reply = planet_loom::CaptureReply;
    static constexpr std::string_view Name{ "planet.CaptureReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, std::string>{ "Path", &Reply::Path },
        Field<Reply, int32_t>{ "Width", &Reply::Width, { .Unit = "px" } },
        Field<Reply, int32_t>{ "Height", &Reply::Height, { .Unit = "px" } },
        Field<Reply, int32_t>{ "Step", &Reply::Step },
        Field<Reply, float>{ "Time", &Reply::Time, { .Unit = "My" } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::PassTimingEntry>
{
    using Entry = planet_loom::PassTimingEntry;
    static constexpr std::string_view Name{ "planet.PassTimingEntry" };
    static constexpr std::tuple Fields
    {
        Field<Entry, std::string>{ "Name", &Entry::Name },
        Field<Entry, double>{ "TimeUs", &Entry::TimeUs, { .Unit = "us" } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::ShapingState>
{
    using State = planet_loom::ShapingState;

    static constexpr std::string_view Name{ "planet.ShapingState" };
    static constexpr std::string_view Doc{ "Cortial (2019) slab-dip uplift, spherical NoiseGraph coastline/ridge shaping, and Atoll World controls." };
    static constexpr std::tuple Fields
    {
        Field<State, bool>{ "SlabDipEnabled", &State::SlabDipEnabled },
        Field<State, bool>{ "CoastalNoiseEnabled", &State::CoastalNoiseEnabled },
        Field<State, bool>{ "AtollShapingEnabled", &State::AtollShapingEnabled },
        Field<State, bool>{ "CustomProfileLut", &State::CustomProfileLut },
        Field<State, float>{ "BaseUplift", &State::BaseUplift, { .Unit = "km/My", .Range = FieldRange{ 0.0, 5.0 } } },
        Field<State, float>{ "ContinentErosion", &State::ContinentErosion, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<State, float>{ "AccretionRelief", &State::AccretionRelief, { .Unit = "km", .Range = FieldRange{ 0.2, 6.0 } } },
        Field<State, float>{ "AccretionStartRelief", &State::AccretionStartRelief, { .Unit = "km", .Range = FieldRange{ 0.0, 2.0 } } },
        Field<State, float>{ "ContinentalReachKm", &State::ContinentalReachKm, { .Unit = "km", .Range = FieldRange{ 100.0, 3000.0 } } },
        Field<State, float>{ "IslandArcReachKm", &State::IslandArcReachKm, { .Unit = "km", .Range = FieldRange{ 50.0, 1500.0 } } },
        Field<State, float>{ "ArcDistanceKm", &State::ArcDistanceKm, { .Unit = "km", .Range = FieldRange{ 40.0, 800.0 } } },
        Field<State, float>{ "CollisionReachKm", &State::CollisionReachKm, { .Unit = "km", .Range = FieldRange{ 100.0, 2000.0 } } },
        Field<State, float>{ "CollisionUpliftScale", &State::CollisionUpliftScale, { .Range = FieldRange{ 0.0, 3.0 } } },
        Field<State, float>{ "SlabDipAgeWeight", &State::SlabDipAgeWeight, { .Range = FieldRange{ 0.0, 3.0 } } },
        Field<State, float>{ "SlabDipSpeedWeight", &State::SlabDipSpeedWeight, { .Range = FieldRange{ 0.0, 3.0 } } },
        Field<State, float>{ "TrenchDepthKm", &State::TrenchDepthKm, { .Unit = "km", .Range = FieldRange{ 0.0, 5.0 } } },
        Field<State, float>{ "CoastalWarpAmplitude", &State::CoastalWarpAmplitude, { .Range = FieldRange{ 0.0, 0.2 } } },
        Field<State, float>{ "CoastalDetailKm", &State::CoastalDetailKm, { .Unit = "km", .Range = FieldRange{ 0.0, 4.0 } } },
        Field<State, float>{ "AtollLagoonStrength", &State::AtollLagoonStrength, { .Range = FieldRange{ 0.0, 4.0 } } },
        Field<State, float>{ "NoiseFrequency", &State::NoiseFrequency, { .Range = FieldRange{ 0.5, 40.0 } } },
        Field<State, uint32_t>{ "NoiseSeed", &State::NoiseSeed },
        Field<State, float>{ "AtollLandBias", &State::AtollLandBias, { .Unit = "km", .Range = FieldRange{ -3.0, 3.0 } } },
        Field<State, bool>{ "ErosionEnabled", &State::ErosionEnabled },
        Field<State, float>{ "ErosionDiffusivity", &State::ErosionDiffusivity, { .Unit = "km^2/My", .Range = FieldRange{ 0.0, 5000.0 } } },
        Field<State, float>{ "CollapseElevationKm", &State::CollapseElevationKm, { .Unit = "km", .Range = FieldRange{ 1.0, 10.0 } } },
        Field<State, float>{ "CollapseTimeMy", &State::CollapseTimeMy, { .Unit = "My", .Range = FieldRange{ 0.0, 100.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::SetShapingPresetParams>
{
    using Params = planet_loom::SetShapingPresetParams;
    static constexpr std::string_view Name{ "planet.SetShapingPresetParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, std::string>{ "Preset", &Params::Preset, { .Doc = "legacy|earth|atoll", .Required = true } },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::ShapingPresetReply>
{
    using Reply = planet_loom::ShapingPresetReply;
    static constexpr std::string_view Name{ "planet.ShapingPresetReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, std::string>{ "Preset", &Reply::Preset },
        Field<Reply, bool>{ "SlabDipEnabled", &Reply::SlabDipEnabled },
        Field<Reply, bool>{ "CoastalNoiseEnabled", &Reply::CoastalNoiseEnabled },
        Field<Reply, bool>{ "AtollShapingEnabled", &Reply::AtollShapingEnabled },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::TimingsReply>
{
    using Reply = planet_loom::TimingsReply;
    static constexpr std::string_view Name{ "planet.TimingsReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, bool>{ "HasData", &Reply::HasData },
        Field<Reply, double>{ "TotalUs", &Reply::TotalUs, { .Unit = "us" } },
        Field<Reply, double>{ "TopLevelUs", &Reply::TopLevelUs, { .Unit = "us" } },
        Field<Reply, uint64_t>{ "DroppedScopes", &Reply::DroppedScopes },
        Field<Reply, std::vector<planet_loom::PassTimingEntry>>{ "Passes", &Reply::Passes },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::SetProjectionParams>
{
    using Params = planet_loom::SetProjectionParams;
    static constexpr std::string_view Name{ "planet.SetProjectionParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, std::string>{ "Projection", &Params::Projection, { .Doc = "equirectangular|equal_earth|mollweide|orthographic|polar_north|polar_south or 0..5.", .Required = true } },
        Field<Params, float>{ "CenterLonDeg", &Params::CenterLonDeg, { .Unit = "deg" } },
        Field<Params, float>{ "CenterLatDeg", &Params::CenterLatDeg, { .Unit = "deg" } },
        Field<Params, float>{ "CropMinU", &Params::CropMinU },
        Field<Params, float>{ "CropMinV", &Params::CropMinV },
        Field<Params, float>{ "CropMaxU", &Params::CropMaxU },
        Field<Params, float>{ "CropMaxV", &Params::CropMaxV },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::ProjectionReply>
{
    using Reply = planet_loom::ProjectionReply;
    static constexpr std::string_view Name{ "planet.ProjectionReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, int32_t>{ "RenderProjection", &Reply::RenderProjection },
        Field<Reply, std::string>{ "Projection", &Reply::Projection },
        Field<Reply, float>{ "CenterLonDeg", &Reply::CenterLonDeg, { .Unit = "deg" } },
        Field<Reply, float>{ "CenterLatDeg", &Reply::CenterLatDeg, { .Unit = "deg" } },
        Field<Reply, float>{ "CropMinU", &Reply::CropMinU },
        Field<Reply, float>{ "CropMinV", &Reply::CropMinV },
        Field<Reply, float>{ "CropMaxU", &Reply::CropMaxU },
        Field<Reply, float>{ "CropMaxV", &Reply::CropMaxV },
        Field<Reply, std::string>{ "Choices", &Reply::Choices },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::DumpStateParams>
{
    using Params = planet_loom::DumpStateParams;
    static constexpr std::string_view Name{ "planet.DumpStateParams" };
    static constexpr std::tuple Fields
    {
        Field<Params, std::string>{ "Name", &Params::Name },
        Field<Params, std::string>{ "Stage", &Params::Stage },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::DumpStateReply>
{
    using Reply = planet_loom::DumpStateReply;
    static constexpr std::string_view Name{ "planet.DumpStateReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, std::string>{ "Path", &Reply::Path },
        Field<Reply, uint64_t>{ "Bytes", &Reply::Bytes },
        Field<Reply, int32_t>{ "Order", &Reply::Order },
        Field<Reply, uint32_t>{ "PointCount", &Reply::PointCount },
        Field<Reply, uint32_t>{ "PlateSlotCount", &Reply::PlateSlotCount },
        Field<Reply, uint32_t>{ "ActivePlateCount", &Reply::ActivePlateCount },
        Field<Reply, int32_t>{ "Step", &Reply::Step },
        Field<Reply, float>{ "Time", &Reply::Time, { .Unit = "My" } },
        Field<Reply, std::string>{ "Stage", &Reply::Stage },
    };
};

template<>
struct foundation::reflect::Describe<planet_loom::TelemetryOutput>
{
    using Reply = planet_loom::TelemetryOutput;
    static constexpr std::string_view Name{ "planet.TelemetryReply" };
    static constexpr std::tuple Fields
    {
        Field<Reply, bool>{ "Available", &Reply::Available },
        Field<Reply, int32_t>{ "Step", &Reply::Step },
        Field<Reply, float>{ "Time", &Reply::Time, { .Unit = "My" } },
        Field<Reply, int32_t>{ "StepsObserved", &Reply::StepsObserved },
        Field<Reply, std::string>{ "Verdict", &Reply::Verdict },
        Field<Reply, std::vector<std::string>>{ "Flags", &Reply::Flags },
        Field<Reply, uint32_t>{ "UpliftOceanToContFlips", &Reply::UpliftOceanToContFlips },
        Field<Reply, uint32_t>{ "UpliftVolcanicToContFlips", &Reply::UpliftVolcanicToContFlips },
        Field<Reply, uint32_t>{ "UpliftVolcanicOverwritten", &Reply::UpliftVolcanicOverwritten },
        Field<Reply, uint32_t>{ "PlumeOceanToContFlips", &Reply::PlumeOceanToContFlips },
        Field<Reply, uint32_t>{ "PlumeVolcanicCellsActive", &Reply::PlumeVolcanicCellsActive },
        Field<Reply, uint32_t>{ "CollisionsTriggered", &Reply::CollisionsTriggered },
        Field<Reply, uint32_t>{ "TerraneCellsTransferred", &Reply::TerraneCellsTransferred },
        Field<Reply, uint32_t>{ "OceanCellsBorn", &Reply::OceanCellsBorn },
        Field<Reply, uint32_t>{ "SubductionOverlapPoints", &Reply::SubductionOverlapPoints },
        Field<Reply, uint32_t>{ "SnapOverflowCells", &Reply::SnapOverflowCells },
        Field<Reply, uint32_t>{ "RiftsCompleted", &Reply::RiftsCompleted },
        Field<Reply, uint32_t>{ "RiftsBlockedNoFreeSlot", &Reply::RiftsBlockedNoFreeSlot },
        Field<Reply, uint32_t>{ "RiftsBlockedNotWarm", &Reply::RiftsBlockedNotWarm },
        Field<Reply, uint32_t>{ "RiftsBlockedTooSmall", &Reply::RiftsBlockedTooSmall },
        Field<Reply, uint32_t>{ "RiftsBlockedPickFailed", &Reply::RiftsBlockedPickFailed },
        Field<Reply, std::string>{ "LastRiftOutcome", &Reply::LastRiftOutcome },
        Field<Reply, double>{ "UpliftVolumeAddedKm3", &Reply::UpliftVolumeAddedKm3, { .Unit = "km^3" } },
        Field<Reply, double>{ "UpliftLinearErosionKm3", &Reply::UpliftLinearErosionKm3, { .Unit = "km^3" } },
        Field<Reply, double>{ "PlumeVolumeAddedKm3", &Reply::PlumeVolumeAddedKm3, { .Unit = "km^3" } },
        Field<Reply, double>{ "PlumeOceanicErosionKm3", &Reply::PlumeOceanicErosionKm3, { .Unit = "km^3" } },
        Field<Reply, double>{ "ErosionDiffusionAddedKm3", &Reply::ErosionDiffusionAddedKm3, { .Unit = "km^3" } },
        Field<Reply, double>{ "ErosionRemovedKm3", &Reply::ErosionRemovedKm3, { .Unit = "km^3" } },
        Field<Reply, uint32_t>{ "CollapseCellsActive", &Reply::CollapseCellsActive },
        Field<Reply, uint32_t>{ "PlateFrameLiveCells", &Reply::PlateFrameLiveCells },
        Field<Reply, uint32_t>{ "WorldCells", &Reply::WorldCells },
        Field<Reply, uint32_t>{ "ContinentalCells", &Reply::ContinentalCells },
        Field<Reply, uint32_t>{ "OceanicCells", &Reply::OceanicCells },
        Field<Reply, uint32_t>{ "VolcanicOceanicCells", &Reply::VolcanicOceanicCells },
        Field<Reply, uint32_t>{ "AndeanCells", &Reply::AndeanCells },
        Field<Reply, uint32_t>{ "HimalayanCells", &Reply::HimalayanCells },
        Field<Reply, uint32_t>{ "IslandArcCells", &Reply::IslandArcCells },
        Field<Reply, uint32_t>{ "SubductionFrontCells", &Reply::SubductionFrontCells },
        Field<Reply, uint32_t>{ "CollisionFrontCells", &Reply::CollisionFrontCells },
        Field<Reply, float>{ "LandFraction", &Reply::LandFraction },
        Field<Reply, float>{ "OceanAgeMeanMy", &Reply::OceanAgeMeanMy, { .Unit = "My" } },
        Field<Reply, float>{ "YoungRidgeFracUnder5My", &Reply::YoungRidgeFracUnder5My },
        Field<Reply, float>{ "LandBelow05KmFrac", &Reply::LandBelow05KmFrac },
        Field<Reply, float>{ "LandAbove5KmFrac", &Reply::LandAbove5KmFrac },
        Field<Reply, float>{ "ReliefP50Km", &Reply::ReliefP50Km, { .Unit = "km" } },
        Field<Reply, float>{ "ReliefP95Km", &Reply::ReliefP95Km, { .Unit = "km" } },
        Field<Reply, float>{ "ReliefP99Km", &Reply::ReliefP99Km, { .Unit = "km" } },
        Field<Reply, float>{ "ReliefMaxKm", &Reply::ReliefMaxKm, { .Unit = "km" } },
        Field<Reply, int32_t>{ "ActivePlates", &Reply::ActivePlates },
        Field<Reply, int32_t>{ "FreeRiftSlots", &Reply::FreeRiftSlots },
        Field<Reply, int32_t>{ "TinyPlatesUnder1Pct", &Reply::TinyPlatesUnder1Pct },
        Field<Reply, float>{ "LargestPlateShare", &Reply::LargestPlateShare },
        Field<Reply, float>{ "Hhi", &Reply::Hhi },
        Field<Reply, float>{ "EffectivePlateCount", &Reply::EffectivePlateCount },
        Field<Reply, std::vector<int32_t>>{ "PlateCellCounts", &Reply::PlateCellCounts },
        Field<Reply, std::vector<int32_t>>{ "PlateContinentalCounts", &Reply::PlateContinentalCounts },
    };
};

namespace
{
    constexpr size_t k_MaxCaptureNameLength = 64u;
    constexpr int32_t k_MaxToolRequestsPerPump = 16;
    constexpr std::string_view k_LayerChoices{ "elevation, plates, age, relief, fronts, voronoi, plumes, atlas, raw" };
    constexpr std::string_view k_ProjectionChoices{ "equirectangular, equal_earth, mollweide, orthographic, polar_north, polar_south" };

    constexpr std::array<std::string_view, 9> k_LayerNames
    {
        "elevation",
        "plates",
        "age",
        "relief",
        "fronts",
        "voronoi",
        "plumes",
        "atlas",
        "raw",
    };

    [[nodiscard]] bool IsSafeCaptureName(std::string_view name) noexcept;
    [[nodiscard]] planet_loom::SimStatusReply MakeSimStatus(const planet_loom::PlanetSimState& sim);
    [[nodiscard]] planet_loom::PlumeEntry MakePlumeEntry(int32_t index, const planet_loom::MantlePlumeDesc& plume);

    struct ToolBridge
    {
        planet_loom::PlanetToolHost& Host;
        bool Quit{ false };

        void OnSimChanged(std::string_view field_path)
        {
            if (Host.OnSimChanged)
            {
                Host.OnSimChanged(field_path);
            }
        }

        void OnShapingChanged(std::string_view field_path)
        {
            if (Host.OnShapingChanged)
            {
                Host.OnShapingChanged(field_path);
            }
        }

        void NotifyPlumesChanged()
        {
            SyncSimFromPlumes(Host.Sim, Host.Plumes);

            if (Host.OnPlumesChanged)
            {
                Host.OnPlumesChanged();
            }
        }

        void HandleCapture(int32_t x,
                           int32_t y,
                           int32_t width,
                           int32_t height,
                           uint32_t downsample,
                           int32_t mode,
                           int32_t projection,
                           std::string_view name,
                           std::string_view default_name,
                           agent::ReplyToken<planet_loom::CaptureReply> token)
        {
            if (!IsSafeCaptureName(name))
            {
                token.Fail(std::format("the name '{}' is invalid: use up to {} letters, digits, '_' and '-'",
                                       name, k_MaxCaptureNameLength));
                return;
            }

            if (!Host.CaptureArea)
            {
                token.Fail("capture is unavailable in this session");
                return;
            }

            const std::string captureName{ name.empty() ? default_name : name };
            const planet_loom::CaptureOutput output =
                Host.CaptureArea(x, y, width, height, downsample, mode, projection, captureName);

            if (!output.Succeeded)
            {
                token.Fail(output.Error.empty() ? "capture failed to write PNG" : output.Error);
                return;
            }

            token.Complete(planet_loom::CaptureReply
            {
                .Path = output.Path,
                .Width = output.Width,
                .Height = output.Height,
                .Step = Host.Sim.Step,
                .Time = Host.Sim.Time,
            });
        }
    };

    struct PauseCommand
    {
        static constexpr std::string_view Name{ "planet.pause" };
        static constexpr std::string_view Doc{ "Pauses or resumes continuous live simulation stepping." };
        using Params = planet_loom::PauseParams;
        using Reply = planet_loom::SimStatusReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            Bridge.Host.Sim.Paused = params.Paused;
            Bridge.OnSimChanged("Paused");
            token.Complete(MakeSimStatus(Bridge.Host.Sim));
        }

        ToolBridge& Bridge;
    };

    struct StepCommand
    {
        static constexpr std::string_view Name{ "planet.step" };
        static constexpr std::string_view Doc{ "Executes Steps simulation ticks synchronously and updates MapOutput." };
        using Params = planet_loom::StepParams;
        using Reply = planet_loom::SimStatusReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            if (!Bridge.Host.Step)
            {
                token.Fail("step handler is unavailable");
                return;
            }

            const std::optional<std::string> error = Bridge.Host.Step(params.Steps, params.Dt);

            if (error.has_value())
            {
                token.Fail(*error);
                return;
            }

            token.Complete(MakeSimStatus(Bridge.Host.Sim));
        }

        ToolBridge& Bridge;
    };

    struct ResetCommand
    {
        static constexpr std::string_view Name{ "planet.reset" };
        static constexpr std::string_view Doc{ "Resets simulation buffers to initial step 0 and updates MapOutput." };
        using Params = agent::NoParams;
        using Reply = planet_loom::SimStatusReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);

            if (!Bridge.Host.Reset)
            {
                token.Fail("reset handler is unavailable");
                return;
            }

            const std::optional<std::string> error = Bridge.Host.Reset();

            if (error.has_value())
            {
                token.Fail(*error);
                return;
            }

            token.Complete(MakeSimStatus(Bridge.Host.Sim));
        }

        ToolBridge& Bridge;
    };

    struct SetLayerCommand
    {
        static constexpr std::string_view Name{ "planet.set_layer" };
        static constexpr std::string_view Doc{ "Selects debug visualization layer by name (elevation..raw) or index (0..8)." };
        using Params = planet_loom::SetLayerParams;
        using Reply = planet_loom::LayerReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const std::optional<int32_t> parsed = planet_loom::ParseRenderModeName(params.Layer);

            if (!parsed.has_value())
            {
                token.Fail(std::format("unknown layer '{}'; valid choices: {} (or 0..8)", params.Layer, k_LayerChoices));
                return;
            }

            Bridge.Host.Sim.RenderMode = *parsed;
            Bridge.OnSimChanged("RenderMode");

            token.Complete(Reply
            {
                .RenderMode = *parsed,
                .Layer = std::string{ planet_loom::RenderModeName(*parsed) },
                .Choices = std::string{ k_LayerChoices },
            });
        }

        ToolBridge& Bridge;
    };

    struct PlumeListCommand
    {
        static constexpr std::string_view Name{ "planet.plume.list" };
        static constexpr std::string_view Doc{ "Lists global mantle plume settings and all authored hotspots." };
        using Params = agent::NoParams;
        using Reply = planet_loom::PlumeListReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);
            const planet_loom::PlumeSystemState& plumes = Bridge.Host.Plumes;

            Reply reply
            {
                .Enabled = plumes.Enabled,
                .DriftEnabled = plumes.DriftEnabled,
                .GlobalStrength = plumes.GlobalStrength,
                .OceanicErosionRate = plumes.OceanicErosionRate,
                .SwellScale = plumes.SwellScale,
                .AccretionThresholdKm = plumes.AccretionThresholdKm,
                .Count = static_cast<int32_t>(plumes.Plumes.size()),
                .Plumes = {},
            };
            reply.Plumes.reserve(plumes.Plumes.size());

            for (size_t i = 0u; i < plumes.Plumes.size(); ++i)
            {
                reply.Plumes.push_back(MakePlumeEntry(static_cast<int32_t>(i), plumes.Plumes[i]));
            }

            token.Complete(std::move(reply));
        }

        ToolBridge& Bridge;
    };

    struct PlumeAddCommand
    {
        static constexpr std::string_view Name{ "planet.plume.add" };
        static constexpr std::string_view Doc{ "Adds a mantle plume hotspot at LatDeg/LonDeg (up to 64 active plumes)." };
        using Params = planet_loom::PlumeAddParams;
        using Reply = planet_loom::PlumeMutateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            planet_loom::PlumeSystemState& plumes = Bridge.Host.Plumes;

            if (plumes.Plumes.size() >= planet_loom::k_MaxMantlePlumes)
            {
                token.Fail(std::format("maximum {} mantle plumes already reached", planet_loom::k_MaxMantlePlumes));
                return;
            }

            plumes.Plumes.push_back(planet_loom::MantlePlumeDesc
            {
                .LatDeg = params.LatDeg,
                .LonDeg = params.LonDeg,
                .ConduitRadiusKm = params.ConduitRadiusKm,
                .SwellRadiusKm = params.SwellRadiusKm,
                .PeakSwellKm = params.PeakSwellKm,
                .BuoyancyFlux = params.BuoyancyFlux,
                .VolcanicUpliftRate = params.VolcanicUpliftRate,
                .DriftAzimuthDeg = params.DriftAzimuthDeg,
                .DriftSpeedKmPerMy = params.DriftSpeedKmPerMy,
                .BirthTimeMy = params.BirthTimeMy,
                .LifetimeMy = params.LifetimeMy,
                .PulsePeriodMy = params.PulsePeriodMy,
                .PulseAmplitude = params.PulseAmplitude,
            });
            Bridge.NotifyPlumesChanged();

            const int32_t count = static_cast<int32_t>(plumes.Plumes.size());
            token.Complete(Reply{ .Index = count - 1, .PlumeCount = count });
        }

        ToolBridge& Bridge;
    };

    struct PlumeUpdateCommand
    {
        static constexpr std::string_view Name{ "planet.plume.update" };
        static constexpr std::string_view Doc{ "Updates selected fields of an existing mantle plume by Index." };
        using Params = planet_loom::PlumeUpdateParams;
        using Reply = planet_loom::PlumeMutateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            planet_loom::PlumeSystemState& plumes = Bridge.Host.Plumes;

            if (params.Index < 0 || static_cast<size_t>(params.Index) >= plumes.Plumes.size())
            {
                token.Fail(std::format("plume index {} out of range [0, {})", params.Index, plumes.Plumes.size()));
                return;
            }

            planet_loom::MantlePlumeDesc& plume = plumes.Plumes[static_cast<size_t>(params.Index)];

            if (std::isfinite(params.LatDeg))
            {
                plume.LatDeg = std::clamp(params.LatDeg, -90.0f, 90.0f);
            }
            if (std::isfinite(params.LonDeg))
            {
                plume.LonDeg = std::clamp(params.LonDeg, -360.0f, 360.0f);
            }
            if (std::isfinite(params.ConduitRadiusKm))
            {
                plume.ConduitRadiusKm = std::max(params.ConduitRadiusKm, 5.0f);
            }
            if (std::isfinite(params.SwellRadiusKm))
            {
                plume.SwellRadiusKm = std::max(params.SwellRadiusKm, 10.0f);
            }
            if (std::isfinite(params.PeakSwellKm))
            {
                plume.PeakSwellKm = std::max(params.PeakSwellKm, 0.0f);
            }
            if (std::isfinite(params.BuoyancyFlux))
            {
                plume.BuoyancyFlux = std::max(params.BuoyancyFlux, 0.0f);
            }
            if (std::isfinite(params.VolcanicUpliftRate))
            {
                plume.VolcanicUpliftRate = std::max(params.VolcanicUpliftRate, 0.0f);
            }
            if (std::isfinite(params.DriftAzimuthDeg))
            {
                plume.DriftAzimuthDeg = params.DriftAzimuthDeg;
            }
            if (std::isfinite(params.DriftSpeedKmPerMy))
            {
                plume.DriftSpeedKmPerMy = std::max(params.DriftSpeedKmPerMy, 0.0f);
            }
            if (std::isfinite(params.BirthTimeMy))
            {
                plume.BirthTimeMy = params.BirthTimeMy;
            }
            if (std::isfinite(params.LifetimeMy))
            {
                plume.LifetimeMy = std::max(params.LifetimeMy, 0.0f);
            }
            if (std::isfinite(params.PulsePeriodMy))
            {
                plume.PulsePeriodMy = std::max(params.PulsePeriodMy, 0.0f);
            }
            if (std::isfinite(params.PulseAmplitude))
            {
                plume.PulseAmplitude = std::clamp(params.PulseAmplitude, 0.0f, 2.0f);
            }

            Bridge.NotifyPlumesChanged();
            token.Complete(Reply
            {
                .Index = params.Index,
                .PlumeCount = static_cast<int32_t>(plumes.Plumes.size()),
            });
        }

        ToolBridge& Bridge;
    };

    struct PlumeRemoveCommand
    {
        static constexpr std::string_view Name{ "planet.plume.remove" };
        static constexpr std::string_view Doc{ "Removes a mantle plume by Index." };
        using Params = planet_loom::PlumeRemoveParams;
        using Reply = planet_loom::PlumeMutateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            planet_loom::PlumeSystemState& plumes = Bridge.Host.Plumes;

            if (params.Index < 0 || static_cast<size_t>(params.Index) >= plumes.Plumes.size())
            {
                token.Fail(std::format("plume index {} out of range [0, {})", params.Index, plumes.Plumes.size()));
                return;
            }

            plumes.Plumes.erase(plumes.Plumes.begin() + static_cast<std::ptrdiff_t>(params.Index));
            Bridge.NotifyPlumesChanged();

            token.Complete(Reply
            {
                .Index = params.Index,
                .PlumeCount = static_cast<int32_t>(plumes.Plumes.size()),
            });
        }

        ToolBridge& Bridge;
    };

    struct PlumeClearCommand
    {
        static constexpr std::string_view Name{ "planet.plume.clear" };
        static constexpr std::string_view Doc{ "Clears all authored mantle plumes." };
        using Params = agent::NoParams;
        using Reply = planet_loom::PlumeMutateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);
            Bridge.Host.Plumes.Plumes.clear();
            Bridge.NotifyPlumesChanged();
            token.Complete(Reply{ .Index = -1, .PlumeCount = 0 });
        }

        ToolBridge& Bridge;
    };

    struct CaptureCommand
    {
        static constexpr std::string_view Name{ "planet.capture" };
        static constexpr std::string_view Doc{ "Captures the full MapOutput image to a PNG file with optional Mode and Projection override." };
        using Params = planet_loom::CaptureParams;
        using Reply = planet_loom::CaptureReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const uint32_t downsample = static_cast<uint32_t>(std::max(params.Downsample, 1));
            Bridge.HandleCapture(0, 0, Bridge.Host.Sim.Width, Bridge.Host.Sim.Height,
                                 downsample, params.Mode, params.Projection, params.Name, "map", std::move(token));
        }

        ToolBridge& Bridge;
    };

    struct CaptureRegionCommand
    {
        static constexpr std::string_view Name{ "planet.capture_region" };
        static constexpr std::string_view Doc{ "Captures a clamped 1:1 sub-rectangle of MapOutput to a PNG file." };
        using Params = planet_loom::CaptureRegionParams;
        using Reply = planet_loom::CaptureReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const int32_t x = std::clamp(params.X, 0, Bridge.Host.Sim.Width - 1);
            const int32_t y = std::clamp(params.Y, 0, Bridge.Host.Sim.Height - 1);
            const int32_t width = std::clamp(params.Width, 1, Bridge.Host.Sim.Width - x);
            const int32_t height = std::clamp(params.Height, 1, Bridge.Host.Sim.Height - y);

            Bridge.HandleCapture(x, y, width, height, 1u, params.Mode, params.Projection, params.Name, "region", std::move(token));
        }

        ToolBridge& Bridge;
    };

    struct SetProjectionCommand
    {
        static constexpr std::string_view Name{ "planet.set_projection" };
        static constexpr std::string_view Doc{ "Selects map projection (equirectangular, equal_earth, mollweide, orthographic, polar_north, polar_south) and optional center/crop." };
        using Params = planet_loom::SetProjectionParams;
        using Reply = planet_loom::ProjectionReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const std::optional<int32_t> parsed = planet_loom::ParseRenderProjectionName(params.Projection);

            if (!parsed.has_value())
            {
                token.Fail(std::format("unknown projection '{}'; valid choices: {} (or 0..5)",
                                       params.Projection, k_ProjectionChoices));
                return;
            }

            planet_loom::PlanetSimState& sim = Bridge.Host.Sim;
            sim.RenderProjection = *parsed;

            if (std::isfinite(params.CenterLonDeg))
            {
                sim.ProjectionCenterLonDeg = std::clamp(params.CenterLonDeg, -360.0f, 360.0f);
            }

            if (std::isfinite(params.CenterLatDeg))
            {
                sim.ProjectionCenterLatDeg = std::clamp(params.CenterLatDeg, -90.0f, 90.0f);
            }

            if (std::isfinite(params.CropMinU))
            {
                sim.CropMinU = std::clamp(params.CropMinU, 0.0f, 1.0f);
            }

            if (std::isfinite(params.CropMinV))
            {
                sim.CropMinV = std::clamp(params.CropMinV, 0.0f, 1.0f);
            }

            if (std::isfinite(params.CropMaxU))
            {
                sim.CropMaxU = std::clamp(params.CropMaxU, 0.0f, 1.0f);
            }

            if (std::isfinite(params.CropMaxV))
            {
                sim.CropMaxV = std::clamp(params.CropMaxV, 0.0f, 1.0f);
            }

            Bridge.OnSimChanged("RenderProjection");

            token.Complete(Reply
            {
                .RenderProjection = sim.RenderProjection,
                .Projection = std::string{ planet_loom::RenderProjectionName(sim.RenderProjection) },
                .CenterLonDeg = sim.ProjectionCenterLonDeg,
                .CenterLatDeg = sim.ProjectionCenterLatDeg,
                .CropMinU = sim.CropMinU,
                .CropMinV = sim.CropMinV,
                .CropMaxU = sim.CropMaxU,
                .CropMaxV = sim.CropMaxV,
                .Choices = std::string{ k_ProjectionChoices },
            });
        }

        ToolBridge& Bridge;
    };

    struct TelemetryCommand
    {
        static constexpr std::string_view Name{ "planet.telemetry" };
        static constexpr std::string_view Doc{ "Returns structured GPU telemetry counters, plate ecology, volume budgets, and PASS/WARN/FAIL gates." };
        using Params = agent::NoParams;
        using Reply = planet_loom::TelemetryOutput;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);

            if (!Bridge.Host.GetTelemetry)
            {
                token.Fail("telemetry is unavailable in this session");
                return;
            }

            const planet_loom::TelemetryOutput output = Bridge.Host.GetTelemetry();

            if (!output.Available && !output.Error.empty())
            {
                token.Fail(output.Error);
                return;
            }

            token.Complete(output);
        }

        ToolBridge& Bridge;
    };

    struct DumpStateCommand
    {
        static constexpr std::string_view Name{ "planet.dump_state" };
        static constexpr std::string_view Doc{ "Exports a compact binary snapshot (.pghp) of all 1D nested-HEALPix cells without 2D map projection. "
                                               "Stage 'end' captures the state now. A stage name (after_swap_to_majority, after_fill_gaps, "
                                               "after_remove_subducted, after_collision, after_erosion, after_rift) runs one step and "
                                               "captures the state right after that pass (front is from step N-1 before after_remove_subducted)." };
        using Params = planet_loom::DumpStateParams;
        using Reply = planet_loom::DumpStateReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            if (!IsSafeCaptureName(params.Name))
            {
                token.Fail(std::format("the name '{}' is invalid: use up to {} letters, digits, '_' and '-'",
                                       params.Name, k_MaxCaptureNameLength));
                return;
            }

            if (!Bridge.Host.DumpState)
            {
                token.Fail("state dump is unavailable in this session");
                return;
            }

            const std::string dumpName{ params.Name.empty() ? "state" : params.Name };
            const std::string stage{ params.Stage.empty() ? "end" : params.Stage };
            const planet_loom::DumpStateOutput output = Bridge.Host.DumpState(dumpName, stage);

            if (!output.Succeeded)
            {
                token.Fail(output.Error.empty() ? "state dump failed" : output.Error);
                return;
            }

            token.Complete(Reply
            {
                .Path = output.Path,
                .Bytes = output.Bytes,
                .Order = output.Order,
                .PointCount = output.PointCount,
                .PlateSlotCount = output.PlateSlotCount,
                .ActivePlateCount = output.ActivePlateCount,
                .Step = output.Step,
                .Time = output.Time,
                .Stage = output.Stage,
            });
        }

        ToolBridge& Bridge;
    };

    struct SetShapingPresetCommand
    {
        static constexpr std::string_view Name{ "planet.shaping.preset" };
        static constexpr std::string_view Doc{ "Applies a named shaping preset (legacy, earth, or atoll) and re-renders MapOutput." };
        using Params = planet_loom::SetShapingPresetParams;
        using Reply = planet_loom::ShapingPresetReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            const std::optional<planet_loom::ShapingPreset> preset = planet_loom::ParseShapingPreset(params.Preset);

            if (!preset.has_value())
            {
                token.Fail(std::format("unknown shaping preset '{}'; expected legacy, earth, or atoll", params.Preset));
                return;
            }

            if (Bridge.Host.ApplyShapingPreset)
            {
                const std::optional<std::string> error = Bridge.Host.ApplyShapingPreset(*preset);
                if (error.has_value())
                {
                    token.Fail(*error);
                    return;
                }
            }
            else
            {
                const bool erosionEnabled = Bridge.Host.Shaping.ErosionEnabled;
                Bridge.Host.Shaping = planet_loom::ShapingStateForPreset(*preset, Bridge.Host.Shaping.NoiseSeed);
                Bridge.Host.Shaping.ErosionEnabled = erosionEnabled;
                Bridge.OnShapingChanged("");
            }

            token.Complete(Reply
            {
                .Preset = params.Preset,
                .SlabDipEnabled = Bridge.Host.Shaping.SlabDipEnabled,
                .CoastalNoiseEnabled = Bridge.Host.Shaping.CoastalNoiseEnabled,
                .AtollShapingEnabled = Bridge.Host.Shaping.AtollShapingEnabled,
            });
        }

        ToolBridge& Bridge;
    };

    struct TimingsCommand
    {
        static constexpr std::string_view Name{ "planet.timings" };
        static constexpr std::string_view Doc{ "Returns the latest completed GPU per-pass timings in microseconds." };
        using Params = agent::NoParams;
        using Reply = planet_loom::TimingsReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);

            if (!Bridge.Host.GetTimings)
            {
                token.Complete(Reply{});
                return;
            }

            const planet_loom::TimingsOutput timings = Bridge.Host.GetTimings();
            token.Complete(Reply
            {
                .HasData = timings.HasData,
                .TotalUs = timings.TotalUs,
                .TopLevelUs = timings.TopLevelUs,
                .DroppedScopes = timings.DroppedScopes,
                .Passes = timings.Passes,
            });
        }

        ToolBridge& Bridge;
    };

    struct QuitCommand
    {
        static constexpr std::string_view Name{ "planet.quit" };
        static constexpr std::string_view Doc{ "Closes the PlanetGenLoom session cleanly." };
        using Params = agent::NoParams;
        using Reply = agent::NoReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token)
        {
            static_cast<void>(params);
            Bridge.Quit = true;
            token.Complete(agent::NoReply{});
        }

        ToolBridge& Bridge;
    };

    bool IsSafeCaptureName(std::string_view name) noexcept
    {
        if (name.size() > k_MaxCaptureNameLength)
        {
            return false;
        }

        for (const char ch : name)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (!std::isalnum(uch) && ch != '_' && ch != '-')
            {
                return false;
            }
        }

        return true;
    }

    planet_loom::SimStatusReply MakeSimStatus(const planet_loom::PlanetSimState& sim)
    {
        return planet_loom::SimStatusReply
        {
            .Step = sim.Step,
            .Time = sim.Time,
            .Paused = sim.Paused,
            .RenderMode = sim.RenderMode,
            .Layer = std::string{ planet_loom::RenderModeName(sim.RenderMode) },
        };
    }

    planet_loom::PlumeEntry MakePlumeEntry(int32_t index, const planet_loom::MantlePlumeDesc& plume)
    {
        return planet_loom::PlumeEntry
        {
            .Index = index,
            .LatDeg = plume.LatDeg,
            .LonDeg = plume.LonDeg,
            .ConduitRadiusKm = plume.ConduitRadiusKm,
            .SwellRadiusKm = plume.SwellRadiusKm,
            .PeakSwellKm = plume.PeakSwellKm,
            .BuoyancyFlux = plume.BuoyancyFlux,
            .VolcanicUpliftRate = plume.VolcanicUpliftRate,
            .DriftAzimuthDeg = plume.DriftAzimuthDeg,
            .DriftSpeedKmPerMy = plume.DriftSpeedKmPerMy,
            .BirthTimeMy = plume.BirthTimeMy,
            .LifetimeMy = plume.LifetimeMy,
            .PulsePeriodMy = plume.PulsePeriodMy,
            .PulseAmplitude = plume.PulseAmplitude,
        };
    }
}

namespace planet_loom
{
    std::string_view RenderModeName(int32_t mode) noexcept
    {
        if (mode < 0 || static_cast<size_t>(mode) >= k_LayerNames.size())
        {
            return "unknown";
        }

        return k_LayerNames[static_cast<size_t>(mode)];
    }

    std::optional<int32_t> ParseRenderModeName(std::string_view text) noexcept
    {
        int32_t numeric = -1;
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), numeric);

        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && numeric >= 0 && numeric <= 8)
        {
            return numeric;
        }

        for (size_t i = 0u; i < k_LayerNames.size(); ++i)
        {
            if (text == k_LayerNames[i])
            {
                return static_cast<int32_t>(i);
            }
        }

        return std::nullopt;
    }

    std::string_view RenderProjectionName(int32_t projection) noexcept
    {
        if (ShaderIndexToProjection(projection) == RenderProjection::Invalid)
        {
            return "unknown";
        }

        return k_Projections[static_cast<size_t>(projection)].Name;
    }

    std::optional<int32_t> ParseRenderProjectionName(std::string_view text) noexcept
    {
        int32_t numeric = -1;
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), numeric);

        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size())
        {
            if (ShaderIndexToProjection(numeric) == RenderProjection::Invalid)
            {
                return std::nullopt;
            }

            return numeric;
        }

        const RenderProjection named = ProjectionFromName(text);

        if (named == RenderProjection::Invalid)
        {
            return std::nullopt;
        }

        return static_cast<int32_t>(ProjectionToShaderIndex(named));
    }

    void SyncSimFromPlumes(PlanetSimState& sim, const PlumeSystemState& plumes) noexcept
    {
        sim.PlumesEnabled = plumes.Enabled;
        sim.PlumeDriftEnabled = plumes.DriftEnabled;
        sim.PlumeGlobalStrength = plumes.GlobalStrength;
        sim.PlumeOceanicErosionRate = plumes.OceanicErosionRate;
        sim.PlumeSwellScale = plumes.SwellScale;
        sim.PlumeAccretionThresholdKm = plumes.AccretionThresholdKm;
        sim.PlumeCount = static_cast<int32_t>(plumes.Plumes.size());
    }

    void SyncPlumesFromSim(PlumeSystemState& plumes, const PlanetSimState& sim) noexcept
    {
        plumes.Enabled = sim.PlumesEnabled;
        plumes.DriftEnabled = sim.PlumeDriftEnabled;
        plumes.GlobalStrength = sim.PlumeGlobalStrength;
        plumes.OceanicErosionRate = sim.PlumeOceanicErosionRate;
        plumes.SwellScale = sim.PlumeSwellScale;
        plumes.AccretionThresholdKm = sim.PlumeAccretionThresholdKm;
    }

    struct PlanetLoomToolServer::State
    {
        explicit State(PlanetToolHost& host)
            : Bridge{ .Host = host }
            , PauseCmd{ Bridge }
            , StepCmd{ Bridge }
            , ResetCmd{ Bridge }
            , SetLayerCmd{ Bridge }
            , SetProjectionCmd{ Bridge }
            , PlumeListCmd{ Bridge }
            , PlumeAddCmd{ Bridge }
            , PlumeUpdateCmd{ Bridge }
            , PlumeRemoveCmd{ Bridge }
            , PlumeClearCmd{ Bridge }
            , CaptureCmd{ Bridge }
            , CaptureRegionCmd{ Bridge }
            , ShapingPresetCmd{ Bridge }
            , TimingsCmd{ Bridge }
            , TelemetryCmd{ Bridge }
            , DumpStateCmd{ Bridge }
            , QuitCmd{ Bridge }
        {
        }

        ToolBridge Bridge;
        PauseCommand PauseCmd;
        StepCommand StepCmd;
        ResetCommand ResetCmd;
        SetLayerCommand SetLayerCmd;
        SetProjectionCommand SetProjectionCmd;
        PlumeListCommand PlumeListCmd;
        PlumeAddCommand PlumeAddCmd;
        PlumeUpdateCommand PlumeUpdateCmd;
        PlumeRemoveCommand PlumeRemoveCmd;
        PlumeClearCommand PlumeClearCmd;
        CaptureCommand CaptureCmd;
        CaptureRegionCommand CaptureRegionCmd;
        SetShapingPresetCommand ShapingPresetCmd;
        TimingsCommand TimingsCmd;
        TelemetryCommand TelemetryCmd;
        DumpStateCommand DumpStateCmd;
        QuitCommand QuitCmd;
        agent::ToolRegistry Registry;
        agent::ToolServer Server;
    };

    PlanetLoomToolServer::PlanetLoomToolServer(PlanetToolHost& host)
        : state{ std::make_unique<State>(host) }
    {
        const agent::ObjectChanged changed =
            agent::ObjectChanged::Create<ToolBridge, &ToolBridge::OnSimChanged>(&state->Bridge);
        const agent::ObjectChanged shapingChanged =
            agent::ObjectChanged::Create<ToolBridge, &ToolBridge::OnShapingChanged>(&state->Bridge);

        const std::array errors
        {
            state->Registry.AddObject("planet.sim", state->Bridge.Host.Sim, changed),
            state->Registry.AddObject("planet.shaping", state->Bridge.Host.Shaping, shapingChanged),
            state->Registry.AddCommand(state->PauseCmd),
            state->Registry.AddCommand(state->StepCmd),
            state->Registry.AddCommand(state->ResetCmd),
            state->Registry.AddCommand(state->SetLayerCmd),
            state->Registry.AddCommand(state->SetProjectionCmd),
            state->Registry.AddCommand(state->PlumeListCmd),
            state->Registry.AddCommand(state->PlumeAddCmd),
            state->Registry.AddCommand(state->PlumeUpdateCmd),
            state->Registry.AddCommand(state->PlumeRemoveCmd),
            state->Registry.AddCommand(state->PlumeClearCmd),
            state->Registry.AddCommand(state->CaptureCmd),
            state->Registry.AddCommand(state->CaptureRegionCmd),
            state->Registry.AddCommand(state->ShapingPresetCmd),
            state->Registry.AddCommand(state->TimingsCmd),
            state->Registry.AddCommand(state->TelemetryCmd),
            state->Registry.AddCommand(state->DumpStateCmd),
            state->Registry.AddCommand(state->QuitCmd),
        };

        for (const agent::ToolResultCode code : errors)
        {
            if (code != agent::ToolResultCode::Success)
            {
                std::println(stderr, "PlanetGenLoom: tool registration failed: {}", agent::ToString(code));
            }
        }

        state->Registry.Freeze();
    }

    PlanetLoomToolServer::~PlanetLoomToolServer()
    {
        Stop();
    }

    bool PlanetLoomToolServer::Start(int32_t port)
    {
        const agent::ToolServerResult result = state->Server.Start(port);

        if (result != agent::ToolServerResult::Success)
        {
            std::println(stderr, "PlanetGenLoom: ToolServer failed to start on port {}: {}",
                         port, agent::ToString(result));
            return false;
        }

        std::println("ToolServer listening on 127.0.0.1:{}", state->Server.Port());
        std::fflush(stdout);
        return true;
    }

    int32_t PlanetLoomToolServer::Port() const noexcept
    {
        return state->Server.Port();
    }

    void PlanetLoomToolServer::Pump()
    {
        state->Server.Pump(state->Registry, k_MaxToolRequestsPerPump);
    }

    void PlanetLoomToolServer::Stop()
    {
        state->Server.Stop();
    }

    bool PlanetLoomToolServer::QuitRequested() const noexcept
    {
        return state->Bridge.Quit;
    }
}
