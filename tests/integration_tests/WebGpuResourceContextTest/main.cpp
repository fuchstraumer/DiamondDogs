#include "BootResult.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceConfig.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextWebGpu.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "WebGpuDeviceStage.hpp"

#include <emscripten/emscripten.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr uint64_t k_TestBudgetBytes = 4096u;
constexpr uint64_t k_ThreeTimesBudgetBytes = 3u * k_TestBudgetBytes;
constexpr uint64_t k_ChunkTestBudgetBytes = 2u * 1024u * 1024u;
constexpr uint64_t k_ChunkTestUploadBytes = 768u * 1024u;
constexpr uint64_t k_ExpectedChunkBytes = 256u * 1024u;
constexpr int32_t k_MaxTicksPerStage = 600;
constexpr int32_t k_TotalTests = 10;

enum class RunnerPhase : uint8_t
{
    Invalid = 0,
    Booting = 1,
    StartCase = 2,
    StepCase = 3,
    PopErrorScope = 4,
    Finished = 5,
};

struct ReadbackState
{
    wgpu::Buffer Buffer;
    uint64_t ByteSize{ 0u };
    bool Done{ false };
    bool Failed{ false };
};

struct ErrorScopeState
{
    bool Done{ false };
    bool HasError{ false };
    std::string Message;
};

struct AppState
{
    RunnerPhase Phase{ RunnerPhase::Booting };
    bootstrap::WebGpuDeviceStage DeviceStage;
    rhi::WebGpuRhiContextView View{};

    int32_t CaseIndex{ 1 };
    int32_t SubStep{ 0 };
    int32_t StageTicks{ 0 };
    int32_t PassedCount{ 0 };
    int32_t FailedCount{ 0 };

    bool CaseFinished{ false };
    bool CasePassed{ false };
    std::string CaseDetail;
    std::string StatusLog;

    std::unique_ptr<ResourceContext> Context;
    GraphicsResource ResourceA;
    GraphicsResource ResourceB;
    rhi::ResourceFuture<GraphicsResource> CreateFutureA;
    rhi::ResourceFuture<GraphicsResource> CreateFutureB;
    rhi::OperationFuture OpFutureA;
    rhi::OperationFuture OpFutureB;
    std::vector<uint32_t> ExpectedWordsA;
    std::vector<uint32_t> ExpectedWordsB;
    int64_t LastBytesStaged{ 0 };
    uint64_t SavedSerialA{ 0u };
    uint64_t SavedSerialB{ 0u };

    std::shared_ptr<ReadbackState> Readback;
    std::shared_ptr<ErrorScopeState> ErrorScope;
};

[[nodiscard]] uint32_t PatternWord(uint32_t seed, uint32_t index) noexcept;
[[nodiscard]] std::vector<uint32_t> MakePatternWords(uint32_t seed, uint64_t byte_count);
[[nodiscard]] std::string CopyWgpuStringView(wgpu::StringView view);
[[nodiscard]] std::string_view CaseName(int32_t case_index) noexcept;
[[nodiscard]] bool InitContextWithBudget(AppState& state, uint64_t budget_bytes);
void ResetCaseResources(AppState& state);
void CompleteCase(AppState& state, bool passed, std::string detail);
void RecordCaseOutcome(AppState& state, bool passed, std::string_view detail);
void UpdateDomStatus(std::string_view text);
void FinishAllCases(AppState& state);
[[nodiscard]] bool StartBufferReadback(AppState& state, const GraphicsResource& source, uint64_t byte_count);
[[nodiscard]] bool StartImageReadback(AppState& state,
                                      const GraphicsResource& source,
                                      uint32_t width,
                                      uint32_t height);
[[nodiscard]] bool VerifyReadbackWords(const ReadbackState& readback,
                                       std::span<const uint32_t> expected,
                                       std::string& detail_out);

void StepCase1(AppState& state);
void StepCase2(AppState& state);
void StepCase3(AppState& state);
void StepCase4(AppState& state);
void StepCase5(AppState& state);
void StepCase6(AppState& state);
void StepCase7(AppState& state);
void StepCase8(AppState& state);
void StepCase9(AppState& state);
void StepCase10(AppState& state);

void StepBooting(AppState& state);
void StepStartCase(AppState& state);
void StepActiveCase(AppState& state);
void StepPopErrorScope(AppState& state);
void MainLoopTick(void* user_data);

} // namespace

int main()
{
    EM_ASM(
        window['__ddTestDone'] = false;
        window['__ddTestPassed'] = false;
        window['__ddTestSummary'] = "";
    );

    AppState state{};
    bootstrap::DeviceConfig deviceConfig{};
    deviceConfig.Validation = rhi::ValidationLayers::BaseOnly;
    deviceConfig.ApplicationName = "WebGpuResourceContextTest";

    const bootstrap::DeviceRequirements requirements{};

    state.DeviceStage.StartHeadless(requirements, deviceConfig);
    emscripten_set_main_loop_arg(&MainLoopTick, &state, 0, 1);
    return 0;
}

