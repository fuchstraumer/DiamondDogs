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
#include <filesystem>
#include <format>
#include <memory>
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

        [[nodiscard]] std::string FormatDiagnosticValue(const loom::DiagnosticValue& value);
        void PrintLiveDiagnostics(const loom::CollectingSink& sink);
        [[nodiscard]] std::vector<bootstrap::Need> BuildDeviceNeeds(bool timestamps);

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
                                  PlatformWindowSystem* window_platform,
                                  const Swapchain* swapchain,
                                  const PlanetLiveOptions& opts)
                : Resources{ resources }
                , Device{ device }
                , Initial{ initial }
                , Capture{ frame_capture }
                , Platform{ window_platform }
                , ActiveSwapchain{ swapchain }
                , Preset{ opts.Preset }
                , Seed{ opts.Seed }
                , UseBytes{ !opts.Words }
                , LiveWindowed{ window_platform != nullptr }
                , Plumes{ opts.PlumesEnabled ? PresetPlumes(opts.Preset, initial.InitialTime, opts.Seed) : EmptyPlumeSystem() }
                , Shaping{ ShapingStateForPreset(opts.Shaping, static_cast<uint32_t>(opts.Seed)) }
                , Sim{
                    .Paused = opts.StartPaused,
                    .TimeStepMy = opts.Dt,
                    .RenderMode = static_cast<int32_t>(opts.Mode),
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
                                                      std::string_view name);
            [[nodiscard]] TimingsOutput GetTimings();
            void DrainCompletedCaptures();
            void Shutdown();

            ResourceContext& Resources;
            const rhi::Device& Device;
            const planet::HealpixInitialState& Initial;
            capture::FrameCapture& Capture;
            PlatformWindowSystem* Platform{ nullptr };
            const Swapchain* ActiveSwapchain{ nullptr };
            PlanetPreset Preset{ PlanetPreset::Reference };
            uint64_t Seed{ 42ull };
            bool UseBytes{ true };
            bool LiveWindowed{ false };
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

        bool LiveSessionController::SupplyGraphResources(const PlanetLiveOptions& opts)
        {
            Imports.RiftEnabled = opts.RiftEnabled;

            if (!Imports.SupplyAll(*Graph, Resources, Initial, opts.Dt, opts.Order,
                                   opts.Width, opts.Height, opts.Mode, UseBytes, Plumes, Shaping))
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
                                        static_cast<uint32_t>(Sim.RenderMode),
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
            Shaping = ShapingStateForPreset(preset, static_cast<uint32_t>(Seed));
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
                                              static_cast<uint32_t>(Sim.RenderMode),
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
                                                         std::string_view name)
        {
            if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
                x + width > Sim.Width || y + height > Sim.Height)
            {
                return CaptureOutput{ .Error = "capture region is outside MapOutput bounds" };
            }

            const int32_t savedMode = Sim.RenderMode;
            const bool overrideMode = mode >= 0 && mode <= 7 && mode != savedMode;

            if (overrideMode)
            {
                Sim.RenderMode = mode;
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

            if (overrideMode)
            {
                Sim.RenderMode = savedMode;
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
                                             std::string_view name)
                {
                    return controller.CaptureArea(x, y, width, height, downsample, mode, name);
                },
                .GetTimings = [&controller]()
                {
                    return controller.GetTimings();
                },
            };
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
        LiveSessionController controller{ resources, device, initial, frameCapture, &platform, &swapchain, options };
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

        if (!controller.SupplyGraphResources(options))
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
                    0, 0, controller.Sim.Width, controller.Sim.Height, 1u, -1, name);
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
        LiveSessionController controller{ resources, device, initial, frameCapture, nullptr, nullptr, options };

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

        if (!controller.SupplyGraphResources(options))
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
