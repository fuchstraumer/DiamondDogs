#include "ColorSpaces.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "ManifestDescriptors.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "TransferFunctions.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Runs every ddColor function on the GPU (ColorMathKernels.slang) on the same input samples, and compares each output
// buffer with the double precision reference in modules/ColorPipe. The matrices prove the convention: the Slang
// constants are the transpose of the C++ matrices, and a wrong transpose gives a large error here.
//
// The shader applies the encode functions and the matrices to the sample itself, the decode functions to
// saturate(sample / 64), and the Kelvin function to lerp(1000, 15000, saturate(sample.x / 64)). The references below
// use the same inputs.

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr int32_t k_RandomSampleCount = 4096;
    constexpr uint32_t k_RandomSeed = 0x0C01025Eu;
    constexpr float k_SampleRange = 64.0f;
    constexpr double k_RelativeErrorFloor = 1.0e-6;
    constexpr std::string_view k_ModuleName{ "ColorMathKernels" };
    constexpr std::array<std::string_view, 1> k_EntryPoints{ "ColorMathCS" };

    constexpr double k_MatrixTolerance = 2.0e-6;
    constexpr double k_TransferTolerance = 1.0e-5;
    constexpr double k_KelvinTolerance = 1.0e-4;
    // PQ is ill-conditioned in float32. PqEncode raises a ratio near 1 to the power 78.8, so each ulp of the ratio
    // (6e-8) becomes 4.7e-6 of the signal, and the rounding of the ratio takes the error past 1e-5. PqDecode subtracts
    // nearly equal numbers in its denominator, c2 - c3 * p, and raises the quotient to the power 6.3
    constexpr double k_PqTolerance = 1.0e-4;

    // 0, 1e-6, 1, and the nit values 80, 100 and 10000
    constexpr std::array<float, 6> k_EdgeValues{ 0.0f, 1.0e-6f, 1.0f, 80.0f, 100.0f, 10000.0f };

    enum class ReferenceKind : uint8_t
    {
        Invalid = 0,
        GamutMatrix,
        Luminance,
        SrgbEncode,
        SrgbDecode,
        Gamma22Encode,
        Gamma22Decode,
        PqEncode,
        PqDecode,
        ScRgbEncode,
        ScRgbDecode,
        Kelvin,
    };

    // A matrix mixes components, and cancellation makes the relative error of one component meaningless, so a matrix
    // is measured against the largest component of its result. A transfer function acts on each component alone.
    enum class ErrorMetric : uint8_t
    {
        Invalid = 0,
        NormRelative,
        ComponentRelative,
        ScalarRelative,
    };

    struct FunctionCase
    {
        std::string_view Name;
        std::string_view BufferName;
        ReferenceKind Kind;
        ErrorMetric Metric;
        double Tolerance;
        const color::Gamut* Source;
        const color::Gamut* Destination;
    };

    struct FunctionResult
    {
        double WorstError{ 0.0 };
        int32_t WorstIndex{ 0 };
        bool Finite{ true };
    };

    struct GpuBuffers
    {
        std::shared_ptr<GraphicsResourceReply> InputReply;
        GraphicsResource Input;
        std::vector<GraphicsResource> Outputs;
        GraphicsResource Readback;
    };

    constexpr color::ChromaticAdaptation k_GamutAdaptation = color::ChromaticAdaptation::Bradford;

    const std::array<FunctionCase, 16> k_Cases
    {
        FunctionCase{ "Rec709ToAp1", "Rec709ToAp1Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::Rec709, &color::AP1 },
        FunctionCase{ "Ap1ToRec709", "Ap1ToRec709Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::AP1, &color::Rec709 },
        FunctionCase{ "Rec2020ToAp1", "Rec2020ToAp1Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::Rec2020, &color::AP1 },
        FunctionCase{ "Ap1ToRec2020", "Ap1ToRec2020Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::AP1, &color::Rec2020 },
        FunctionCase{ "DisplayP3ToAp1", "DisplayP3ToAp1Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::DisplayP3, &color::AP1 },
        FunctionCase{ "Ap1ToDisplayP3", "Ap1ToDisplayP3Out", ReferenceKind::GamutMatrix, ErrorMetric::NormRelative, k_MatrixTolerance, &color::AP1, &color::DisplayP3 },
        FunctionCase{ "Ap1Luminance", "LuminanceOut", ReferenceKind::Luminance, ErrorMetric::ScalarRelative, k_MatrixTolerance, &color::AP1, nullptr },
        FunctionCase{ "SrgbEncode", "SrgbEncodeOut", ReferenceKind::SrgbEncode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "SrgbDecode", "SrgbDecodeOut", ReferenceKind::SrgbDecode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "Gamma22Encode", "Gamma22EncodeOut", ReferenceKind::Gamma22Encode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "Gamma22Decode", "Gamma22DecodeOut", ReferenceKind::Gamma22Decode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "PqEncode", "PqEncodeOut", ReferenceKind::PqEncode, ErrorMetric::ComponentRelative, k_PqTolerance, nullptr, nullptr },
        FunctionCase{ "PqDecode", "PqDecodeOut", ReferenceKind::PqDecode, ErrorMetric::ComponentRelative, k_PqTolerance, nullptr, nullptr },
        FunctionCase{ "ScRgbEncode", "ScRgbEncodeOut", ReferenceKind::ScRgbEncode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "ScRgbDecode", "ScRgbDecodeOut", ReferenceKind::ScRgbDecode, ErrorMetric::ComponentRelative, k_TransferTolerance, nullptr, nullptr },
        FunctionCase{ "KelvinToAp1", "KelvinOut", ReferenceKind::Kelvin, ErrorMetric::NormRelative, k_KelvinTolerance, &color::AP1, nullptr },
    };

    [[nodiscard]] std::vector<float> BuildSamples();
    [[nodiscard]] color::Vec3d ReferenceFor(const FunctionCase& test_case, const color::Vec3d& input);
    [[nodiscard]] color::Vec3d Apply(double (*function)(double) noexcept, const color::Vec3d& input);
    [[nodiscard]] color::Vec3d SignalOf(const color::Vec3d& input);
    [[nodiscard]] double ErrorOf(ErrorMetric metric, const color::Vec3d& gpu, const color::Vec3d& reference);
    [[nodiscard]] bool WaitUntilResourceIsKnown(const GraphicsResourceReply& reply) noexcept;
    [[nodiscard]] std::optional<GpuBuffers> CreateBuffers(ResourceContext& resources, std::span<const float> samples);
    void DestroyBuffers(ResourceContext& resources, const GpuBuffers& buffers);
    [[nodiscard]] bool RecordAndSubmit(rhi::Device& device, const rhi::ManifestProgram& program, VkDescriptorSet descriptor_set, const GpuBuffers& buffers, int32_t sample_count);
    [[nodiscard]] int32_t CompareOutputs(std::span<const float> samples, std::span<const float> gpu_outputs);
    [[nodiscard]] int32_t RunAll(rhi::RhiSystem& rhi_system, ResourceContext& resources, const rhi::ShaderLibrary& library);
}

int main()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ColorMathTest";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(COLOR_MATH_TEST_MANIFEST);

    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", COLOR_MATH_TEST_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    int32_t failures = 0;

    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        failures = RunAll(*rhiSystem, resources, **library);
    }

    if (failures != 0)
    {
        std::cerr << std::format("ColorMathTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "ColorMathTest: every ddColor function matches the CPU reference\n";
    return 0;
}

namespace
{
    std::vector<float> BuildSamples()
    {
        std::vector<float> samples;
        samples.reserve((k_RandomSampleCount + k_EdgeValues.size() * 4) * 4);

        std::mt19937 random(k_RandomSeed);
        std::uniform_real_distribution<float> distribution(0.0f, k_SampleRange);

        for (int32_t sampleIndex = 0; sampleIndex < k_RandomSampleCount; ++sampleIndex)
        {
            const float red = distribution(random);
            const float green = distribution(random);
            const float blue = distribution(random);
            samples.insert(samples.end(), { red, green, blue, 0.0f });
        }

        // each edge value on its own in every channel, then in all three
        for (const float edge : k_EdgeValues)
        {
            samples.insert(samples.end(), { edge, 0.0f, 0.0f, 0.0f });
            samples.insert(samples.end(), { 0.0f, edge, 0.0f, 0.0f });
            samples.insert(samples.end(), { 0.0f, 0.0f, edge, 0.0f });
            samples.insert(samples.end(), { edge, edge, edge, 0.0f });
        }

        return samples;
    }

    color::Vec3d Apply(double (*function)(double) noexcept, const color::Vec3d& input)
    {
        return color::Vec3d{ function(input.X), function(input.Y), function(input.Z) };
    }

    // the decode functions read saturate(sample / 64)
    color::Vec3d SignalOf(const color::Vec3d& input)
    {
        const double scale = 1.0 / k_SampleRange;

        return color::Vec3d{ std::clamp(input.X * scale, 0.0, 1.0), std::clamp(input.Y * scale, 0.0, 1.0), std::clamp(input.Z * scale, 0.0, 1.0) };
    }

    color::Vec3d ReferenceFor(const FunctionCase& test_case, const color::Vec3d& input)
    {
        switch (test_case.Kind)
        {
        case ReferenceKind::GamutMatrix:
            return color::Multiply(color::RgbToRgb(*test_case.Source, *test_case.Destination, k_GamutAdaptation), input);
        case ReferenceKind::Luminance:
            return color::Vec3d{ color::Luminance(*test_case.Source, input), 0.0, 0.0 };
        case ReferenceKind::SrgbEncode:
            return Apply(color::SrgbEncode, input);
        case ReferenceKind::SrgbDecode:
            return Apply(color::SrgbDecode, SignalOf(input));
        case ReferenceKind::Gamma22Encode:
            return Apply(color::Gamma22Encode, input);
        case ReferenceKind::Gamma22Decode:
            return Apply(color::Gamma22Decode, SignalOf(input));
        case ReferenceKind::PqEncode:
            return Apply(color::PqEncode, input);
        case ReferenceKind::PqDecode:
            return Apply(color::PqDecode, SignalOf(input));
        case ReferenceKind::ScRgbEncode:
            return Apply(color::ScRgbEncode, input);
        case ReferenceKind::ScRgbDecode:
            return Apply(color::ScRgbDecode, input);
        case ReferenceKind::Kelvin:
        {
            const double kelvin = color::KelvinMinimum + (color::KelvinMaximum - color::KelvinMinimum) * SignalOf(input).X;
            return color::KelvinToRgb(*test_case.Source, kelvin);
        }
        default:
            return color::Vec3d{ std::nan(""), std::nan(""), std::nan("") };
        }
    }

    double ErrorOf(ErrorMetric metric, const color::Vec3d& gpu, const color::Vec3d& reference)
    {
        const std::array<double, 3> gpuValues{ gpu.X, gpu.Y, gpu.Z };
        const std::array<double, 3> referenceValues{ reference.X, reference.Y, reference.Z };
        double largestDifference = 0.0;
        double largestReference = 0.0;
        double worstComponent = 0.0;

        for (int32_t component = 0; component < 3; ++component)
        {
            const double difference = std::fabs(gpuValues[component] - referenceValues[component]);
            largestDifference = std::max(largestDifference, difference);
            largestReference = std::max(largestReference, std::fabs(referenceValues[component]));
            worstComponent = std::max(worstComponent, difference / std::max(std::fabs(referenceValues[component]), k_RelativeErrorFloor));
        }

        switch (metric)
        {
        case ErrorMetric::NormRelative:
            return largestDifference / std::max(largestReference, 1.0e-30);
        case ErrorMetric::ComponentRelative:
            return worstComponent;
        case ErrorMetric::ScalarRelative:
            return std::fabs(gpu.X - reference.X) / std::max(std::fabs(reference.X), 1.0e-30);
        default:
            return std::nan("");
        }
    }

    bool WaitUntilResourceIsKnown(const GraphicsResourceReply& reply) noexcept
    {
        // the handles are known once the reply reaches Transferring. the dispatch waits for the upload on the GPU,
        // through the reply's semaphore, so the CPU does not wait for the transfer itself
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

        while (std::chrono::steady_clock::now() < deadline)
        {
            const MessageReply::Status status = reply.GetStatus();

            if (status == MessageReply::Status::Transferring || status == MessageReply::Status::Completed)
            {
                return true;
            }

            if (status == MessageReply::Status::Failed)
            {
                return false;
            }

            std::this_thread::yield();
        }

        return false;
    }

    std::optional<GpuBuffers> CreateBuffers(ResourceContext& resources, std::span<const float> samples)
    {
        const uint64_t sampleBytes = samples.size_bytes();
        GpuBuffers buffers{};

        // the input is device-local, so its upload goes through the transfer system, and the dispatch waits on the
        // reply's semaphore
        const RhiBufferResourceData inputData{ std::as_bytes(samples), 0u };
        BufferCreateInfo inputInfo{};
        inputInfo.Domain = ResourceDomain::GPUOnly;
        inputInfo.Usage = BufferUsageBits::ComputeShaderSRV;
        inputInfo.Size = sampleBytes;
        buffers.InputReply = resources.CreateBuffer(inputInfo, std::span{ &inputData, 1u });

        BufferCreateInfo outputInfo{};
        outputInfo.Domain = ResourceDomain::GPUOnly;
        outputInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        outputInfo.Size = sampleBytes;
        std::vector<std::shared_ptr<GraphicsResourceReply>> outputReplies;

        for (size_t outputIndex = 0; outputIndex < k_Cases.size(); ++outputIndex)
        {
            outputReplies.push_back(resources.CreateBuffer(outputInfo, {}));
        }

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = sampleBytes * k_Cases.size();
        std::shared_ptr<GraphicsResourceReply> readbackReply = resources.CreateBuffer(readbackInfo, {});

        bool created = WaitUntilResourceIsKnown(*buffers.InputReply);
        created = created && readbackReply->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed;

        for (const std::shared_ptr<GraphicsResourceReply>& reply : outputReplies)
        {
            created = created && reply->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed;
        }

        if (!created)
        {
            return std::nullopt;
        }

        buffers.Input = buffers.InputReply->GetResource();
        buffers.Readback = readbackReply->GetResource();

        for (const std::shared_ptr<GraphicsResourceReply>& reply : outputReplies)
        {
            buffers.Outputs.push_back(reply->GetResource());
        }

        return buffers;
    }

    void DestroyBuffers(ResourceContext& resources, const GpuBuffers& buffers)
    {
        std::vector<GraphicsResource> all{ buffers.Input, buffers.Readback };
        all.insert(all.end(), buffers.Outputs.begin(), buffers.Outputs.end());

        for (const GraphicsResource& resource : all)
        {
            if (resource)
            {
                (void)resources.DestroyResource(resource)->WaitForCompletion(k_TimeoutNs);
            }
        }
    }

    bool RecordAndSubmit(rhi::Device& device, const rhi::ManifestProgram& program, VkDescriptorSet descriptor_set, const GpuBuffers& buffers, int32_t sample_count)
    {
        const uint64_t outputBytes = uint64_t{ static_cast<uint32_t>(sample_count) } * 4u * sizeof(float);

        rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(commandPool.AllocateCommandBuffers(1));
        rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());
        RhiAssert(commandBuffer.BindShaders(program.Program()));

        const rhi::DescriptorSetHandle descriptorSetHandle{ reinterpret_cast<uint64_t>(descriptor_set) };
        commandBuffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, program.PipelineLayout(), 0, std::span{ &descriptorSetHandle, 1 });

        const uint32_t workgroupX = program.Workgroup()[0];
        commandBuffer.Dispatch((static_cast<uint32_t>(sample_count) + workgroupX - 1u) / workgroupX, 1, 1);

        const rhi::GlobalBarrier toCopy{ rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite, rhi::PipelineStage::Copy, rhi::Access::TransferRead };
        commandBuffer.Barrier(rhi::DependencyInfo{ std::span{ &toCopy, 1 }, {}, {} });

        for (size_t outputIndex = 0; outputIndex < buffers.Outputs.size(); ++outputIndex)
        {
            const rhi::BufferCopy region{ 0u, outputIndex * outputBytes, outputBytes };
            commandBuffer.CopyBuffer(rhi::BufferHandle{ buffers.Outputs[outputIndex].VkHandle }, rhi::BufferHandle{ buffers.Readback.VkHandle }, std::span{ &region, 1 });
        }

        const rhi::GlobalBarrier toHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
        commandBuffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
        RhiAssert(commandBuffer.End());

        // the input upload runs on the transfer queue, and signals the reply's semaphore when it finishes
        const rhi::SemaphoreSubmitInfo uploadWait{ rhi::TimelineSemaphoreHandle{ buffers.InputReply->SemaphoreHandle() }, k_TransferCompleteSemaphoreValue, rhi::PipelineStage::ComputeShader };
        const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ std::span{ &uploadWait, 1 }, std::span{ &commandBufferHandle, 1 }, {} }, fence.Handle()));

        return fence.Wait();
    }

    int32_t CompareOutputs(std::span<const float> samples, std::span<const float> gpu_outputs)
    {
        const size_t sampleCount = samples.size() / 4u;
        int32_t failures = 0;

        for (size_t caseIndex = 0; caseIndex < k_Cases.size(); ++caseIndex)
        {
            const FunctionCase& testCase = k_Cases[caseIndex];
            const std::span<const float> gpu = gpu_outputs.subspan(caseIndex * sampleCount * 4u, sampleCount * 4u);
            FunctionResult result{};

            for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                const color::Vec3d input{ samples[sampleIndex * 4u], samples[sampleIndex * 4u + 1u], samples[sampleIndex * 4u + 2u] };
                const color::Vec3d gpuValue{ gpu[sampleIndex * 4u], gpu[sampleIndex * 4u + 1u], gpu[sampleIndex * 4u + 2u] };
                const color::Vec3d reference = ReferenceFor(testCase, input);
                const double error = ErrorOf(testCase.Metric, gpuValue, reference);
                result.Finite = result.Finite && std::isfinite(gpuValue.X) && std::isfinite(gpuValue.Y) && std::isfinite(gpuValue.Z);

                // a NaN error never compares greater, so it is caught by the Finite flag and by the negated test
                if (!(error <= result.WorstError))
                {
                    result.WorstError = error;
                    result.WorstIndex = static_cast<int32_t>(sampleIndex);
                }
            }

            const bool passed = result.Finite && result.WorstError <= testCase.Tolerance;
            const size_t worst = static_cast<size_t>(result.WorstIndex);
            (passed ? std::cout : std::cerr) << std::format("[{}] {}: largest error {:.3e}, tolerance {:.0e}, at sample {} ({}, {}, {}){}\n",
                passed ? "pass" : "FAIL", testCase.Name, result.WorstError, testCase.Tolerance, result.WorstIndex,
                samples[worst * 4u], samples[worst * 4u + 1u], samples[worst * 4u + 2u], result.Finite ? "" : ", non-finite output");
            failures += passed ? 0 : 1;
        }

        return failures;
    }

    int32_t RunAll(rhi::RhiSystem& rhi_system, ResourceContext& resources, const rhi::ShaderLibrary& library)
    {
        rhi::Device& device = *rhi_system.GetDevice();
        const VkDevice vkDevice = device.Handle().As<VkDevice>();

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);

        if (!index.has_value())
        {
            std::cerr << std::format("module {} not found: {}\n", k_ModuleName, rhi::ToString(index.error()));
            return 1;
        }

        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = (*index)->Query().SingleFor(k_EntryPoints);

        if (!variant.has_value())
        {
            std::cerr << std::format("variant query failed, code {}, axis '{}'\n", static_cast<int>(variant.error().Code), variant.error().AxisName);
            return 1;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program = library.CreateProgram(device.Handle(), k_ModuleName, *variant, k_EntryPoints);

        if (!program.has_value())
        {
            std::cerr << std::format("program creation failed: {}\n", rhi::ToString(program.error()));
            return 1;
        }

        const std::vector<float> samples = BuildSamples();
        const int32_t sampleCount = static_cast<int32_t>(samples.size() / 4u);
        std::optional<GpuBuffers> buffers = CreateBuffers(resources, samples);

        if (!buffers.has_value())
        {
            std::cerr << "ResourceContext did not create the buffers\n";
            return 1;
        }

        // one storage buffer for the input and one for each output
        const VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(k_Cases.size() + 1u) };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &poolSize };
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool)));

        std::span<const rhi::DescriptorSetLayoutHandle> setLayouts = (*program)->SetLayouts();
        const VkDescriptorSetLayout setLayout = setLayouts[0].As<VkDescriptorSetLayout>();
        const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &setLayout };
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &setInfo, &descriptorSet)));

        BoundResources bound;
        bound.emplace("Input", BoundResource{ buffers->Input });

        for (size_t caseIndex = 0; caseIndex < k_Cases.size(); ++caseIndex)
        {
            bound.emplace(k_Cases[caseIndex].BufferName, BoundResource{ buffers->Outputs[caseIndex] });
        }

        int32_t failures = 0;

        if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, descriptorSet, **index, *variant, k_EntryPoints, bound); error.has_value())
        {
            std::cerr << std::format("WriteManifestDescriptors: {}\n", *error);
            failures = 1;
        }
        else if (!RecordAndSubmit(device, **program, descriptorSet, *buffers, sampleCount))
        {
            std::cerr << "the dispatch did not finish\n";
            failures = 1;
        }
        else
        {
            const uint64_t readbackBytes = samples.size() * sizeof(float) * k_Cases.size();
            std::shared_ptr<PointerMessageReply> mapReply = resources.MapBuffer(buffers->Readback, readbackBytes, 0u);

            if (mapReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << "MapBuffer failed on the readback buffer\n";
                failures = 1;
            }
            else
            {
                const std::span<const float> gpuOutputs{ static_cast<const float*>(mapReply->GetPointer()), samples.size() * k_Cases.size() };
                failures = CompareOutputs(samples, gpuOutputs);

                if (resources.UnmapBuffer(buffers->Readback, readbackBytes, 0u)->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
                {
                    std::cerr << "UnmapBuffer failed\n";
                    ++failures;
                }
            }
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        DestroyBuffers(resources, *buffers);

        return failures;
    }
}