namespace
{

uint32_t PatternWord(uint32_t seed, uint32_t index) noexcept
{
    const uint32_t value = (index ^ seed) * 747796405u + 2891336453u;
    const uint32_t word = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    return (word >> 22u) ^ word;
}

std::vector<uint32_t> MakePatternWords(uint32_t seed, uint64_t byte_count)
{
    const size_t wordCount = static_cast<size_t>(byte_count / sizeof(uint32_t));
    std::vector<uint32_t> words(wordCount, 0u);

    for (size_t index = 0u; index < wordCount; ++index)
    {
        words[index] = PatternWord(seed, static_cast<uint32_t>(index));
    }

    return words;
}

std::string CopyWgpuStringView(wgpu::StringView view)
{
    if (view.data == nullptr || view.length == 0u)
    {
        return std::string{};
    }

    if (view.length == WGPU_STRLEN)
    {
        return std::string{ view.data };
    }

    return std::string{ view.data, view.length };
}

std::string_view CaseName(int32_t case_index) noexcept
{
    switch (case_index)
    {
    case 1:
        return "1. DeferredUploadCompletes";
    case 2:
        return "2. CopyAfterDeferredUpload";
    case 3:
        return "3. DestroyAfterDeferredUpload";
    case 4:
        return "4. ImageBudget";
    case 5:
        return "5. WaitDoesNotBlock";
    case 6:
        return "6. CompletionTicket";
    case 7:
        return "7. BudgetFieldValidation";
    case 8:
        return "8. TimeSliceAndProgressRule";
    case 9:
        return "9. OldTickOverload";
    case 10:
        return "10. DestroyContextInFlight";
    default:
        return "Unknown";
    }
}

bool InitContextWithBudget(AppState& state, uint64_t budget_bytes)
{
    state.Context = std::make_unique<ResourceContext>();
    ResourceContextCreateInfo createInfo{};
    createInfo.Context = &state.DeviceStage.Context();
    createInfo.StagingRingSize = k_DefaultStagingRingSize;
    createInfo.UploadBytesPerTick = budget_bytes;
    return state.Context->Initialize(createInfo) == ResourceContextError::Success;
}

void ResetCaseResources(AppState& state)
{
    state.CreateFutureA = rhi::ResourceFuture<GraphicsResource>{};
    state.CreateFutureB = rhi::ResourceFuture<GraphicsResource>{};
    state.OpFutureA = rhi::OperationFuture{};
    state.OpFutureB = rhi::OperationFuture{};
    state.ResourceA = GraphicsResource{};
    state.ResourceB = GraphicsResource{};
    state.ExpectedWordsA.clear();
    state.ExpectedWordsB.clear();
    state.Readback.reset();
    state.Context.reset();
    state.LastBytesStaged = 0;
    state.SavedSerialA = 0u;
    state.SavedSerialB = 0u;
}

void CompleteCase(AppState& state, bool passed, std::string detail)
{
    state.CaseFinished = true;
    state.CasePassed = passed;
    state.CaseDetail = std::move(detail);
}

void UpdateDomStatus(std::string_view text)
{
    const std::string statusText{ text };

    EM_ASM(
        {
            var summaryText = UTF8ToString($0);
            var statusElement = document['getElementById']('status');

            if (statusElement)
            {
                statusElement['textContent'] = summaryText;
            }
        },
        statusText.c_str());
}

void RecordCaseOutcome(AppState& state, bool passed, std::string_view detail)
{
    if (passed)
    {
        ++state.PassedCount;
    }
    else
    {
        ++state.FailedCount;
    }

    const std::string line = std::format("[{}] {}: {}",
                                         passed ? "PASS" : "FAIL",
                                         CaseName(state.CaseIndex),
                                         detail);

    if (passed)
    {
        std::println("{}", line);
    }
    else
    {
        std::println(stderr, "{}", line);
    }

    state.StatusLog.append(line);
    state.StatusLog.push_back('\n');
    UpdateDomStatus(state.StatusLog);
}

void FinishAllCases(AppState& state)
{
    ResetCaseResources(state);
    state.Phase = RunnerPhase::Finished;

    const bool allPassed = (state.FailedCount == 0) && (state.PassedCount == k_TotalTests);
    const std::string summaryLine = std::format("WebGpuResourceContextTest: {}/{} passed ({} failed)",
                                                state.PassedCount,
                                                k_TotalTests,
                                                state.FailedCount);
    state.StatusLog.append(summaryLine);

    if (allPassed)
    {
        std::println("{}", summaryLine);
    }
    else
    {
        std::println(stderr, "{}", summaryLine);
    }

    EM_ASM(
        {
            var fullText = UTF8ToString($1);
            var statusElement = document['getElementById']('status');

            if (statusElement)
            {
                statusElement['textContent'] = fullText;
            }

            window['__ddTestDone'] = true;
            window['__ddTestPassed'] = Boolean($0);
            window['__ddTestSummary'] = fullText;
        },
        allPassed ? 1 : 0,
        state.StatusLog.c_str());

    emscripten_cancel_main_loop();
}

bool StartBufferReadback(AppState& state, const GraphicsResource& source, uint64_t byte_count)
{
    const wgpu::Buffer sourceBuffer = rhi::UnwrapWebGpuBuffer(source);

    if (!sourceBuffer)
    {
        return false;
    }

    wgpu::BufferDescriptor stagingDesc{};
    stagingDesc.size = byte_count;
    stagingDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    stagingDesc.label = "ResourceContextTest.ReadbackBuffer";

    std::shared_ptr<ReadbackState> readback = std::make_shared<ReadbackState>();
    readback->Buffer = state.View.Device.CreateBuffer(&stagingDesc);
    readback->ByteSize = byte_count;

    if (!readback->Buffer)
    {
        return false;
    }

    const wgpu::CommandEncoder encoder = state.View.Device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(sourceBuffer, 0u, readback->Buffer, 0u, byte_count);
    const wgpu::CommandBuffer commands = encoder.Finish();
    state.View.Queue.Submit(1u, &commands);

    const auto onMapDone = [readback](wgpu::MapAsyncStatus status, wgpu::StringView)
    {
        readback->Failed = (status != wgpu::MapAsyncStatus::Success);
        readback->Done = true;
    };

    readback->Buffer.MapAsync(wgpu::MapMode::Read,
                              0u,
                              static_cast<size_t>(byte_count),
                              wgpu::CallbackMode::AllowProcessEvents,
                              onMapDone);
    state.Readback = std::move(readback);
    return true;
}

bool StartImageReadback(AppState& state,
                        const GraphicsResource& source,
                        uint32_t width,
                        uint32_t height)
{
    const wgpu::Texture sourceTexture = rhi::UnwrapWebGpuTexture(source);

    if (!sourceTexture)
    {
        return false;
    }

    const uint32_t rowBytes = width * sizeof(uint32_t);
    const uint64_t totalBytes = static_cast<uint64_t>(rowBytes) * height;

    wgpu::BufferDescriptor stagingDesc{};
    stagingDesc.size = totalBytes;
    stagingDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    stagingDesc.label = "ResourceContextTest.ImageReadbackBuffer";

    std::shared_ptr<ReadbackState> readback = std::make_shared<ReadbackState>();
    readback->Buffer = state.View.Device.CreateBuffer(&stagingDesc);
    readback->ByteSize = totalBytes;

    if (!readback->Buffer)
    {
        return false;
    }

    wgpu::TexelCopyTextureInfo copySrc{};
    copySrc.texture = sourceTexture;
    copySrc.mipLevel = 0u;
    copySrc.origin = { 0u, 0u, 0u };

    wgpu::TexelCopyBufferInfo copyDst{};
    copyDst.buffer = readback->Buffer;
    copyDst.layout.offset = 0u;
    copyDst.layout.bytesPerRow = rowBytes;
    copyDst.layout.rowsPerImage = height;

    const wgpu::Extent3D extent{ width, height, 1u };
    const wgpu::CommandEncoder encoder = state.View.Device.CreateCommandEncoder();
    encoder.CopyTextureToBuffer(&copySrc, &copyDst, &extent);
    const wgpu::CommandBuffer commands = encoder.Finish();
    state.View.Queue.Submit(1u, &commands);

    const auto onMapDone = [readback](wgpu::MapAsyncStatus status, wgpu::StringView)
    {
        readback->Failed = (status != wgpu::MapAsyncStatus::Success);
        readback->Done = true;
    };

    readback->Buffer.MapAsync(wgpu::MapMode::Read,
                              0u,
                              static_cast<size_t>(totalBytes),
                              wgpu::CallbackMode::AllowProcessEvents,
                              onMapDone);
    state.Readback = std::move(readback);
    return true;
}

bool VerifyReadbackWords(const ReadbackState& readback,
                         std::span<const uint32_t> expected,
                         std::string& detail_out)
{
    const void* const mapped = readback.Buffer.GetConstMappedRange(0u, static_cast<size_t>(readback.ByteSize));

    if (mapped == nullptr)
    {
        detail_out = "GetConstMappedRange returned null";
        return false;
    }

    const std::span<const uint32_t> actual{ static_cast<const uint32_t*>(mapped), expected.size() };

    for (size_t index = 0u; index < expected.size(); ++index)
    {
        if (actual[index] != expected[index])
        {
            detail_out = std::format("word {} mismatch: expected 0x{:08X}, got 0x{:08X}",
                                     index,
                                     expected[index],
                                     actual[index]);
            readback.Buffer.Unmap();
            return false;
        }
    }

    readback.Buffer.Unmap();
    return true;
}

void StepCase1(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0x11112222u, k_ThreeTimesBudgetBytes);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case1.Buffer";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        if (state.CreateFutureA.State() != rhi::FutureState::Pending)
        {
            CompleteCase(state, false, "3x budget upload was not Pending before Tick");
            return;
        }

