#include "PlanetLiveApp.hpp"
#include "CommandBuffer.hpp"
#include "Compile.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceStage.hpp"
#include "FrameCapture.hpp"
#include "FramePlan.hpp"
#include "GpuTimers.hpp"
#include "GraphBuilder.hpp"
#include "HeadlessBoot.hpp"
#include "Healpix.hpp"
#include "ImageDataFormats.hpp"
#include "ImGuiModule.hpp"
#include "LoadedBundle.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomGraph.hpp"
#include "MantlePlumes.hpp"
#include "PlatformSystem.hpp"
#include "PlanetGraph.hpp"
#include "PlanetLoomTools.hpp"
#include "RecordContext.hpp"
#include "ResourceContext.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiSystem.hpp"
#include "ShaderBundle.hpp"
#include "SpineNeeds.hpp"
#include "Swapchain.hpp"
#include "SwapchainFrameTarget.hpp"
#include "WindowedBoot.hpp"
#include "math/MathStorage.hpp"
#include "utility/Delegate.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace planet_loom
{
    namespace
    {
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
        constexpr double k_SettleSeconds = 0.05;

        constexpr bootstrap::BundleRequest k_Bundles[]
        {
            bootstrap::BundleRequest
            {
                .FileName = "ShaderLibrary.ldmanifest",
                .BuildTreePath = PLANET_GEN_MANIFEST,
                .Profile = rhi::k_SpirvProfileName,
            },
        };

        struct PendingCaptureRequest
        {
            int32_t X{ 0 };
            int32_t Y{ 0 };
            int32_t Width{ 1024 };
            int32_t Height{ 512 };
            uint32_t Downsample{ 1u };
            std::string Name;
        };

        struct HealpixDumpHeader
        {
            char Magic[4]{ 'P', 'G', 'H', 'P' };
            uint32_t Version{ 1u };
            uint32_t HealpixOrder{ 0u };
            uint32_t PointCount{ 0u };
            uint32_t PlateSlotCount{ 0u };
            int32_t Step{ 0 };
            float TimeMy{ 0.0f };
            uint32_t RecordStrideBytes{ 24u };
            uint32_t ActivePlateCount{ 0u };
            uint32_t Flags{ 0u };
            uint32_t Reserved[6]{};
        };
        static_assert(sizeof(HealpixDumpHeader) == 64u);

        // the UI combo index is the shader index (PlanetProjections.hpp asserts the table order)
        [[nodiscard]] consteval std::array<const char*, k_Projections.size()> MakeProjectionLabels()
        {
            std::array<const char*, k_Projections.size()> labels{};

            for (size_t index = 0u; index < k_Projections.size(); ++index)
            {
                labels[index] = k_Projections[index].Label;
            }

            return labels;
        }

        inline constexpr std::array<const char*, k_Projections.size()> k_ProjectionLabels = MakeProjectionLabels();

        [[nodiscard]] std::string FormatDiagnosticValue(const loom::DiagnosticValue& value);
        void PrintLiveDiagnostics(const loom::CollectingSink& sink);
        [[nodiscard]] std::vector<bootstrap::Need> BuildDeviceNeeds(bool timestamps);
        [[nodiscard]] std::vector<std::byte> ReadBackBuffer(ResourceContext& resources,
                                                            const loom::Graph& graph,
                                                            GraphicsResource source,
                                                            size_t bytes);
        [[nodiscard]] std::string_view RiftOutcomeName(uint32_t outcome) noexcept;
        [[nodiscard]] TelemetryOutput BuildTelemetryOutput(std::span<const uint32_t> words,
                                                           uint32_t point_count,
                                                           uint32_t plate_count,
                                                           int32_t step,
                                                           float time_my);

        class LiveSessionController;

        class PlanetUiOverlay
        {
        public:
            explicit PlanetUiOverlay(ImGuiModule& imgui_module) noexcept
                : imgui{ &imgui_module }
            {
            }

            void AttachController(LiveSessionController* controller) noexcept
            {
                host = controller;
            }

            void Begin();
            void End();
            void Record(loom::RecordContext& ctx);

            int32_t PendingSteps{ 0 };
            bool PendingReset{ false };
            bool PendingCapture{ false };
            std::string LastCaptureMessage;

        private:
            void handleViewportMouse();
            void drawControlWindow();
            void drawPlumesSection();
            void drawShapingSection();
            void drawTimingsSection();

            ImGuiModule* imgui{ nullptr };
            LiveSessionController* host{ nullptr };
            bool rendered{ false };
            MantlePlumeDesc newPlume{
                .LatDeg = 18.0f,
                .LonDeg = -155.0f,
                .ConduitRadiusKm = 95.0f,
                .SwellRadiusKm = 520.0f,
                .PeakSwellKm = 1.4f,
                .BuoyancyFlux = 6.5f,
                .VolcanicUpliftRate = 2.2f,
                .DriftAzimuthDeg = 120.0f,
                .DriftSpeedKmPerMy = 12.0f,
                .BirthTimeMy = 0.0f,
                .LifetimeMy = 0.0f,
                .PulsePeriodMy = 12.0f,
                .PulseAmplitude = 0.45f,
            };
        };

        class LiveSessionController
        {
        public:
            LiveSessionController(ResourceContext& resources,
                                  const rhi::Device& device,
                                  const planet::HealpixInitialState& initial,
                                  capture::FrameCapture& frame_capture,
                                  std::filesystem::path capture_dir,
                                  PlatformWindowSystem* window_platform,
                                  const Swapchain* swapchain,
                                  const PlanetLiveOptions& opts)
                : Resources{ resources }
                , Device{ device }
                , Initial{ initial }
                , Capture{ frame_capture }
                , CaptureDir{ std::move(capture_dir) }
                , Platform{ window_platform }
                , ActiveSwapchain{ swapchain }
                , Preset{ opts.Preset }
                , Seed{ opts.Seed }
                , UseBytes{ !opts.Words }
                , LiveWindowed{ window_platform != nullptr }
                , TelemetryEnabled{ opts.Telemetry }
                , Plumes{ opts.PlumesEnabled ? PresetPlumes(opts.Preset, initial.InitialTime, opts.Seed) : EmptyPlumeSystem() }
                , Shaping{ ShapingStateForPreset(opts.Shaping, static_cast<uint32_t>(opts.Seed)) }
                , Sim{
                    .Paused = opts.StartPaused,
                    .TimeStepMy = opts.Dt,
                    .RenderMode = static_cast<int32_t>(opts.Mode),
                    .RenderProjection = static_cast<int32_t>(opts.Projection),
                    .Time = initial.InitialTime,
                    .Step = 0,
                    .Width = static_cast<int32_t>(opts.Width),
                    .Height = static_cast<int32_t>(opts.Height),
                    .Order = static_cast<int32_t>(opts.Order),
                }
            {
                SyncSimFromPlumes(Sim, Plumes);
            }

            void AttachGraph(loom::Graph& graph_ref, PlanetUiOverlay* ui_overlay) noexcept
            {
                Graph = &graph_ref;
                Ui = ui_overlay;
            }

            void RecordCapture(loom::RecordContext& ctx);

            [[nodiscard]] bool SupplyGraphResources(const PlanetLiveOptions& opts);
            [[nodiscard]] RenderMapView MakeRenderMapView() const noexcept;
            [[nodiscard]] bool UpdateAllUniforms();
            [[nodiscard]] bool TickOnce(bool step_simulation, bool& out_submitted);
            void OnSimChanged(std::string_view field_path);
            void OnPlumesChanged();
            void OnShapingChanged(std::string_view field_path);
            [[nodiscard]] std::optional<std::string> ApplyShapingPreset(ShapingPreset preset);
            [[nodiscard]] std::optional<std::string> Step(int32_t steps, float dt);
            [[nodiscard]] std::optional<std::string> Reset();
            [[nodiscard]] CaptureOutput CaptureArea(int32_t x,
                                                      int32_t y,
                                                      int32_t width,
                                                      int32_t height,
                                                      uint32_t downsample,
                                                      int32_t mode,
                                                      int32_t projection,
                                                      std::string_view name);
            [[nodiscard]] TimingsOutput GetTimings();
            [[nodiscard]] TelemetryOutput GetTelemetry();
            [[nodiscard]] DumpStateOutput DumpState(std::string_view name, std::string_view stage);
            void DrainCompletedCaptures();
            void Shutdown();

            ResourceContext& Resources;
            const rhi::Device& Device;
            const planet::HealpixInitialState& Initial;
            capture::FrameCapture& Capture;
            std::filesystem::path CaptureDir;
            PlatformWindowSystem* Platform{ nullptr };
            const Swapchain* ActiveSwapchain{ nullptr };
            PlanetPreset Preset{ PlanetPreset::Reference };
            uint64_t Seed{ 42ull };
            bool UseBytes{ true };
            bool LiveWindowed{ false };
            bool TelemetryEnabled{ false };
            PlumeSystemState Plumes;
            ShapingState Shaping;
            PlanetSimState Sim;
            PlanetGraphImports Imports;
            loom::Graph* Graph{ nullptr };
            PlanetUiOverlay* Ui{ nullptr };
            bool StepDtDirty{ false };
            std::optional<PendingCaptureRequest> PendingCapture;
            std::optional<uint64_t> LastCaptureTicket;
            std::vector<capture::CaptureResult> RecentCaptureResults;

        private:
            [[nodiscard]] PresentMapParamsHost makePresentParams() const noexcept;
        };

        void PlanetUiOverlay::Begin()
        {
            imgui->BeginFrame();
            rendered = false;

            if (host != nullptr)
            {
                handleViewportMouse();
                drawControlWindow();
            }
        }

        void PlanetUiOverlay::End()
        {
            if (!rendered)
            {
                ImGui::EndFrame();
            }
        }

        void PlanetUiOverlay::Record(loom::RecordContext& ctx)
        {
            imgui->Render(ctx.CommandBuffer().Handle());
            rendered = true;
        }

        void PlanetUiOverlay::handleViewportMouse()
        {
            ImGuiIO& io = ImGui::GetIO();

            if (io.WantCaptureMouse)
            {
                return;
            }

            if (io.MouseWheel != 0.0f)
            {
                const float factor = std::pow(1.15f, io.MouseWheel);
                host->Sim.ViewZoom = std::clamp(host->Sim.ViewZoom * factor, 0.25f, 32.0f);
            }

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float refPixels = std::max(io.DisplaySize.y, 1.0f);
                const float mapAspect = static_cast<float>(std::max(host->Sim.Width, 1)) /
                                        static_cast<float>(std::max(host->Sim.Height, 1));
                const float zoom = std::max(host->Sim.ViewZoom, 0.25f);
                host->Sim.ViewCenterX -= io.MouseDelta.x / (refPixels * zoom * mapAspect);
                host->Sim.ViewCenterX = host->Sim.ViewCenterX - std::floor(host->Sim.ViewCenterX);
                host->Sim.ViewCenterY = std::clamp(host->Sim.ViewCenterY - io.MouseDelta.y / (refPixels * zoom),
                                                   0.0f, 1.0f);
            }
        }

        void PlanetUiOverlay::drawControlWindow()
        {
            ImGui::SetNextWindowSize(ImVec2{ 400.0f, 640.0f }, ImGuiCond_FirstUseEver);
            ImGui::Begin("PlanetGen (Loom)");

            ImGui::Text("Step %d | %.2f My | Order %d (%u pts, %u plates)",
                        host->Sim.Step, host->Sim.Time, host->Sim.Order,
                        host->Initial.PointCount, host->Initial.PlateCount);
            ImGui::Text("FPS %.1f (%.2f ms) | %s",
                        ImGui::GetIO().Framerate,
                        1000.0f / std::max(ImGui::GetIO().Framerate, 1.0f),
                        host->Sim.Paused ? "PAUSED" : "RUNNING");

            if (ImGui::Button(host->Sim.Paused ? "Play" : "Pause"))
            {
                host->Sim.Paused = !host->Sim.Paused;
            }
            ImGui::SameLine();
            if (ImGui::Button("+1"))
            {
                PendingSteps += 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("+10"))
            {
                PendingSteps += 10;
            }
            ImGui::SameLine();
            if (ImGui::Button("+50"))
            {
                PendingSteps += 50;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
            {
                PendingReset = true;
            }

            if (ImGui::SliderFloat("dt (My)", &host->Sim.TimeStepMy, 0.05f, 2.5f, "%.2f"))
            {
                host->StepDtDirty = true;
            }

            ImGui::SeparatorText("Debug Layer");
            static constexpr const char* k_ModeLabels[]
            {
                "0: Elevation",
                "1: Plates",
                "2: Ocean Age",
                "3: Relief",
                "4: Fronts",
                "5: Voronoi",
                "6: Mantle Plumes",
                "7: Atlas (shaded relief)",
            };
            ImGui::Combo("Layer", &host->Sim.RenderMode, k_ModeLabels, 8);

            ImGui::Combo("Projection", &host->Sim.RenderProjection, k_ProjectionLabels.data(),
                         static_cast<int>(k_ProjectionLabels.size()));

            if (host->Sim.RenderProjection > 0)
            {
                ImGui::SliderFloat("Center Lon", &host->Sim.ProjectionCenterLonDeg, -180.0f, 180.0f, "%.1f deg");

                if (host->Sim.RenderProjection == 3)
                {
                    ImGui::SliderFloat("Center Lat", &host->Sim.ProjectionCenterLatDeg, -90.0f, 90.0f, "%.1f deg");
                }
            }

            ImGui::SliderFloat("Zoom", &host->Sim.ViewZoom, 0.5f, 8.0f, "%.2fx");
            ImGui::SameLine();
            if (ImGui::Button("Reset View"))
            {
                host->Sim.ViewCenterX = 0.5f;
                host->Sim.ViewCenterY = 0.5f;
                host->Sim.ViewZoom = 1.0f;
            }
            ImGui::TextDisabled("Drag left mouse on map to pan, wheel to zoom");

            if (ImGui::Button("Capture PNG"))
            {
                PendingCapture = true;
            }
            if (!LastCaptureMessage.empty())
            {
                ImGui::SameLine();
                ImGui::TextWrapped("%s", LastCaptureMessage.c_str());
            }

            drawPlumesSection();
            drawShapingSection();
            drawTimingsSection();

            ImGui::End();
        }

        void PlanetUiOverlay::drawPlumesSection()
        {
            if (!ImGui::CollapsingHeader("Mantle Plumes", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            bool dirty = false;
            dirty |= ImGui::Checkbox("Plumes Enabled", &host->Sim.PlumesEnabled);
            ImGui::SameLine();
            dirty |= ImGui::Checkbox("Hotspot Drift", &host->Sim.PlumeDriftEnabled);
            dirty |= ImGui::SliderFloat("Strength", &host->Sim.PlumeGlobalStrength, 0.0f, 3.0f, "%.2f");
            dirty |= ImGui::SliderFloat("Swell Scale", &host->Sim.PlumeSwellScale, 0.0f, 3.0f, "%.2f");
            dirty |= ImGui::SliderFloat("Oceanic Erosion", &host->Sim.PlumeOceanicErosionRate, 0.0f, 0.08f, "%.4f");
            dirty |= ImGui::SliderFloat("Accretion Thresh (km)", &host->Sim.PlumeAccretionThresholdKm, 0.0f, 8.0f, "%.2f");

            if (dirty)
            {
                SyncPlumesFromSim(host->Plumes, host->Sim);
            }

            if (ImGui::Button("Preset Plumes"))
            {
                host->Plumes = PresetPlumes(host->Preset, host->Initial.InitialTime, host->Seed);
                SyncSimFromPlumes(host->Sim, host->Plumes);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All"))
            {
                host->Plumes.Plumes.clear();
                SyncSimFromPlumes(host->Sim, host->Plumes);
            }

            if (ImGui::TreeNode("Place New Plume"))
            {
                ImGui::SliderFloat("Lat (deg)", &newPlume.LatDeg, -85.0f, 85.0f, "%.1f");
                ImGui::SliderFloat("Lon (deg)", &newPlume.LonDeg, -180.0f, 180.0f, "%.1f");
                ImGui::SliderFloat("Peak Swell (km)", &newPlume.PeakSwellKm, 0.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("Buoyancy Flux", &newPlume.BuoyancyFlux, 0.5f, 20.0f, "%.1f");
                ImGui::SliderFloat("Uplift Rate", &newPlume.VolcanicUpliftRate, 0.2f, 8.0f, "%.2f");
                ImGui::SliderFloat("Drift (km/My)", &newPlume.DriftSpeedKmPerMy, 0.0f, 60.0f, "%.1f");
                ImGui::SliderFloat("Azimuth (deg)", &newPlume.DriftAzimuthDeg, 0.0f, 360.0f, "%.0f");

                if (ImGui::Button("Add Plume") && host->Plumes.Plumes.size() < k_MaxMantlePlumes)
                {
                    MantlePlumeDesc created = newPlume;
                    created.BirthTimeMy = host->Sim.Time;
                    host->Plumes.Plumes.push_back(created);
                    SyncSimFromPlumes(host->Sim, host->Plumes);
                }

                ImGui::TreePop();
            }

            int32_t removeIndex = -1;
            for (int32_t i = 0; i < static_cast<int32_t>(host->Plumes.Plumes.size()); ++i)
            {
                MantlePlumeDesc& plume = host->Plumes.Plumes[static_cast<size_t>(i)];
                ImGui::PushID(i);
                const std::string label = std::format("Plume {} ({:.1f}, {:.1f})", i, plume.LatDeg, plume.LonDeg);
                if (ImGui::TreeNode(label.c_str()))
                {
                    ImGui::SliderFloat("Lat", &plume.LatDeg, -85.0f, 85.0f, "%.1f");
                    ImGui::SliderFloat("Lon", &plume.LonDeg, -180.0f, 180.0f, "%.1f");
                    ImGui::SliderFloat("Conduit (km)", &plume.ConduitRadiusKm, 25.0f, 350.0f, "%.0f");
                    ImGui::SliderFloat("Swell R (km)", &plume.SwellRadiusKm, 100.0f, 1500.0f, "%.0f");
                    ImGui::SliderFloat("Peak Swell", &plume.PeakSwellKm, 0.0f, 3.5f, "%.2f");
                    ImGui::SliderFloat("Flux", &plume.BuoyancyFlux, 0.0f, 25.0f, "%.1f");
                    ImGui::SliderFloat("Uplift", &plume.VolcanicUpliftRate, 0.0f, 10.0f, "%.2f");
                    ImGui::SliderFloat("Drift km/My", &plume.DriftSpeedKmPerMy, 0.0f, 80.0f, "%.1f");
                    if (ImGui::Button("Remove"))
                    {
                        removeIndex = i;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeIndex >= 0)
            {
                host->Plumes.Plumes.erase(host->Plumes.Plumes.begin() + removeIndex);
                SyncSimFromPlumes(host->Sim, host->Plumes);
            }
        }

        void PlanetUiOverlay::drawShapingSection()
        {
            if (!ImGui::CollapsingHeader("Art Direction & Shaping"))
            {
                return;
            }

            if (ImGui::Button("Legacy"))
            {
                host->Shaping = LegacyShapingState();
            }
            ImGui::SameLine();
            if (ImGui::Button("Earth"))
            {
                host->Shaping = EarthShapingState(static_cast<uint32_t>(host->Seed));
            }
            ImGui::SameLine();
            if (ImGui::Button("Atoll World"))
            {
                host->Shaping = AtollShapingState(static_cast<uint32_t>(host->Seed));
            }

            ImGui::Checkbox("Cortial Slab Dip", &host->Shaping.SlabDipEnabled);
            ImGui::Checkbox("Spherical Coastal Noise", &host->Shaping.CoastalNoiseEnabled);
            ImGui::Checkbox("Atoll Lagoons & Palette", &host->Shaping.AtollShapingEnabled);

            ImGui::SliderFloat("Base Uplift", &host->Shaping.BaseUplift, 0.1f, 2.0f, "%.2f");
            ImGui::SliderFloat("Accretion Thresh", &host->Shaping.AccretionRelief, 0.3f, 3.0f, "%.2f");
            ImGui::SliderFloat("Slab Age Weight", &host->Shaping.SlabDipAgeWeight, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Slab Speed Weight", &host->Shaping.SlabDipSpeedWeight, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Trench Depth (km)", &host->Shaping.TrenchDepthKm, 0.0f, 3.5f, "%.2f");
            ImGui::SliderFloat("Coastal Warp", &host->Shaping.CoastalWarpAmplitude, 0.0f, 0.06f, "%.3f");
            ImGui::SliderFloat("Coastal Detail (km)", &host->Shaping.CoastalDetailKm, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Atoll Lagoon", &host->Shaping.AtollLagoonStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Noise Freq", &host->Shaping.NoiseFrequency, 1.0f, 20.0f, "%.1f");
            ImGui::SliderFloat("Atoll Sea Bias", &host->Shaping.AtollLandBias, -1.5f, 1.5f, "%.2f");
        }

        void PlanetUiOverlay::drawTimingsSection()
        {
            if (!ImGui::CollapsingHeader("GPU Timings"))
            {
                return;
            }

            const TimingsOutput timings = host->GetTimings();
            if (!timings.HasData)
            {
                ImGui::TextDisabled("Waiting for completed GPU timing block...");
                return;
            }

            ImGui::Text("Total: %.1f us | Top-level: %.1f us | Dropped: %llu",
                        timings.TotalUs, timings.TopLevelUs,
                        static_cast<unsigned long long>(timings.DroppedScopes));

            if (ImGui::BeginTable("TimingsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                for (const PassTimingEntry& entry : timings.Passes)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.Name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f us", entry.TimeUs);
                }
                ImGui::EndTable();
            }
        }

        PresentMapParamsHost LiveSessionController::makePresentParams() const noexcept
        {
            float screenAspect = 16.0f / 9.0f;
            bool outputSrgb = false;

            if (ActiveSwapchain != nullptr)
            {
                const math::Float2 extent = ActiveSwapchain->GetExtent();
                if (extent.y > 0.0f)
                {
                    screenAspect = extent.x / extent.y;
                }
                outputSrgb = Is_sRGBFormat(ActiveSwapchain->ImageFormat());
            }

            const float mapAspect = static_cast<float>(std::max(Sim.Width, 1)) /
                                    static_cast<float>(std::max(Sim.Height, 1));

            return PresentMapParamsHost
            {
                .ViewCenter = math::Float2{ Sim.ViewCenterX, Sim.ViewCenterY },
                .ViewZoom = Sim.ViewZoom,
                .MapAspect = mapAspect,
                .ScreenAspect = screenAspect,
                .OutputIsSrgb = outputSrgb ? 1u : 0u,
                .Pad0 = 0u,
                .Pad1 = 0u,
            };
        }

        void LiveSessionController::RecordCapture(loom::RecordContext& ctx)
        {
            if (!PendingCapture.has_value())
            {
                return;
            }

            const PendingCaptureRequest request = *PendingCapture;
            PendingCapture.reset();

            const GraphicsResource mapImage = Graph->ImageOf(k_MapOutput);
            if (!mapImage)
            {
                return;
            }

            const capture::CaptureSource source
            {
                .Image = rhi::ImageHandle{ mapImage.VkHandle },
                .Width = static_cast<uint32_t>(request.Width),
                .Height = static_cast<uint32_t>(request.Height),
                .Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm },
                .Encoding = capture::CaptureEncoding::sRGB,
                .CurrentLayout = rhi::ImageLayout::TransferSource,
                .SrcStages = rhi::PipelineStage::AllTransfer,
                .SrcAccess = rhi::Access::TransferRead,
                .LinearScale = 1.0f,
                .FinalLayout = rhi::ImageLayout::TransferSource,
                .DstStages = rhi::PipelineStage::Copy,
                .DstAccess = rhi::Access::None,
                .Aspect = rhi::ImageAspect::Color,
                .OffsetX = request.X,
                .OffsetY = request.Y,
                .Downsample = request.Downsample,
            };

            LastCaptureTicket = Capture.RecordTrackedCopy(
                ctx.CommandBuffer(), 0u, source, request.Name, capture::CaptureOutput::Png);
        }

        RenderMapView LiveSessionController::MakeRenderMapView() const noexcept
        {
            const RenderProjection projection = ShaderIndexToProjection(Sim.RenderProjection);
            return RenderMapView
            {
                .Mode = static_cast<uint32_t>(std::clamp(Sim.RenderMode, 0, 8)),
                .Projection = projection != RenderProjection::Invalid
                    ? projection
                    : RenderProjection::Equirectangular,
                .CenterLonDeg = Sim.ProjectionCenterLonDeg,
                .CenterLatDeg = Sim.ProjectionCenterLatDeg,
                .CropMinU = Sim.CropMinU,
                .CropMinV = Sim.CropMinV,
                .CropMaxU = Sim.CropMaxU,
                .CropMaxV = Sim.CropMaxV,
            };
        }

        bool LiveSessionController::SupplyGraphResources(const PlanetLiveOptions& opts)
        {
            Imports.RiftEnabled = opts.RiftEnabled;
            Shaping.ErosionEnabled = opts.ErosionEnabled;

            if (!Imports.SupplyAll(*Graph, Resources, Initial, opts.Dt, opts.Order,
                                   opts.Width, opts.Height, MakeRenderMapView(), UseBytes, Plumes, Shaping))
            {
                return false;
            }

            if (LiveWindowed && !Imports.SupplyPresentConstants(*Graph, Resources, makePresentParams()))
            {
                return false;
            }

            return true;
        }

        bool LiveSessionController::UpdateAllUniforms()
        {
            if (StepDtDirty)
            {
                StepDtDirty = false;
                if (!Imports.UpdateStepDt(Resources, Initial, Sim.TimeStepMy, static_cast<uint32_t>(Sim.Order)))
                {
                    return false;
                }
            }

            if (!Imports.UpdateUniforms(Resources,
                                        Sim.Time,
                                        static_cast<uint32_t>(Sim.Width),
                                        static_cast<uint32_t>(Sim.Height),
                                        MakeRenderMapView(),
                                        Plumes,
                                        Shaping))
            {
                return false;
            }

            if (LiveWindowed && !Imports.UpdatePresentConstants(Resources, makePresentParams()))
            {
                return false;
            }

            return true;
        }

        bool LiveSessionController::TickOnce(bool step_simulation, bool& out_submitted)
        {
            out_submitted = false;

            if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                std::println(stderr, "PlanetGenLoom: timed out waiting for frame slot");
                return false;
            }

            Capture.Collect(0u);
            DrainCompletedCaptures();

            if (step_simulation)
            {
                ++Sim.Step;
                Sim.Time = Initial.InitialTime + static_cast<float>(Sim.Step) * Sim.TimeStepMy;
                TriggerSimulationStep(*Graph);

                if (TelemetryEnabled)
                {
                    TriggerTelemetryStep(*Graph);
                }
            }

            if (!UpdateAllUniforms())
            {
                return false;
            }

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

            while (std::chrono::steady_clock::now() < deadline)
            {
                if (Platform != nullptr)
                {
                    Platform->Update();
                    if (Platform->ShouldWindowClose())
                    {
                        return true;
                    }
                }

                const ConsumerTimelineValue pending{ Graph->Timeline(), Graph->PendingValue() };
                Resources.Tick(std::span{ &pending, 1u });

                if (Ui != nullptr)
                {
                    Ui->Begin();
                }

                const loom::TickResult result = Graph->Tick(std::chrono::duration<double>{ 1.0 / 60.0 });

                if (Ui != nullptr)
                {
                    Ui->End();
                }

                switch (result.Status)
                {
                case loom::TickStatus::Submitted:
                    out_submitted = true;
                    return true;
                case loom::TickStatus::NoFrameSlot:
                    if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
                    {
                        std::println(stderr, "PlanetGenLoom: frame slot wait timed out");
                        return false;
                    }
                    break;
                case loom::TickStatus::NothingToRun:
                case loom::TickStatus::TargetNotReady:
                    if (LiveWindowed && !step_simulation && !PendingCapture.has_value())
                    {
                        // Minimized window or settling resize in free-running loop: yield back to caller.
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        return true;
                    }
                    std::this_thread::yield();
                    break;
                case loom::TickStatus::Invalid:
                case loom::TickStatus::Failed:
                    std::println(stderr, "PlanetGenLoom: Tick failed:\n{}", Graph->ExplainLastTick());
                    return false;
                }
            }

            std::println(stderr, "PlanetGenLoom: TickOnce timed out\n{}", Graph->ExplainLastTick());
            return false;
        }

        void LiveSessionController::OnSimChanged(std::string_view field_path)
        {
            if (field_path == "TimeStepMy")
            {
                StepDtDirty = true;
                return;
            }

            if (field_path.starts_with("Plume"))
            {
                SyncPlumesFromSim(Plumes, Sim);
                SyncSimFromPlumes(Sim, Plumes);
            }
        }

        void LiveSessionController::OnPlumesChanged()
        {
            SyncSimFromPlumes(Sim, Plumes);
        }

        void LiveSessionController::OnShapingChanged(std::string_view field_path)
        {
            static_cast<void>(field_path);
            if (!LiveWindowed)
            {
                bool submitted = false;
                static_cast<void>(TickOnce(false, submitted));
                if (submitted)
                {
                    static_cast<void>(Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)));
                }
            }
        }

        std::optional<std::string> LiveSessionController::ApplyShapingPreset(ShapingPreset preset)
        {
            const bool erosionEnabled = Shaping.ErosionEnabled;
            Shaping = ShapingStateForPreset(preset, static_cast<uint32_t>(Seed));
            Shaping.ErosionEnabled = erosionEnabled;
            OnShapingChanged("");
            return std::nullopt;
        }

        std::optional<std::string> LiveSessionController::Step(int32_t steps, float dt)
        {
            if (steps < 0)
            {
                return std::string{ "steps must be non-negative" };
            }

            if (dt > 0.0f && std::fabs(dt - Sim.TimeStepMy) > 1.0e-6f)
            {
                Sim.TimeStepMy = dt;
                StepDtDirty = true;
            }

            for (int32_t i = 0; i < steps; ++i)
            {
                bool submitted = false;
                if (!TickOnce(true, submitted))
                {
                    return std::string{ "simulation step tick failed" };
                }

                if (!submitted && Platform != nullptr && Platform->ShouldWindowClose())
                {
                    return std::string{ "window closed during step" };
                }
            }

            if (steps == 0)
            {
                bool submitted = false;
                if (!TickOnce(false, submitted))
                {
                    return std::string{ "render tick failed" };
                }
            }

            if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                return std::string{ "timed out waiting for step completion" };
            }

            return std::nullopt;
        }

        std::optional<std::string> LiveSessionController::Reset()
        {
            if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                return std::string{ "timed out waiting before reset" };
            }

            Sim.Step = 0;
            Sim.Time = Initial.InitialTime;
            StepDtDirty = false;

            if (!Imports.ResetSimulationState(*Graph, Resources, Initial, Sim.TimeStepMy,
                                              static_cast<uint32_t>(Sim.Order),
                                              static_cast<uint32_t>(Sim.Width),
                                              static_cast<uint32_t>(Sim.Height),
                                              MakeRenderMapView(),
                                              UseBytes, Plumes, Shaping))
            {
                return std::string{ "failed to reset GPU simulation buffers" };
            }

            TriggerInitialLiveView(*Graph);
            bool submitted = false;
            if (!TickOnce(false, submitted) || !Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                return std::string{ "failed to render initial step-0 view after reset" };
            }

            return std::nullopt;
        }

        CaptureOutput LiveSessionController::CaptureArea(int32_t x,
                                                         int32_t y,
                                                         int32_t width,
                                                         int32_t height,
                                                         uint32_t downsample,
                                                         int32_t mode,
                                                         int32_t projection,
                                                         std::string_view name)
        {
            if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                x + width > Sim.Width || y + height > Sim.Height)
            {
                return CaptureOutput{ .Error = "capture region is outside MapOutput bounds" };
            }

            const int32_t savedMode = Sim.RenderMode;
            const int32_t savedProjection = Sim.RenderProjection;
            const bool overrideMode = mode >= 0 && mode <= 8 && mode != savedMode;
            const bool overrideProjection = ShaderIndexToProjection(projection) != RenderProjection::Invalid &&
                                            projection != savedProjection;

            if (overrideMode)
            {
                Sim.RenderMode = mode;
            }

            if (overrideProjection)
            {
                Sim.RenderProjection = projection;
            }

            LastCaptureTicket.reset();
            PendingCapture = PendingCaptureRequest
            {
                .X = x,
                .Y = y,
                .Width = width,
                .Height = height,
                .Downsample = downsample,
                .Name = std::string{ name },
            };
            Graph->Trigger(k_CaptureMapPass);

            bool submitted = false;
            const bool ticked = TickOnce(false, submitted);
            const bool waited = ticked && Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs));

            Capture.Collect(0u);
            Capture.Flush();
            DrainCompletedCaptures();

            if (overrideMode || overrideProjection)
            {
                Sim.RenderMode = savedMode;
                Sim.RenderProjection = savedProjection;
                static_cast<void>(UpdateAllUniforms());
            }

            if (!ticked || !waited || !submitted || !LastCaptureTicket.has_value())
            {
                return CaptureOutput{ .Error = "capture pass failed to record" };
            }

            const uint64_t ticket = *LastCaptureTicket;
            const auto match = std::ranges::find_if(RecentCaptureResults, [ticket](const capture::CaptureResult& r)
            {
                return r.Ticket == ticket;
            });

            if (match == RecentCaptureResults.end() || !match->Succeeded)
            {
                return CaptureOutput{ .Error = "capture writer failed to save PNG" };
            }

            std::error_code ec;
            const std::filesystem::path absPath = std::filesystem::absolute(match->Path, ec);
            return CaptureOutput
            {
                .Succeeded = true,
                .Path = (ec ? match->Path : absPath).string(),
                .Width = static_cast<int32_t>(match->Width),
                .Height = static_cast<int32_t>(match->Height),
            };
        }

        TimingsOutput LiveSessionController::GetTimings()
        {
            const std::optional<rhi::GpuTimingBlock> block = Graph->LatestTimings();
            if (!block.has_value())
            {
                return TimingsOutput{};
            }

            TimingsOutput output
            {
                .HasData = true,
                .TotalUs = 0.0,
                .TopLevelUs = block->TopLevelNanoseconds * 1.0e-3,
                .DroppedScopes = block->DroppedScopes,
                .Passes = {},
            };
            output.Passes.reserve(block->Timings.size());

            for (const rhi::GpuTiming& timing : block->Timings)
            {
                const double us = timing.Nanoseconds * 1.0e-3;
                output.TotalUs += us;
                output.Passes.push_back(PassTimingEntry
                {
                    .Name = std::string{ PassNameForHash(timing.Hash) },
                    .TimeUs = us,
                });
            }

            return output;
        }

        TelemetryOutput LiveSessionController::GetTelemetry()
        {
            if (!TelemetryEnabled)
            {
                return TelemetryOutput
                {
                    .Available = false,
                    .Error = "telemetry is disabled in this session (run with --telemetry)",
                };
            }

            if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                return TelemetryOutput{ .Available = false, .Error = "timed out waiting for frame slot" };
            }

            const GraphicsResource counterBuffer = Graph->BufferOf(k_TelemetryCounters);
            const size_t counterBytes = static_cast<size_t>(k_TelemetryCounterWordCount) * sizeof(uint32_t);
            const std::vector<std::byte> raw = ReadBackBuffer(Resources, *Graph, counterBuffer, counterBytes);

            if (raw.size() != counterBytes)
            {
                return TelemetryOutput{ .Available = false, .Error = "failed to read back TelemetryCounters buffer" };
            }

            std::array<uint32_t, k_TelemetryCounterWordCount> words{};
            std::memcpy(words.data(), raw.data(), counterBytes);
            return BuildTelemetryOutput(words, Initial.PointCount, Initial.PlateCount, Sim.Step, Sim.Time);
        }

        DumpStateOutput LiveSessionController::DumpState(std::string_view name, std::string_view stage)
        {
            // header flag 0 is the end of a step; flag i + 1 is k_DumpStages[i]
            loom::PassKey dumpPass = k_DumpHealpixStatePass;
            uint32_t stageFlag = 0u;
            const bool atEnd = stage == "end";

            if (!atEnd)
            {
                const auto matchesStage = [stage](const DumpStage& candidate)
                {
                    return candidate.Name == stage;
                };
                const auto found = std::ranges::find_if(k_DumpStages, matchesStage);

                if (found == k_DumpStages.end())
                {
                    std::string names{ "end" };

                    for (const DumpStage& candidate : k_DumpStages)
                    {
                        names += std::format(", {}", candidate.Name);
                    }

                    return DumpStateOutput{ .Error = std::format("unknown dump stage '{}': use one of {}", stage, names) };
                }

                dumpPass = found->Pass;
                stageFlag = static_cast<uint32_t>(found - k_DumpStages.begin()) + 1u;
            }

            if (!Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)))
            {
                return DumpStateOutput{ .Error = "timed out waiting before state dump" };
            }

            Graph->Trigger(dumpPass);

            // a stage dump captures a point inside a step, so it runs one step
            bool submitted = false;
            const bool ticked = TickOnce(!atEnd, submitted);
            const bool waited = ticked && Graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs));

            if (!ticked || !waited || !submitted)
            {
                return DumpStateOutput{ .Error = "DumpHealpixStateCS pass failed to execute" };
            }

            const GraphicsResource dumpBuffer = Graph->BufferOf(k_HealpixStateDump);
            const size_t payloadBytes = static_cast<size_t>(Initial.PointCount) *
                                        static_cast<size_t>(k_HealpixDumpWordsPerCell) *
                                        sizeof(uint32_t);
            const std::vector<std::byte> payload = ReadBackBuffer(Resources, *Graph, dumpBuffer, payloadBytes);

            if (payload.size() != payloadBytes)
            {
                return DumpStateOutput{ .Error = "failed to read back HealpixStateDump buffer" };
            }

            std::error_code dirError;
            std::filesystem::create_directories(CaptureDir, dirError);

            if (dirError)
            {
                return DumpStateOutput{ .Error = std::format("failed to create dump directory '{}'", CaptureDir.string()) };
            }

            std::array<uint8_t, 32> seenPlates{};
            const uint32_t strideBytes = k_HealpixDumpWordsPerCell * static_cast<uint32_t>(sizeof(uint32_t));

            for (uint32_t pt = 0u; pt < Initial.PointCount; ++pt)
            {
                const uint8_t plateByte = std::to_integer<uint8_t>(payload[static_cast<size_t>(pt) * strideBytes]);

                if (plateByte < Initial.PlateCount && plateByte < 32u)
                {
                    seenPlates[plateByte] = 1u;
                }
            }

            uint32_t activePlates = 0u;

            for (const uint8_t seen : seenPlates)
            {
                activePlates += seen;
            }

            const HealpixDumpHeader header
            {
                .Magic = { 'P', 'G', 'H', 'P' },
                .Version = 1u,
                .HealpixOrder = static_cast<uint32_t>(Sim.Order),
                .PointCount = Initial.PointCount,
                .PlateSlotCount = Initial.PlateCount,
                .Step = Sim.Step,
                .TimeMy = Sim.Time,
                .RecordStrideBytes = strideBytes,
                .ActivePlateCount = activePlates,
                .Flags = stageFlag,
            };

            std::vector<std::byte> fileBytes(sizeof(HealpixDumpHeader) + payloadBytes);
            std::memcpy(fileBytes.data(), &header, sizeof(HealpixDumpHeader));
            std::memcpy(fileBytes.data() + sizeof(HealpixDumpHeader), payload.data(), payloadBytes);

            const std::filesystem::path outputPath = CaptureDir / std::format("{}.pghp", name);
            std::ofstream file{ outputPath, std::ios::binary | std::ios::trunc };

            if (!file)
            {
                return DumpStateOutput{ .Error = std::format("failed to open '{}' for writing", outputPath.string()) };
            }

            file.write(reinterpret_cast<const char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));

            if (!file.good())
            {
                return DumpStateOutput{ .Error = std::format("failed to write '{}'", outputPath.string()) };
            }

            std::error_code absError;
            const std::filesystem::path absPath = std::filesystem::absolute(outputPath, absError);
            return DumpStateOutput
            {
                .Succeeded = true,
                .Path = (absError ? outputPath : absPath).string(),
                .Bytes = static_cast<uint64_t>(fileBytes.size()),
                .Order = Sim.Order,
                .PointCount = Initial.PointCount,
                .PlateSlotCount = Initial.PlateCount,
                .ActivePlateCount = activePlates,
                .Step = Sim.Step,
                .Time = Sim.Time,
                .Stage = std::string{ stage },
            };
        }

        void LiveSessionController::DrainCompletedCaptures()
        {
            std::vector<capture::CaptureResult> latest = Capture.TakeResults();
            if (!latest.empty())
            {
                RecentCaptureResults.insert(RecentCaptureResults.end(),
                                            std::make_move_iterator(latest.begin()),
                                            std::make_move_iterator(latest.end()));
                if (RecentCaptureResults.size() > 32u)
                {
                    RecentCaptureResults.erase(
                        RecentCaptureResults.begin(),
                        RecentCaptureResults.begin() + static_cast<std::ptrdiff_t>(RecentCaptureResults.size() - 32u));
                }
            }
        }

        void LiveSessionController::Shutdown()
        {
            Capture.Flush();
        }

        [[nodiscard]] PlanetToolHost MakeToolHost(LiveSessionController& controller)
        {
            return PlanetToolHost
            {
                .Sim = controller.Sim,
                .Plumes = controller.Plumes,
                .Shaping = controller.Shaping,
                .OnSimChanged = [&controller](std::string_view field_path)
                {
                    controller.OnSimChanged(field_path);
                },
                .OnPlumesChanged = [&controller]()
                {
                    controller.OnPlumesChanged();
                },
                .OnShapingChanged = [&controller](std::string_view field_path)
                {
                    controller.OnShapingChanged(field_path);
                },
                .ApplyShapingPreset = [&controller](ShapingPreset preset)
                {
                    return controller.ApplyShapingPreset(preset);
                },
                .Step = [&controller](int32_t steps, float dt)
                {
                    return controller.Step(steps, dt);
                },
                .Reset = [&controller]()
                {
                    return controller.Reset();
                },
                .CaptureArea = [&controller](int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             int32_t height,
                                             uint32_t downsample,
                                             int32_t mode,
                                             int32_t projection,
                                             std::string_view name)
                {
                    return controller.CaptureArea(x, y, width, height, downsample, mode, projection, name);
                },
                .GetTimings = [&controller]()
                {
                    return controller.GetTimings();
                },
                .GetTelemetry = [&controller]()
                {
                    return controller.GetTelemetry();
                },
                .DumpState = [&controller](std::string_view name, std::string_view stage)
                {
                    return controller.DumpState(name, stage);
                },
            };
        }

        std::vector<std::byte> ReadBackBuffer(ResourceContext& resources,
                                              const loom::Graph& graph,
                                              GraphicsResource source,
                                              size_t bytes)
        {
            if (!source || bytes == 0u)
            {
                return {};
            }

            BufferCreateInfo readbackInfo{};
            readbackInfo.Domain = ResourceDomain::GPUToCPU;
            readbackInfo.Usage = BufferUsageBits::TransferDestination;
            readbackInfo.Size = bytes;
            readbackInfo.Name = "PlanetLiveApp.Readback";
            rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

            if (resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                return {};
            }

            const GraphicsResource readback = readbackReply.Get();
            const rhi::GpuWait last = graph.LastSubmit();
            const rhi::TransferWait afterGraph{ last.Semaphore, last.Value };
            rhi::OperationFuture copyReply = last.Submitted
                ? resources.CopyBufferContents(source, readback, std::span{ &afterGraph, 1u })
                : resources.CopyBufferContents(source, readback, {});
            rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readback, bytes, 0u);
            std::vector<std::byte> result;

            if (resources.Wait(copyReply, k_TimeoutNs) == rhi::FutureState::Complete &&
                resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete)
            {
                const auto* mapped = static_cast<const std::byte*>(mapReply.Get());
                result.assign(mapped, mapped + bytes);
            }

            rhi::OperationFuture unmapReply = resources.UnmapBuffer(readback, bytes, 0u);
            rhi::OperationFuture destroyReply = resources.DestroyResource(readback);
            [[maybe_unused]] const rhi::FutureState unmapped = resources.Wait(unmapReply, k_TimeoutNs);
            [[maybe_unused]] const rhi::FutureState destroyed = resources.Wait(destroyReply, k_TimeoutNs);
            return result;
        }

        std::string_view RiftOutcomeName(uint32_t outcome) noexcept
        {
            switch (outcome)
            {
            case 0u:
                return "none";
            case 1u:
                return "completed";
            case 2u:
                return "no_free_slot";
            case 3u:
                return "not_warm";
            case 4u:
                return "too_small";
            case 5u:
                return "pick_failed";
            case 6u:
                return "trial_skipped";
            default:
                return "unknown";
            }
        }

        [[nodiscard]] float EstimateHistogramQuantileKm(std::span<const uint32_t> bins,
                                                        uint32_t total_count,
                                                        float quantile) noexcept
        {
            if (total_count == 0u)
            {
                return 0.0f;
            }

            const double target = static_cast<double>(quantile) * static_cast<double>(total_count);
            uint32_t cumulative = 0u;

            for (size_t bin = 0u; bin < bins.size(); ++bin)
            {
                const uint32_t next = cumulative + bins[bin];

                if (static_cast<double>(next) >= target)
                {
                    const double within = bins[bin] > 0u
                        ? (target - static_cast<double>(cumulative)) / static_cast<double>(bins[bin])
                        : 0.0;
                    return static_cast<float>((static_cast<double>(bin) + std::clamp(within, 0.0, 1.0)) * 0.5);
                }

                cumulative = next;
            }

            return 8.0f;
        }

        [[nodiscard]] uint64_t CombineUint64(uint32_t lo, uint32_t hi) noexcept
        {
            return (static_cast<uint64_t>(hi) << 32u) | static_cast<uint64_t>(lo);
        }

        TelemetryOutput BuildTelemetryOutput(std::span<const uint32_t> words,
                                             uint32_t point_count,
                                             uint32_t plate_count,
                                             int32_t step,
                                             float time_my)
        {
            namespace ts = telemetry_slots;
            constexpr double k_PlanetRadiusKm = 6370.0;
            const double sphereAreaKm2 = 4.0 * std::numbers::pi * k_PlanetRadiusKm * k_PlanetRadiusKm;
            const double cellAreaKm2 = point_count > 0u ? (sphereAreaKm2 / static_cast<double>(point_count)) : 0.0;
            const double meterHeightToKm3 = 0.001 * cellAreaKm2;

            TelemetryOutput out{};
            out.Available = true;
            out.Step = step;
            out.Time = time_my;
            out.StepsObserved = static_cast<int32_t>(words[ts::k_StepsObserved]);
            out.CollisionsTriggered = words[ts::k_CollisionsTriggered];
            out.TerraneCellsTransferred = words[ts::k_TerraneCellsTransferred];
            out.OceanCellsBorn = words[ts::k_OceanCellsBorn];
            out.SubductionOverlapPoints = words[ts::k_SubductionOverlapPoints];
            out.SnapOverflowCells = words[ts::k_SnapOverflowCells];
            out.UpliftOceanToContFlips = words[ts::k_UpliftOceanToContFlips];
            out.UpliftVolcanicToContFlips = words[ts::k_UpliftVolcanicToContFlips];
            out.UpliftVolcanicOverwritten = words[ts::k_UpliftVolcanicOverwritten];
            out.PlumeOceanToContFlips = words[ts::k_PlumeOceanToContFlips];
            out.PlumeVolcanicCellsActive = words[ts::k_PlumeVolcanicCellsActive];
            out.RiftsCompleted = words[ts::k_RiftsCompleted];
            out.RiftsBlockedNoFreeSlot = words[ts::k_RiftsBlockedNoFreeSlot];
            out.RiftsBlockedNotWarm = words[ts::k_RiftsBlockedNotWarm];
            out.RiftsBlockedTooSmall = words[ts::k_RiftsBlockedTooSmall];
            out.RiftsBlockedPickFailed = words[ts::k_RiftsBlockedPickFailed];
            out.LastRiftOutcome = std::string{ RiftOutcomeName(words[ts::k_LastStepRiftOutcome]) };

            out.UpliftVolumeAddedKm3 = static_cast<double>(CombineUint64(
                words[ts::k_UpliftHeightAddedMetersLo], words[ts::k_UpliftHeightAddedMetersHi])) * meterHeightToKm3;
            out.UpliftLinearErosionKm3 = static_cast<double>(CombineUint64(
                words[ts::k_UpliftLinearErosionMetersLo], words[ts::k_UpliftLinearErosionMetersHi])) * meterHeightToKm3;
            out.PlumeVolumeAddedKm3 = static_cast<double>(CombineUint64(
                words[ts::k_PlumeHeightAddedMetersLo], words[ts::k_PlumeHeightAddedMetersHi])) * meterHeightToKm3;
            out.PlumeOceanicErosionKm3 = static_cast<double>(CombineUint64(
                words[ts::k_PlumeOceanicErosionMetersLo], words[ts::k_PlumeOceanicErosionMetersHi])) * meterHeightToKm3;
            out.ErosionDiffusionAddedKm3 = static_cast<double>(CombineUint64(
                words[ts::k_ErosionDiffusionAddedMetersLo], words[ts::k_ErosionDiffusionAddedMetersHi])) * meterHeightToKm3;
            out.ErosionRemovedKm3 = static_cast<double>(CombineUint64(
                words[ts::k_ErosionRemovedMetersLo], words[ts::k_ErosionRemovedMetersHi])) * meterHeightToKm3;
            out.CollapseCellsActive = words[ts::k_CollapseCellsActive];

            out.PlateFrameLiveCells = words[ts::k_PlateFrameLiveCells];
            out.WorldCells = words[ts::k_WorldCells];
            out.ContinentalCells = words[ts::k_ContinentalCells];
            out.OceanicCells = words[ts::k_OceanicCells];
            out.VolcanicOceanicCells = words[ts::k_VolcanicOceanicCells];
            out.AndeanCells = words[ts::k_AndeanCells];
            out.HimalayanCells = words[ts::k_HimalayanCells];
            out.IslandArcCells = words[ts::k_IslandArcCells];
            out.SubductionFrontCells = words[ts::k_SubductionFrontCells];
            out.CollisionFrontCells = words[ts::k_CollisionFrontCells];

            const uint32_t youngOceanCells = words[ts::k_YoungOceanCellsUnder5My];
            const uint64_t oceanAgeTenths = (static_cast<uint64_t>(words[ts::k_OceanAgeSumTenthsMyHi]) << 32u) |
                                            words[ts::k_OceanAgeSumTenthsMy];
            const uint32_t landBelow05Cells = words[ts::k_LandBelow05KmCells];
            const uint32_t landAbove5Cells = words[ts::k_LandAbove5KmCells];
            out.ReliefMaxKm = static_cast<float>(words[ts::k_ReliefMaxMm]) * 0.001f;

            const uint32_t ownedWorldCells = out.ContinentalCells + out.OceanicCells;

            if (ownedWorldCells > 0u)
            {
                out.LandFraction = static_cast<float>(out.ContinentalCells) / static_cast<float>(ownedWorldCells);
            }

            if (out.OceanicCells > 0u)
            {
                out.OceanAgeMeanMy = static_cast<float>(static_cast<double>(oceanAgeTenths) * 0.1 /
                                                        static_cast<double>(out.OceanicCells));
                out.YoungRidgeFracUnder5My = static_cast<float>(youngOceanCells) / static_cast<float>(out.OceanicCells);
            }

            if (out.ContinentalCells > 0u)
            {
                out.LandBelow05KmFrac = static_cast<float>(landBelow05Cells) / static_cast<float>(out.ContinentalCells);
                out.LandAbove5KmFrac = static_cast<float>(landAbove5Cells) / static_cast<float>(out.ContinentalCells);
                const std::span<const uint32_t> hist{ words.data() + ts::k_ReliefHistBase, ts::k_ReliefHistBins };
                out.ReliefP50Km = EstimateHistogramQuantileKm(hist, out.ContinentalCells, 0.50f);
                out.ReliefP95Km = EstimateHistogramQuantileKm(hist, out.ContinentalCells, 0.95f);
                out.ReliefP99Km = EstimateHistogramQuantileKm(hist, out.ContinentalCells, 0.99f);
            }

            const uint32_t clampedPlates = std::min(plate_count, 32u);
            out.PlateCellCounts.resize(clampedPlates, 0);
            out.PlateContinentalCounts.resize(clampedPlates, 0);

            double totalCells = 0.0;

            for (uint32_t p = 0u; p < clampedPlates; ++p)
            {
                const uint32_t cells = words[ts::k_PlateCellCountBase + p];
                const uint32_t cont = words[ts::k_PlateContCountBase + p];
                out.PlateCellCounts[p] = static_cast<int32_t>(cells);
                out.PlateContinentalCounts[p] = static_cast<int32_t>(cont);
                totalCells += static_cast<double>(cells);

                if (cells == 0u && cont == 0u)
                {
                    ++out.FreeRiftSlots;
                }
                else
                {
                    ++out.ActivePlates;
                }
            }

            if (totalCells > 0.0)
            {
                double hhi = 0.0;
                double maxShare = 0.0;

                for (uint32_t p = 0u; p < clampedPlates; ++p)
                {
                    const double share = static_cast<double>(out.PlateCellCounts[p]) / totalCells;

                    if (share > 0.0)
                    {
                        hhi += share * share;
                        maxShare = std::max(maxShare, share);

                        if (share < 0.01)
                        {
                            ++out.TinyPlatesUnder1Pct;
                        }
                    }
                }

                out.LargestPlateShare = static_cast<float>(maxShare);
                out.Hhi = static_cast<float>(hhi);
                out.EffectivePlateCount = hhi > 1.0e-9 ? static_cast<float>(1.0 / hhi) : 0.0f;
            }

            bool hasFail = false;
            bool hasWarn = false;

            if (out.SnapOverflowCells > 0u)
            {
                hasFail = true;
                out.Flags.push_back(std::format("FAIL:telemetry_snap_overflow={}", out.SnapOverflowCells));
            }

            if (out.UpliftVolcanicToContFlips > 0u)
            {
                hasFail = true;
                out.Flags.push_back(std::format("FAIL:volcanic_to_cont_flips={}", out.UpliftVolcanicToContFlips));
            }

            if (out.UpliftVolcanicOverwritten > 0u)
            {
                hasFail = true;
                out.Flags.push_back(std::format("FAIL:volcanic_orogeny_overwritten={}", out.UpliftVolcanicOverwritten));
            }

            if (out.StepsObserved > 0 && out.OceanicCells > 0u && out.YoungRidgeFracUnder5My <= 0.0f)
            {
                hasFail = true;
                out.Flags.push_back("FAIL:young_ridge_frac_under_5my=0");
            }

            if (out.ReliefP99Km > 7.5f)
            {
                hasFail = true;
                out.Flags.push_back(std::format("FAIL:relief_p99_km={:.2f}>7.5", out.ReliefP99Km));
            }
            else if (out.ReliefP99Km > 6.5f)
            {
                hasWarn = true;
                out.Flags.push_back(std::format("WARN:relief_p99_km={:.2f}>6.5", out.ReliefP99Km));
            }

            if (out.StepsObserved > 0 && out.LargestPlateShare > 0.35f)
            {
                hasWarn = true;
                out.Flags.push_back(std::format("WARN:largest_plate_share={:.3f}>0.35", out.LargestPlateShare));
            }

            if (out.StepsObserved > 0 && out.EffectivePlateCount > 0.0f && out.EffectivePlateCount < 5.0f)
            {
                hasWarn = true;
                out.Flags.push_back(std::format("WARN:N_eff={:.2f}<5.0", out.EffectivePlateCount));
            }

            if (out.RiftsBlockedNoFreeSlot > 0u)
            {
                hasWarn = true;
                out.Flags.push_back(std::format("WARN:rifts_blocked_no_free_slot={}", out.RiftsBlockedNoFreeSlot));
            }

            out.Verdict = hasFail ? "FAIL" : (hasWarn ? "WARN" : "PASS");
            return out;
        }

        std::string FormatDiagnosticValue(const loom::DiagnosticValue& value)
        {
            switch (value.Kind)
            {
            case loom::DiagnosticValueKind::None:
                return "-";
            case loom::DiagnosticValueKind::Text:
                return std::format("{}:{}", value.FieldName, value.Text);
            case loom::DiagnosticValueKind::Integer:
                return std::format("{}:{}", value.FieldName, value.Integer);
            case loom::DiagnosticValueKind::Real:
                return std::format("{}:{}", value.FieldName, value.Real);
            }

            return "-";
        }

        void PrintLiveDiagnostics(const loom::CollectingSink& sink)
        {
            for (const loom::Diagnostic& diagnostic : sink.Diagnostics())
            {
                std::println(stderr,
                             "{}({}): {} {}: {} [pass='{}', resource='{}', binding='{}', expected={}, actual={}]",
                             diagnostic.Where.file_name(),
                             diagnostic.Where.line(),
                             loom::ToString(diagnostic.Code),
                             loom::ToString(diagnostic.Severity),
                             loom::SummaryOf(diagnostic.Code),
                             diagnostic.Pass.Name,
                             diagnostic.Resource.Name,
                             diagnostic.Binding,
                             FormatDiagnosticValue(diagnostic.Expected),
                             FormatDiagnosticValue(diagnostic.Actual));
            }
        }

        [[nodiscard]] bool WaitForGraphReady(loom::Graph& graph, ResourceContext& resources)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

            while (!graph.Ready() && std::chrono::steady_clock::now() < deadline)
            {
                const ConsumerTimelineValue pending{ graph.Timeline(), graph.PendingValue() };
                resources.Tick(std::span{ &pending, 1u });
                std::this_thread::yield();
            }

            if (!graph.Ready())
            {
                std::println(stderr, "PlanetGenLoom: graph resources/pipelines did not become ready before first step");
                return false;
            }

            return true;
        }

        std::vector<bootstrap::Need> BuildDeviceNeeds(bool timestamps)
        {
            std::vector<bootstrap::Need> needs;
            needs.append_range(bootstrap::ShaderObjectNeeds());

            if (timestamps)
            {
                needs.append_range(bootstrap::QueryPoolNeeds());
            }

            return needs;
        }
    }

    std::string_view PassNameForHash(uint64_t hash) noexcept
    {
        for (const loom::PassKey& key : AllPlanetPassKeys())
        {
            if (rhi::HashGpuTimerName(key.Name) == hash)
            {
                return key.Name;
            }
        }

        return "<unknown>";
    }

    int RunLivePlanetApp(std::span<char* const> arguments,
                         const PlanetLiveOptions& options,
                         const planet::HealpixInitialState& initial)
    {
        const PlatformWindowCreateInfo windowInfo
        {
            "DiamondDogs - PlanetGen (Loom)",
            nullptr,
            PlatformWindowMode::Windowed,
            1600,
            900,
            80,
            80,
            60.0f,
            PlatformWindowBehaviorFlags{ true, true, true, false, false }
        };

        const bootstrap::DeviceConfig deviceConfig
        {
            .ApiVersion = rhi::ApiVersion::Latest,
            .Validation = options.ValidationFull ? rhi::ValidationLayers::Full : rhi::ValidationLayers::None,
            .ApplicationName = "PlanetGenLoom.Live",
        };

        WindowedBoot boot;
        const std::vector<bootstrap::Need> needs = BuildDeviceNeeds(options.Timestamps);

        if (!StartWindowedBoot(boot, arguments, k_Bundles, needs, deviceConfig, windowInfo))
        {
            return 1;
        }

        PlatformWindowSystem& platform = boot.Window.Platform();
        rhi::RhiSystem& rhiSystem = boot.Device.Rhi();
        const rhi::Device& device = *rhiSystem.GetDevice();
        ResourceContext& resources = boot.Gpu.Resources();
        Swapchain& swapchain = *platform.GetActiveSwapchain();

        const ImGuiModuleCreateInfo imguiInfo
        {
            .PlatformWindowHandle = platform.GetWindowHandle(),
            .Instance = rhiSystem.GetInstance(),
            .Device = &device,
            .VulkanApiVersion = rhiSystem.GetVulkanApiVersion(),
            .MinImageCount = swapchain.ImageCount(),
            .ImageCount = swapchain.ImageCount(),
            .ColorFormat = swapchain.ImageFormat(),
            .InstallInputCallbacks = true,
            .FontDirectory = PLANET_GEN_FONT_DIR,
        };
        ImGuiModule imgui{ imguiInfo };
        PlanetUiOverlay ui{ imgui };

        const std::filesystem::path captureDir = options.CaptureDir.empty()
            ? std::filesystem::path{ "build/planet_captures" }
            : options.CaptureDir;
        capture::FrameCapture frameCapture{ resources, 1u, captureDir, 1u };
        LiveSessionController controller{ resources, device, initial, frameCapture, captureDir, &platform, &swapchain, options };
        ui.AttachController(&controller);

        ZeroCountersRecorder zeroRecorder;
        const PlanetGraphConfig graphConfig
        {
            .HealpixOrder = options.Order,
            .PlateCount = initial.PlateCount,
            .MapWidth = options.Width,
            .MapHeight = options.Height,
            .UseByteCrustFlags = !options.Words,
            .RenderOnTrigger = false,
            .InteractiveControl = true,
            .LiveMode = true,
            .EnableCapturePass = true,
            .EnableTelemetry = options.Telemetry,
            .EnableStateDump = true,
        };
        const PlanetGraphCallbacks callbacks
        {
            .ZeroCounters = &zeroRecorder,
            .CaptureCallback = Delegate<void(loom::RecordContext&)>::Create<LiveSessionController, &LiveSessionController::RecordCapture>(&controller),
            .UiCallback = Delegate<void(loom::RecordContext&)>::Create<PlanetUiOverlay, &PlanetUiOverlay::Record>(&ui),
        };

        loom::GraphBuilder builder{ "PlanetGen.LiveGraph", "PlanetLab" };
        RegisterPlanetGraph(builder, graphConfig, callbacks);

        loom::CollectingSink sink;
        const bootstrap::DeviceFilter filter = boot.Device.Filter();
        const loom::CompileInputs compileInputs
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Supports = filter.Supports,
            .SupportsUser = filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(device),
            .Limits = loom::LayoutLimitsOf(device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, compileInputs, sink);

        if (!compiled.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Compile failed");
            PrintLiveDiagnostics(sink);
            return 1;
        }

        loom::SwapchainFrameTarget target{ device, swapchain };
        const loom::FrameTargetBinding binding{ .Key = k_Screen, .Target = target };
        const loom::RealizeInputs realizeInputs
        {
            .Resources = resources,
            .Device = device,
            .FrameSlots = 1,
            .Timestamps = options.Timestamps ? loom::PassTimestamps::On : loom::PassTimestamps::Off,
            .Aliasing = loom::MemoryAliasing::Off,
            .Sink = sink,
            .FrameTargets = std::span{ &binding, 1u },
            .TargetSettleSeconds = k_SettleSeconds,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Realize failed: {}", loom::ToString(realized.error()));
            PrintLiveDiagnostics(sink);
            return 1;
        }

        std::unique_ptr<loom::Graph> graph = std::move(*realized);
        controller.AttachGraph(*graph, &ui);

        if (!controller.SupplyGraphResources(options) || !WaitForGraphReady(*graph, resources))
        {
            graph.reset();
            controller.Imports.Destroy(resources);
            return 1;
        }

        PlanetToolHost toolHost = MakeToolHost(controller);
        std::unique_ptr<PlanetLoomToolServer> toolServer;

        if (options.ToolPort >= 0)
        {
            toolServer = std::make_unique<PlanetLoomToolServer>(toolHost);
            if (!toolServer->Start(options.ToolPort))
            {
                graph.reset();
                controller.Imports.Destroy(resources);
                return 1;
            }
        }

        // Render step 0 cleanly so MapOutput displays initial plate geometry & plume swell immediately.
        TriggerInitialLiveView(*graph);
        bool ok = true;
        int32_t submittedFrames = 0;

        {
            bool initialSubmitted = false;
            ok = controller.TickOnce(false, initialSubmitted);
            if (initialSubmitted)
            {
                ++submittedFrames;
            }
        }

        while (ok &&
               !platform.ShouldWindowClose() &&
               (options.FrameLimit <= 0 || submittedFrames < options.FrameLimit) &&
               (toolServer == nullptr || !toolServer->QuitRequested()))
        {
            platform.Update();

            if (platform.ShouldWindowClose())
            {
                break;
            }

            if (toolServer != nullptr)
            {
                toolServer->Pump();
                if (toolServer->QuitRequested())
                {
                    break;
                }
            }

            if (ui.PendingReset)
            {
                ui.PendingReset = false;
                if (const std::optional<std::string> err = controller.Reset(); err.has_value())
                {
                    std::println(stderr, "PlanetGenLoom: reset error: {}", *err);
                    ok = false;
                    break;
                }
            }

            if (ui.PendingSteps > 0)
            {
                const int32_t batch = ui.PendingSteps;
                ui.PendingSteps = 0;
                if (const std::optional<std::string> err = controller.Step(batch, 0.0f); err.has_value())
                {
                    std::println(stderr, "PlanetGenLoom: step error: {}", *err);
                    ok = false;
                    break;
                }
                submittedFrames += batch;
            }

            if (ui.PendingCapture)
            {
                ui.PendingCapture = false;
                const std::string name = std::format("live_step{}_mode{}", controller.Sim.Step, controller.Sim.RenderMode);
                const CaptureOutput cap = controller.CaptureArea(
                    0, 0, controller.Sim.Width, controller.Sim.Height, 1u, -1, -1, name);
                ui.LastCaptureMessage = cap.Succeeded ? std::format("Saved {}", cap.Path) : cap.Error;
            }

            bool frameSubmitted = false;
            ok = controller.TickOnce(!controller.Sim.Paused, frameSubmitted);
            if (frameSubmitted)
            {
                ++submittedFrames;
            }
        }

        if (toolServer != nullptr)
        {
            toolServer->Stop();
        }

        controller.Shutdown();
        if (graph != nullptr)
        {
            static_cast<void>(graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs)));
        }

        if (options.Explain && graph != nullptr)
        {
            std::println("=== ExplainLastTick ===\n{}", graph->ExplainLastTick());
        }

        if (sink.ErrorCount() != 0)
        {
            PrintLiveDiagnostics(sink);
            ok = false;
        }

        graph.reset();
        controller.Imports.Destroy(resources);
        return ok ? 0 : 1;
    }

    int RunHeadlessToolApp(std::span<char* const> arguments,
                           const PlanetLiveOptions& options,
                           const planet::HealpixInitialState& initial)
    {
        const bootstrap::DeviceConfig deviceConfig
        {
            .ApiVersion = rhi::ApiVersion::Latest,
            .Validation = options.ValidationFull ? rhi::ValidationLayers::Full : rhi::ValidationLayers::None,
            .ApplicationName = "PlanetGenLoom.ToolServer",
        };

        HeadlessBoot boot;
        const std::vector<bootstrap::Need> needs = BuildDeviceNeeds(options.Timestamps);

        if (!StartHeadlessBoot(boot, arguments, k_Bundles, needs, deviceConfig))
        {
            return 1;
        }

        ResourceContext& resources = boot.Gpu.Resources();
        const rhi::Device& device = *boot.Device.Rhi().GetDevice();

        const std::filesystem::path captureDir = options.CaptureDir.empty()
            ? std::filesystem::path{ "build/planet_captures" }
            : options.CaptureDir;
        capture::FrameCapture frameCapture{ resources, 1u, captureDir, 1u };
        LiveSessionController controller{ resources, device, initial, frameCapture, captureDir, nullptr, nullptr, options };

        ZeroCountersRecorder zeroRecorder;
        const PlanetGraphConfig graphConfig
        {
            .HealpixOrder = options.Order,
            .PlateCount = initial.PlateCount,
            .MapWidth = options.Width,
            .MapHeight = options.Height,
            .UseByteCrustFlags = !options.Words,
            .RenderOnTrigger = false,
            .InteractiveControl = true,
            .LiveMode = false,
            .EnableCapturePass = true,
            .EnableTelemetry = options.Telemetry,
            .EnableStateDump = true,
        };
        const PlanetGraphCallbacks callbacks
        {
            .ZeroCounters = &zeroRecorder,
            .CaptureCallback = Delegate<void(loom::RecordContext&)>::Create<LiveSessionController, &LiveSessionController::RecordCapture>(&controller),
            .UiCallback = {},
        };

        loom::GraphBuilder builder{ "PlanetGen.ToolGraph", "PlanetLab" };
        RegisterPlanetGraph(builder, graphConfig, callbacks);

        loom::CollectingSink sink;
        const bootstrap::DeviceFilter filter = boot.Device.Filter();
        const loom::CompileInputs compileInputs
        {
            .Bundle = boot.Content.Bundles().front().Bundle,
            .Supports = filter.Supports,
            .SupportsUser = filter.User,
            .ShaderLimits = loom::ShaderLimitsOf(device),
            .Limits = loom::LayoutLimitsOf(device),
        };

        std::optional<loom::CompiledGraph> compiled = loom::Compile(builder, compileInputs, sink);

        if (!compiled.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Compile failed");
            PrintLiveDiagnostics(sink);
            return 1;
        }

        const loom::RealizeInputs realizeInputs
        {
            .Resources = resources,
            .Device = device,
            .FrameSlots = 1,
            .Timestamps = options.Timestamps ? loom::PassTimestamps::On : loom::PassTimestamps::Off,
            .Aliasing = loom::MemoryAliasing::Off,
            .Sink = sink,
            .FrameTargets = {},
            .TargetSettleSeconds = 0.0,
        };

        loom::RealizeResult realized = loom::Graph::Realize(std::move(*compiled), realizeInputs);

        if (!realized.has_value())
        {
            std::println(stderr, "PlanetGenLoom: Realize failed: {}", loom::ToString(realized.error()));
            PrintLiveDiagnostics(sink);
            return 1;
        }

        std::unique_ptr<loom::Graph> graph = std::move(*realized);
        controller.AttachGraph(*graph, nullptr);

        if (!controller.SupplyGraphResources(options) || !WaitForGraphReady(*graph, resources))
        {
            graph.reset();
            controller.Imports.Destroy(resources);
            return 1;
        }

        // Populate initial step-0 MapOutput cleanly.
        TriggerInitialLiveView(*graph);
        bool ok = true;
        bool initialSubmitted = false;
        ok = controller.TickOnce(false, initialSubmitted) &&
             graph->WaitForFrameSlot(std::chrono::nanoseconds(k_TimeoutNs));

        PlanetToolHost toolHost = MakeToolHost(controller);
        PlanetLoomToolServer toolServer{ toolHost };

        if (!ok || !toolServer.Start(options.ToolPort))
        {
            graph.reset();
            controller.Imports.Destroy(resources);
            return 1;
        }

        while (!toolServer.QuitRequested())
        {
            toolServer.Pump();

            if (!toolServer.QuitRequested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        toolServer.Stop();
        controller.Shutdown();

        if (sink.ErrorCount() != 0)
        {
            PrintLiveDiagnostics(sink);
            ok = false;
        }

        graph.reset();
        controller.Imports.Destroy(resources);
        return ok ? 0 : 1;
    }
}