        state.Context->Tick({});

        if (state.CreateFutureA.IsDone())
        {
            CompleteCase(state, false, "3x budget upload was final after 1 Tick");
            return;
        }

        state.SubStep = 1;
        state.StageTicks = 1;
        return;
    }

    if (state.SubStep == 1)
    {
        state.Context->Tick({});
        ++state.StageTicks;

        if (!state.CreateFutureA.IsDone())
        {
            return;
        }

        if (state.CreateFutureA.State() != rhi::FutureState::Complete)
        {
            CompleteCase(state, false, std::format("upload failed: {}", rhi::ToString(state.CreateFutureA.Error())));
            return;
        }

        if (state.StageTicks < 3)
        {
            CompleteCase(state, false, std::format("completed in {} ticks, expected at least 3", state.StageTicks));
            return;
        }

        state.ResourceA = state.CreateFutureA.Get();

        if (!StartBufferReadback(state, state.ResourceA, k_ThreeTimesBudgetBytes))
        {
            CompleteCase(state, false, "StartBufferReadback failed");
            return;
        }

        state.SubStep = 2;
        return;
    }

    state.View.Instance.ProcessEvents();

    if (!state.Readback->Done)
    {
        return;
    }

    if (state.Readback->Failed)
    {
        CompleteCase(state, false, "MapAsync failed");
        return;
    }

    std::string detail;

    if (!VerifyReadbackWords(*state.Readback, state.ExpectedWordsA, detail))
    {
        CompleteCase(state, false, detail);
        return;
    }

    const ResourceContextCounters counters = state.Context->Counters();
    CompleteCase(state,
                 true,
                 std::format("completed in {} ticks, staged={} bytes, deferred={}",
                             state.StageTicks,
                             counters.BytesStaged,
                             counters.UploadOpsDeferred));
}

void StepCase2(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case2.BufferA";

        state.CreateFutureA = state.Context->CreateBuffer(info, {});
        info.Name = "Case2.BufferB";
        state.CreateFutureB = state.Context->CreateBuffer(info, {});

        if (state.CreateFutureA.State() != rhi::FutureState::Complete ||
            state.CreateFutureB.State() != rhi::FutureState::Complete)
        {
            CompleteCase(state, false, "empty buffer creates did not complete immediately");
            return;
        }

        state.ResourceA = state.CreateFutureA.Get();
        state.ResourceB = state.CreateFutureB.Get();
        state.ExpectedWordsA = MakePatternWords(0x33334444u, k_ThreeTimesBudgetBytes);

        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        state.OpFutureA = state.Context->SetBufferData(state.ResourceA,
                                                       std::span<const RhiBufferResourceData>{ &piece, 1u },
                                                       {});
        state.OpFutureB = state.Context->CopyBufferContents(state.ResourceA, state.ResourceB, {});

        if (state.OpFutureA.State() != rhi::FutureState::Pending ||
            state.OpFutureB.State() != rhi::FutureState::Pending)
        {
            CompleteCase(state, false, "deferred upload and subsequent copy were not both Pending");
            return;
        }

        state.SubStep = 1;
        return;
    }

    if (state.SubStep == 1)
    {
        state.Context->Tick({});

        if (!state.OpFutureA.IsDone() || !state.OpFutureB.IsDone())
        {
            return;
        }

        if (state.OpFutureA.State() != rhi::FutureState::Complete ||
            state.OpFutureB.State() != rhi::FutureState::Complete)
        {
            CompleteCase(state, false, "upload or copy future failed");
            return;
        }

        if (!StartBufferReadback(state, state.ResourceB, k_ThreeTimesBudgetBytes))
        {
            CompleteCase(state, false, "StartBufferReadback failed");
            return;
        }

        state.SubStep = 2;
        return;
    }

    state.View.Instance.ProcessEvents();

    if (!state.Readback->Done)
    {
        return;
    }

    if (state.Readback->Failed)
    {
        CompleteCase(state, false, "MapAsync failed");
        return;
    }

    std::string detail;

    if (!VerifyReadbackWords(*state.Readback, state.ExpectedWordsA, detail))
    {
        CompleteCase(state, false, detail);
        return;
    }

    CompleteCase(state, true, "copy queued after deferred upload matched all uploaded words");
}

void StepCase3(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case3.BufferA";

        state.CreateFutureA = state.Context->CreateBuffer(info, {});
        state.ResourceA = state.CreateFutureA.Get();
        state.ExpectedWordsA = MakePatternWords(0x55556666u, k_ThreeTimesBudgetBytes);

        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        state.OpFutureA = state.Context->SetBufferData(state.ResourceA,
                                                       std::span<const RhiBufferResourceData>{ &piece, 1u },
                                                       {});
        state.OpFutureB = state.Context->DestroyResource(state.ResourceA);

        if (state.OpFutureB.State() != rhi::FutureState::Pending)
        {
            CompleteCase(state, false, "destroy after deferred upload ran immediately instead of queuing");
            return;
        }

        state.SubStep = 1;
        return;
    }

    state.Context->Tick({});

    if (!state.OpFutureA.IsDone() || !state.OpFutureB.IsDone())
    {
        return;
    }

    const bool uploadOk = state.OpFutureA.State() == rhi::FutureState::Complete;
    const bool destroyOk = state.OpFutureB.State() == rhi::FutureState::Complete;
    const int64_t retired = state.Context->Counters().ResourcesRetired;

    if (!uploadOk || !destroyOk || retired != 1)
    {
        CompleteCase(state,
                     false,
                     std::format("uploadState={}, destroyState={}, retired={}",
                                 static_cast<uint32_t>(state.OpFutureA.State()),
                                 static_cast<uint32_t>(state.OpFutureB.State()),
                                 retired));
        return;
    }

    CompleteCase(state, true, "upload and destroy completed in order with zero validation errors");
}

void StepCase4(AppState& state)
{
    constexpr uint32_t k_Width = 64u;
    constexpr uint32_t k_Height = 48u;
    constexpr uint64_t k_ImageBytes = static_cast<uint64_t>(k_Width) * k_Height * sizeof(uint32_t);

    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0x77778888u, k_ImageBytes);
        const RhiImageResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .Width = static_cast<int32_t>(k_Width),
            .Height = static_cast<int32_t>(k_Height),
            .ArrayLayer = 0,
            .NumLayers = 1,
            .MipLevel = 0,
            .NumMips = 1,
            .X = 0,
            .Y = 0,
            .Z = 0,
            .Depth = 1,
        };

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Type = ImageType::Type2D;
        info.Format = CommonFormats::RGBA8_UNorm;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination;
        info.Width = k_Width;
        info.Height = k_Height;
        info.Depth = 1u;
        info.MipLevels = 1u;
        info.ArrayLayers = 1u;
        info.SampleCount = 1u;
        info.Name = "Case4.Texture";

        state.CreateFutureA = state.Context->CreateImage(info,
                                                         std::span<const RhiImageResourceData>{ &piece, 1u },
                                                         ImageUsageBits::SampledImage);

        if (state.CreateFutureA.State() != rhi::FutureState::Pending)
        {
            CompleteCase(state, false, "3x budget image upload was not deferred");
            return;
        }

        state.LastBytesStaged = 0;
        state.SubStep = 1;
        state.StageTicks = 0;
        return;
    }

    if (state.SubStep == 1)
    {
        state.Context->Tick({});
        ++state.StageTicks;

        const int64_t nowStaged = state.Context->Counters().BytesStaged;
        const int64_t deltaStaged = nowStaged - state.LastBytesStaged;
        state.LastBytesStaged = nowStaged;

        if (deltaStaged > static_cast<int64_t>(k_TestBudgetBytes))
        {
            CompleteCase(state,
                         false,
                         std::format("tick {} staged {} bytes, exceeding budget {}",
                                     state.StageTicks,
                                     deltaStaged,
                                     k_TestBudgetBytes));
            return;
        }

        if (!state.CreateFutureA.IsDone())
        {
            return;
        }

        if (state.CreateFutureA.State() != rhi::FutureState::Complete)
        {
            CompleteCase(state, false, "image upload future failed");
            return;
        }

        if (state.StageTicks < 3)
        {
            CompleteCase(state, false, std::format("image upload completed in {} ticks, expected >= 3", state.StageTicks));
            return;
        }

        state.ResourceA = state.CreateFutureA.Get();

        if (!StartImageReadback(state, state.ResourceA, k_Width, k_Height))
        {
            CompleteCase(state, false, "StartImageReadback failed");
            return;
        }

        state.SubStep = 2;
        return;
    }

    state.View.Instance.ProcessEvents();

    if (!state.Readback->Done)
    {
        return;
    }

    if (state.Readback->Failed)
    {
        CompleteCase(state, false, "MapAsync failed");
        return;
    }

    std::string detail;

    if (!VerifyReadbackWords(*state.Readback, state.ExpectedWordsA, detail))
    {
        CompleteCase(state, false, detail);
        return;
    }

    CompleteCase(state,
                 true,
                 std::format("image uploaded in {} ticks within {} B/tick budget and matched readback",
                             state.StageTicks,
                             k_TestBudgetBytes));
}

void StepCase5(AppState& state)
{
    if (!InitContextWithBudget(state, k_TestBudgetBytes))
    {
        CompleteCase(state, false, "Initialize failed");
        return;
    }

    state.ExpectedWordsA = MakePatternWords(0x9999AAAAu, k_ThreeTimesBudgetBytes);
    const RhiBufferResourceData piece
    {
        .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
        .DestinationOffset = 0u,
    };

    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = BufferUsageBits::TransferDestination;
    info.Size = k_ThreeTimesBudgetBytes;
    info.Name = "Case5.Buffer";

    state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });
    const rhi::FutureState waitPending = state.Context->Wait(state.CreateFutureA, rhi::k_WaitForever);

    if (waitPending != rhi::FutureState::Pending)
    {
        CompleteCase(state, false, std::format("Wait on Pending returned {}", static_cast<uint32_t>(waitPending)));
        return;
    }

    state.Context->Tick({});
    const rhi::FutureState waitCreated = state.Context->Wait(state.CreateFutureA, rhi::k_WaitForever);

    if (waitCreated != rhi::FutureState::Created)
    {
        CompleteCase(state, false, std::format("Wait on Created returned {}", static_cast<uint32_t>(waitCreated)));
        return;
    }

    CompleteCase(state, true, "Wait(k_WaitForever) returned Pending and Created immediately without blocking");
}

void StepCase6(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0xBBBBCCCCu, 256u);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferDestination;
        info.Size = 256u;
        info.Name = "Case6.BufferA";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        if (state.CreateFutureA.State() != rhi::FutureState::Created)
        {
            CompleteCase(state,
                         false,
                         std::format("immediate upload future state was {}, expected Created before callback",
                                     static_cast<uint32_t>(state.CreateFutureA.State())));
            return;
        }

        const rhi::GpuWait waitA = state.CreateFutureA.Completion();
        state.SavedSerialA = waitA.Value;

        if (state.SavedSerialA == 0u || !waitA.Submitted)
        {
            CompleteCase(state, false, "first upload Completion() had serial 0 or Submitted=false");
            return;
        }

        state.Context->Tick({});

        if (state.CreateFutureA.State() != rhi::FutureState::Created)
        {
            CompleteCase(state, false, "first upload completed in the same Tick that registered OnSubmittedWorkDone");
            return;
        }

        info.Name = "Case6.BufferB";
        state.CreateFutureB = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        if (state.CreateFutureB.State() != rhi::FutureState::Created)
        {
            CompleteCase(state, false, "second upload future state was not Created");
            return;
        }

        const rhi::GpuWait waitB = state.CreateFutureB.Completion();
        state.SavedSerialB = waitB.Value;

        if (state.SavedSerialB <= state.SavedSerialA || !waitB.Submitted)
        {
            CompleteCase(state,
                         false,
                         std::format("serials did not rise ({} then {})", state.SavedSerialA, state.SavedSerialB));
            return;
        }

        state.Context->Tick({});
        state.SubStep = 1;
        return;
    }

    state.Context->Tick({});

    if (!state.CreateFutureA.IsDone() || !state.CreateFutureB.IsDone())
    {
        return;
    }

    if (state.CreateFutureA.State() != rhi::FutureState::Complete ||
        state.CreateFutureB.State() != rhi::FutureState::Complete)
    {
        CompleteCase(state, false, "one of the futures failed");
        return;
    }

    CompleteCase(state,
                 true,
                 std::format("rising serials ({} < {}) completed only after OnSubmittedWorkDone callbacks",
                             state.SavedSerialA,
                             state.SavedSerialB));
}

void StepCase7(AppState& state)
{
    ResourceContext zeroBudgetCtx;
    ResourceContextCreateInfo zeroInfo{};
    zeroInfo.Context = &state.DeviceStage.Context();
    zeroInfo.StagingRingSize = k_MinStagingRingSize;
    zeroInfo.UploadBytesPerTick = 0u;
    const ResourceContextError zeroErr = zeroBudgetCtx.Initialize(zeroInfo);

    ResourceContext overBudgetCtx;
    ResourceContextCreateInfo overInfo{};
    overInfo.Context = &state.DeviceStage.Context();
    overInfo.StagingRingSize = k_MinStagingRingSize;
    overInfo.UploadBytesPerTick = k_MinStagingRingSize + 4u;
    const ResourceContextError overErr = overBudgetCtx.Initialize(overInfo);

    ResourceContext validCtx;
    ResourceContextCreateInfo validInfo{};
    validInfo.Context = &state.DeviceStage.Context();
    validInfo.StagingRingSize = k_MinStagingRingSize;
    validInfo.UploadBytesPerTick = k_MinStagingRingSize;
    const ResourceContextError validErr = validCtx.Initialize(validInfo);

    const bool passed = (zeroErr == ResourceContextError::InvalidCreateInfo) &&
                        (overErr == ResourceContextError::InvalidCreateInfo) &&
                        (validErr == ResourceContextError::Success);

    if (!passed)
    {
        CompleteCase(state,
                     false,
                     std::format("zero={}, over={}, valid={}",
                                 ToString(zeroErr),
                                 ToString(overErr),
                                 ToString(validErr)));
        return;
    }

    CompleteCase(state, true, "UploadBytesPerTick==0 and >StagingRingSize rejected with InvalidCreateInfo");
}

void StepCase8(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_ChunkTestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        BufferCreateInfo dummyInfo{};
        dummyInfo.Domain = ResourceDomain::GPUOnly;
        dummyInfo.Usage = BufferUsageBits::TransferDestination;
        dummyInfo.Size = k_ChunkTestBudgetBytes;
        dummyInfo.Name = "Case8.Dummy";

        state.ExpectedWordsB = MakePatternWords(0x12345678u, k_ChunkTestBudgetBytes);
        const RhiBufferResourceData dummyPiece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsB }),
            .DestinationOffset = 0u,
        };

        state.CreateFutureB = state.Context->CreateBuffer(dummyInfo,
                                                          std::span<const RhiBufferResourceData>{ &dummyPiece, 1u });

        if (state.CreateFutureB.State() != rhi::FutureState::Created)
        {
            CompleteCase(state, false, "initial budget-consuming upload did not run immediately");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0xDDDDEEEEu, k_ChunkTestUploadBytes);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = k_ChunkTestUploadBytes;
        info.Name = "Case8.SlicedBuffer";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        if (state.CreateFutureA.State() != rhi::FutureState::Pending)
        {
            CompleteCase(state, false, "sliced upload was not deferred");
            return;
        }

        state.LastBytesStaged = state.Context->Counters().BytesStaged;
        state.StageTicks = 0;

        for (int32_t tick = 0; tick < 3; ++tick)
        {
            const int64_t beforeSliceTicks = state.Context->Counters().UploadTicksAtTimeSlice;
            state.Context->Tick({}, std::chrono::nanoseconds{ 1 });
            const ResourceContextCounters after = state.Context->Counters();
            const int64_t chunkDelta = after.BytesStaged - state.LastBytesStaged;
            state.LastBytesStaged = after.BytesStaged;

            if (chunkDelta != static_cast<int64_t>(k_ExpectedChunkBytes))
            {
                CompleteCase(state,
                             false,
                             std::format("tick {} wrote {} bytes under 1ns slice, expected {}",
                                         tick + 1,
                                         chunkDelta,
                                         k_ExpectedChunkBytes));
                return;
            }

            if (after.UploadTicksAtTimeSlice != beforeSliceTicks + 1)
            {
                CompleteCase(state, false, "UploadTicksAtTimeSlice did not increment under 1ns slice");
                return;
            }
        }

        state.SubStep = 1;
        return;
    }

    if (state.SubStep == 1)
    {
        state.Context->Tick({}, std::chrono::seconds{ 10 });

        if (!state.CreateFutureA.IsDone())
        {
            return;
        }

        if (state.CreateFutureA.State() != rhi::FutureState::Complete)
        {
            CompleteCase(state, false, "sliced upload failed");
            return;
        }

        state.ResourceA = state.CreateFutureA.Get();

        if (!StartBufferReadback(state, state.ResourceA, k_ChunkTestUploadBytes))
        {
            CompleteCase(state, false, "StartBufferReadback failed");
            return;
        }

        state.SubStep = 2;
        return;
    }

    if (state.SubStep == 2)
    {
        state.View.Instance.ProcessEvents();

        if (!state.Readback->Done)
        {
            return;
        }

        if (state.Readback->Failed)
        {
            CompleteCase(state, false, "MapAsync failed");
            return;
        }

        std::string detail;

        if (!VerifyReadbackWords(*state.Readback, state.ExpectedWordsA, detail))
        {
            CompleteCase(state, false, detail);
            return;
        }

        ResetCaseResources(state);

        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "re-Initialize for budget counter check failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0xCAFEBABEu, k_ThreeTimesBudgetBytes);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case8.BudgetBuffer";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        for (int32_t tick = 0; tick < 3; ++tick)
        {
            state.Context->Tick({}, std::chrono::seconds{ 10 });
        }

        const ResourceContextCounters counters = state.Context->Counters();

        if (counters.UploadTicksAtBudget != 3 || counters.UploadTicksAtTimeSlice != 0)
        {
            CompleteCase(state,
                         false,
                         std::format("expected UploadTicksAtBudget=3 and UploadTicksAtTimeSlice=0, got {} and {}",
                                     counters.UploadTicksAtBudget,
                                     counters.UploadTicksAtTimeSlice));
            return;
        }

        CompleteCase(state, true, "1ns slice wrote 1 chunk/tick via progress rule; large slice hit budget counter only");
    }
}

void StepCase9(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0xF00DBABEu, k_ThreeTimesBudgetBytes);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case9.Buffer";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });
        state.SubStep = 1;
        return;
    }

    if (state.SubStep == 1)
    {
        state.Context->Tick({});

        if (!state.CreateFutureA.IsDone())
        {
            return;
        }

        const ResourceContextCounters counters = state.Context->Counters();

        if (counters.UploadTicksAtBudget < 3 || counters.UploadTicksAtTimeSlice != 0)
        {
            CompleteCase(state,
                         false,
                         std::format("old Tick overload counters: budget={}, slice={}",
                                     counters.UploadTicksAtBudget,
                                     counters.UploadTicksAtTimeSlice));
            return;
        }

        state.ResourceA = state.CreateFutureA.Get();

        if (!StartBufferReadback(state, state.ResourceA, k_ThreeTimesBudgetBytes))
        {
            CompleteCase(state, false, "StartBufferReadback failed");
            return;
        }

        state.SubStep = 2;
        return;
    }

    state.View.Instance.ProcessEvents();

    if (!state.Readback->Done)
    {
        return;
    }

    if (state.Readback->Failed)
    {
        CompleteCase(state, false, "MapAsync failed");
        return;
    }

    std::string detail;

    if (!VerifyReadbackWords(*state.Readback, state.ExpectedWordsA, detail))
    {
        CompleteCase(state, false, detail);
        return;
    }

    CompleteCase(state, true, "Tick(pending_values) drained by byte budget with UploadTicksAtTimeSlice==0");
}

void StepCase10(AppState& state)
{
    if (state.SubStep == 0)
    {
        if (!InitContextWithBudget(state, k_TestBudgetBytes))
        {
            CompleteCase(state, false, "Initialize failed");
            return;
        }

        state.ExpectedWordsA = MakePatternWords(0xABCD1234u, k_ThreeTimesBudgetBytes);
        const RhiBufferResourceData piece
        {
            .Data = std::as_bytes(std::span<const uint32_t>{ state.ExpectedWordsA }),
            .DestinationOffset = 0u,
        };

        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::TransferDestination;
        info.Size = k_ThreeTimesBudgetBytes;
        info.Name = "Case10.InFlightA";

        state.CreateFutureA = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });
        info.Name = "Case10.DeferredB";
        state.CreateFutureB = state.Context->CreateBuffer(info, std::span<const RhiBufferResourceData>{ &piece, 1u });

        state.Context->Tick({});

        // Destroy the futures before destroying the context, while OnSubmittedWorkDone is still in flight on the queue.
        state.CreateFutureA = rhi::ResourceFuture<GraphicsResource>{};
        state.CreateFutureB = rhi::ResourceFuture<GraphicsResource>{};
        state.Context.reset();

        state.SubStep = 1;
        state.StageTicks = 0;
        return;
    }

    state.View.Instance.ProcessEvents();
    ++state.StageTicks;

    if (state.StageTicks < 8)
    {
        return;
    }

    CompleteCase(state, true, "destroyed context with OnSubmittedWorkDone in flight and pumped 8 ticks cleanly");
}

void StepBooting(AppState& state)
{
    state.DeviceStage.Step();

    if (state.DeviceStage.HasFailed())
    {
        state.StatusLog = std::format("WebGpuResourceContextTest: device boot failed: {}",
                                      bootstrap::FormatFailure(state.DeviceStage.Failure()));
        FinishAllCases(state);
        return;
    }

    if (!state.DeviceStage.IsReady())
    {
        return;
    }

    state.View = rhi::UnwrapWebGpuRhiContext(state.DeviceStage.Context());

    if (!state.View.Instance || !state.View.Device || !state.View.Queue)
    {
        state.StatusLog = "WebGpuResourceContextTest: missing WebGPU handles";
        FinishAllCases(state);
        return;
    }

    state.CaseIndex = 1;
    state.Phase = RunnerPhase::StartCase;
}

void StepStartCase(AppState& state)
{
    if (state.CaseIndex > k_TotalTests)
    {
        FinishAllCases(state);
        return;
    }

    ResetCaseResources(state);
    state.SubStep = 0;
    state.StageTicks = 0;
    state.CaseFinished = false;
    state.CasePassed = false;
    state.CaseDetail.clear();

    state.View.Device.PushErrorScope(wgpu::ErrorFilter::Validation);
    state.Phase = RunnerPhase::StepCase;
}

void StepActiveCase(AppState& state)
{
    switch (state.CaseIndex)
    {
    case 1:
        StepCase1(state);
        break;
    case 2:
        StepCase2(state);
        break;
    case 3:
        StepCase3(state);
        break;
    case 4:
        StepCase4(state);
        break;
    case 5:
        StepCase5(state);
        break;
    case 6:
        StepCase6(state);
        break;
    case 7:
        StepCase7(state);
        break;
    case 8:
        StepCase8(state);
        break;
    case 9:
        StepCase9(state);
        break;
    case 10:
        StepCase10(state);
        break;
    default:
        CompleteCase(state, false, "invalid test case index");
        break;
    }

    ++state.StageTicks;

    if (!state.CaseFinished && state.StageTicks > k_MaxTicksPerStage)
    {
        CompleteCase(state, false, "timed out exceeding tick limit");
    }

    if (!state.CaseFinished)
    {
        return;
    }

    ResetCaseResources(state);

    std::shared_ptr<ErrorScopeState> scopeState = std::make_shared<ErrorScopeState>();
    const auto onPopErrorScope = [scopeState](wgpu::PopErrorScopeStatus status,
                                              wgpu::ErrorType type,
                                              wgpu::StringView message)
    {
        scopeState->HasError = (status != wgpu::PopErrorScopeStatus::Success) ||
                               (type != wgpu::ErrorType::NoError);
        scopeState->Message = CopyWgpuStringView(message);
        scopeState->Done = true;
    };

    state.View.Device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents, onPopErrorScope);
    state.ErrorScope = std::move(scopeState);
    state.StageTicks = 0;
    state.Phase = RunnerPhase::PopErrorScope;
}

void StepPopErrorScope(AppState& state)
{
    state.View.Instance.ProcessEvents();
    ++state.StageTicks;

    if (!state.ErrorScope->Done)
    {
        if (state.StageTicks > k_MaxTicksPerStage)
        {
            RecordCaseOutcome(state, false, "PopErrorScope timed out");
            ++state.CaseIndex;
            state.Phase = RunnerPhase::StartCase;
        }

        return;
    }

    if (state.ErrorScope->HasError)
    {
        RecordCaseOutcome(state,
                          false,
                          std::format("{} (WebGPU validation error: {})",
                                      state.CaseDetail,
                                      state.ErrorScope->Message));
    }
    else
    {
        RecordCaseOutcome(state, state.CasePassed, state.CaseDetail);
    }

    state.ErrorScope.reset();
    ++state.CaseIndex;
    state.Phase = RunnerPhase::StartCase;
}

void MainLoopTick(void* user_data)
{
    AppState& state = *static_cast<AppState*>(user_data);

    switch (state.Phase)
    {
    case RunnerPhase::Booting:
        StepBooting(state);
        break;
    case RunnerPhase::StartCase:
        StepStartCase(state);
        break;
    case RunnerPhase::StepCase:
        StepActiveCase(state);
        break;
    case RunnerPhase::PopErrorScope:
        StepPopErrorScope(state);
        break;
    case RunnerPhase::Invalid:
    case RunnerPhase::Finished:
        break;
    }
}

} // namespace
