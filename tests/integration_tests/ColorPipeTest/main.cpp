#include "ColorComposite.hpp"
#include "ColorPipe.hpp"
#include "ColorSpaces.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "DisplayTarget.hpp"
#include "Fence.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "TransferFunctions.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// headless test of color::ColorPipe: composites known patterns into offscreen images of each encoding, and compares
// every pixel with color::CompositePixel

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    // this GPU truncates when it writes RGBA16F, so the error reaches 1 ulp of the half. The 0.01 is slack for the float32
    // error before the conversion
    constexpr double k_CodeTolerance = 1.0;
    constexpr double k_UlpTolerance = 1.01;
    // pixels closer than this (in stops) to a FalseColorEv zone boundary are skipped, because float32 could pick the other zone
    constexpr double k_ZoneBoundaryMargin = 0.01;
    // the outermost boundary that changes the zone is at +-4.5 stops: beyond +-5.5 both sides clamp to the same zone
    constexpr double k_OutermostZoneBoundary = 4.5;
    constexpr uint32_t k_RandomSeed = 0x0C0102C0u;
    constexpr int32_t k_RandomSceneCount = 1500;
    constexpr uint32_t k_ImageWidth = 128u;
    constexpr uint32_t k_ResizedWidth = 96u;
    // a gray ramp step is a third of a stop, so these exposures keep every step more than 0.08 stops from a half stop
    constexpr std::array<float, 3> k_Exposures{ 0.0f, 1.75f, -2.25f };
    constexpr int32_t k_GrayRampCount = 61;
    constexpr double k_SmallestNormalHalf = 6.103515625e-05;
    constexpr double k_SmallestSubnormalHalf = 5.9604644775390625e-08;

    struct EncodingCase
    {
        color::DisplayEncoding Encoding;
        std::string_view Name;
        std::string_view FormatName;
        ImageFormat Format;
        ColorSpace Space;
        uint32_t BytesPerPixel;
        // the largest code of an integer format, and 0 for a float format
        double MaxCode;
        // the value of the 2-bit or 8-bit alpha channel that means 1.0
        double OpaqueAlpha;
    };

    const std::array<EncodingCase, 3> k_Encodings
    {
        EncodingCase{ color::DisplayEncoding::SrgbNonlinear, "sRGB", "B8G8R8A8_UNORM", ImageFormat{ ImageComponentFormats::BGRA8, ImageDataType::UNorm }, ColorSpace::sRGB_Nonlinear, 4u, 255.0, 255.0 },
        EncodingCase{ color::DisplayEncoding::ScRgbLinear, "scRGB", "R16G16B16A16_SFLOAT", ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::Float }, ColorSpace::Extended_sRGB_Linear, 8u, 0.0, 1.0 },
        EncodingCase{ color::DisplayEncoding::Hdr10Pq, "PQ", "A2B10G10R10_UNORM_PACK32", ImageFormat{ ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm }, ColorSpace::HDR10_ST2084, 4u, 1023.0, 3.0 },
    };

    constexpr std::array<color::TonemapperKind, 2> k_Tonemappers{ color::TonemapperKind::Passthrough, color::TonemapperKind::FalseColorEv };

    // scene values are exactly representable as half floats, so that the reference sees what the shader reads
    struct Pattern
    {
        std::vector<color::Vec3d> Scene;
        std::vector<color::UiTexel> Ui;
    };

    // one pixel of the readback, in the unit of its format: codes for UNORM, and values for float
    struct OutputPixel
    {
        std::array<double, 3> Value{};
        double Alpha{ 0.0 };
        bool Finite{ true };
    };

    struct ErrorStats
    {
        double Worst{ 0.0 };
        size_t WorstPixel{ 0u };
        size_t WorstChannel{ 0u };
        double WorstGpu{ 0.0 };
        double WorstReference{ 0.0 };
        size_t Compared{ 0u };
        size_t Excluded{ 0u };
        bool Finite{ true };
        bool AlphaOk{ true };
    };

    // we use this to report the errors of one tonemapper with one encoding
    struct CaseReport
    {
        color::TonemapperKind Tonemapper{ color::TonemapperKind::Invalid };
        const EncodingCase* Encoding{ nullptr };
        std::string Stage;
        ErrorStats SceneOnly;
        ErrorStats WithUi;
        int32_t InvariantRuns{ 0 };
        int32_t InvariantFailures{ 0 };
    };

    struct Runner
    {
        const EncodingCase* Encoding{ nullptr };
        std::unique_ptr<color::ColorPipe> Pipe;
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        uint32_t HalfRows{ 0u };
        GraphicsResource Output;
        GraphicsResource SceneUpload;
        GraphicsResource UiUpload;
        GraphicsResource Readback;
        bool TargetsFilled{ false };
    };

    struct TestContext
    {
        rhi::Device& Device;
        ResourceContext& Resources;
        const rhi::ShaderLibrary& Library;
        const Pattern& PatternData;
    };

    [[nodiscard]] float HalfToFloat(uint16_t value) noexcept;
    [[nodiscard]] uint16_t FloatToHalf(float value) noexcept;
    [[nodiscard]] double HalfUlp(double magnitude) noexcept;
    [[nodiscard]] color::Vec3d RoundToHalf(const color::Vec3d& value) noexcept;
    [[nodiscard]] color::UiTexel MakeUiTexel(size_t index, std::mt19937& random);
    [[nodiscard]] Pattern BuildPattern();
    [[nodiscard]] bool IsFalseColorAmbiguous(const color::Vec3d& scene, double exposure_ev) noexcept;
    [[nodiscard]] DisplayColorCapabilities LgCapabilities() noexcept;
    [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply);
    [[nodiscard]] bool Submit(rhi::Device& device, const std::function<void(rhi::CommandBuffer&)>& record);
    [[nodiscard]] OutputPixel DecodePixel(const EncodingCase& encoding, const std::byte* texel) noexcept;
    [[nodiscard]] double ErrorInUnits(const EncodingCase& encoding, double gpu_value, double reference_encoded, double largest_reference_encoded) noexcept;
    [[nodiscard]] double UnitOf(const EncodingCase& encoding, double reference_encoded) noexcept;
    [[nodiscard]] size_t PatternIndexOf(const Runner& runner, size_t pattern_size, uint32_t x, uint32_t y) noexcept;
    [[nodiscard]] bool PrepareRunnerResources(const TestContext& context, Runner& runner);
    void DestroyRunnerResources(const TestContext& context, Runner& runner);
    [[nodiscard]] std::unique_ptr<Runner> CreateRunner(const TestContext& context, const EncodingCase& encoding);
    void SetImageSize(const TestContext& context, Runner& runner, uint32_t width);
    [[nodiscard]] std::optional<std::vector<OutputPixel>> RunComposite(const TestContext& context, Runner& runner, float exposure_ev);
    void CompareHalf(const TestContext& context, const Runner& runner, std::span<const OutputPixel> pixels, const color::CompositeSettings& settings, float exposure_ev, bool with_ui, ErrorStats& stats);
    [[nodiscard]] int32_t CheckPassthroughInvariants(const TestContext& context, const Runner& runner, std::span<const OutputPixel> pixels, float exposure_ev);
    [[nodiscard]] int32_t RunAndCompare(const TestContext& context, Runner& runner, color::TonemapperKind tonemapper, CaseReport& report);
    [[nodiscard]] int32_t RunAllExposures(const TestContext& context, Runner& runner, std::string_view stage, std::vector<CaseReport>& reports);
    [[nodiscard]] int32_t CheckTarget(const Runner& runner);
    [[nodiscard]] int32_t RunEncoding(const TestContext& context, const EncodingCase& encoding, std::vector<CaseReport>& reports);
    [[nodiscard]] double ToleranceOf(const EncodingCase& encoding) noexcept;
    [[nodiscard]] bool Passed(const EncodingCase& encoding, const ErrorStats& stats) noexcept;
    [[nodiscard]] std::string Describe(const EncodingCase& encoding, const ErrorStats& stats);
    [[nodiscard]] int32_t PrintReports(std::span<const CaseReport> reports);
}

int main()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ColorPipeTest";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(COLOR_PIPE_TEST_MANIFEST);

    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", COLOR_PIPE_TEST_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    const Pattern pattern = BuildPattern();
    std::cout << std::format("pattern: {} scene pixels, {} UI texels, {} exposures\n", pattern.Scene.size(), pattern.Ui.size(), k_Exposures.size());

    int32_t failures = 0;
    std::vector<CaseReport> reports;

    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        const TestContext context{ *rhiSystem->GetDevice(), resources, **library, pattern };

        for (const EncodingCase& encoding : k_Encodings)
        {
            failures += RunEncoding(context, encoding, reports);
        }
    }

    failures += PrintReports(reports);

    if (failures != 0)
    {
        std::cerr << std::format("ColorPipeTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "ColorPipeTest: every pixel of every tonemapper and encoding matches the CPU reference\n";
    return 0;
}

namespace
{
    float HalfToFloat(uint16_t value) noexcept
    {
        const uint32_t sign = (value & 0x8000u) << 16u;
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;

        if (exponent == 0u)
        {
            const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
            return sign != 0u ? -magnitude : magnitude;
        }

        if (exponent == 31u)
        {
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13u));
        }

        return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
    }

    // round to nearest even
    uint16_t FloatToHalf(float value) noexcept
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
        const float magnitude = std::fabs(value);

        if (magnitude < static_cast<float>(k_SmallestNormalHalf))
        {
            // denormal: round to the nearest multiple of 2^-24
            return static_cast<uint16_t>(sign | static_cast<uint16_t>(std::nearbyint(magnitude * 16777216.0f)));
        }

        const uint32_t magnitudeBits = bits & 0x7FFFFFFFu;
        const uint32_t rounded = magnitudeBits + 0x0FFFu + ((magnitudeBits >> 13u) & 1u);

        return static_cast<uint16_t>(sign | ((rounded - (112u << 23u)) >> 13u));
    }

    double HalfUlp(double magnitude) noexcept
    {
        if (magnitude < k_SmallestNormalHalf)
        {
            return k_SmallestSubnormalHalf;
        }

        return std::exp2(std::floor(std::log2(magnitude)) - 10.0);
    }

    color::Vec3d RoundToHalf(const color::Vec3d& value) noexcept
    {
        return color::Vec3d{
            HalfToFloat(FloatToHalf(static_cast<float>(value.X))),
            HalfToFloat(FloatToHalf(static_cast<float>(value.Y))),
            HalfToFloat(FloatToHalf(static_cast<float>(value.Z))) };
    }

    // includes bytes that Dear ImGui could not write: a color above its alpha, and a color under an alpha of 0
    color::UiTexel MakeUiTexel(size_t index, std::mt19937& random)
    {
        constexpr std::array<color::UiTexel, 5> k_Specials
        {
            color::UiTexel{ 0, 0, 0, 0 },
            color::UiTexel{ 255, 255, 255, 255 },
            color::UiTexel{ 128, 128, 128, 128 },
            color::UiTexel{ 255, 0, 0, 255 },
            color::UiTexel{ 0, 0, 100, 100 },
        };

        std::uniform_int_distribution<int32_t> byte(0, 255);
        std::uniform_int_distribution<int32_t> partialAlpha(1, 254);
        std::uniform_int_distribution<int32_t> lowAlpha(1, 8);

        const auto premultiplied = [&](int32_t alpha)
        {
            const auto channel = [&](int32_t straight) { return static_cast<uint8_t>(std::lround(straight * alpha / 255.0)); };
            const int32_t red = byte(random);
            const int32_t green = byte(random);
            const int32_t blue = byte(random);

            return color::UiTexel{ channel(red), channel(green), channel(blue), static_cast<uint8_t>(alpha) };
        };

        switch (index % 8u)
        {
        case 0:
            // no alpha, so the color bytes must not matter
            return color::UiTexel{ static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), 0 };
        case 1:
            return color::UiTexel{ static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), 255 };
        case 2:
        case 3:
        case 4:
            return premultiplied(partialAlpha(random));
        case 5:
            return color::UiTexel{ static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)), static_cast<uint8_t>(byte(random)) };
        case 6:
            return premultiplied(lowAlpha(random));
        default:
            return k_Specials[(index / 8u) % k_Specials.size()];
        }
    }

    Pattern BuildPattern()
    {
        Pattern pattern;
        std::mt19937 random(k_RandomSeed);

        const auto addScene = [&pattern](const color::Vec3d& value)
        {
            pattern.Scene.push_back(RoundToHalf(value));
        };

        // gray ramp, -10 EV to +10 EV in thirds of a stop
        for (int32_t step = 0; step < k_GrayRampCount; ++step)
        {
            const double ev = -10.0 + static_cast<double>(step) / 3.0;
            const double value = color::k_MiddleGray * std::exp2(ev);
            addScene(color::Vec3d{ value, value, value });
        }

        // AP1 and Rec.709 primaries and secondaries at six exposures. The AP1 ones make negative values in Rec.709 targets
        const color::Mat3d rec709ToAp1 = color::RgbToRgb(color::Rec709, color::AP1, color::ChromaticAdaptation::Bradford);
        constexpr std::array<color::Vec3d, 6> k_Hues
        {
            color::Vec3d{ 1.0, 0.0, 0.0 }, color::Vec3d{ 0.0, 1.0, 0.0 }, color::Vec3d{ 0.0, 0.0, 1.0 },
            color::Vec3d{ 0.0, 1.0, 1.0 }, color::Vec3d{ 1.0, 0.0, 1.0 }, color::Vec3d{ 1.0, 1.0, 0.0 },
        };
        constexpr std::array<double, 6> k_HueExposures{ -6.0, -4.0, -2.0, 0.0, 2.0, 4.0 };

        for (const bool rec709 : { false, true })
        {
            for (const color::Vec3d& hue : k_Hues)
            {
                for (const double exposure : k_HueExposures)
                {
                    const double scale = color::k_MiddleGray * std::exp2(exposure);
                    const color::Vec3d scaled{ hue.X * scale, hue.Y * scale, hue.Z * scale };
                    addScene(rec709 ? color::Multiply(rec709ToAp1, scaled) : scaled);
                }
            }
        }

        // far above the headroom of the LG (1.72), including the brightest finite half float
        for (const double value : { 2.0, 5.0, 10.0, 100.0, 1000.0, 60000.0 })
        {
            addScene(color::Vec3d{ value, value, value });
        }

        constexpr std::array<color::Vec3d, 12> k_Extremes
        {
            color::Vec3d{ 50.0, 5.0, 0.1 }, color::Vec3d{ 0.1, 30.0, 60.0 }, color::Vec3d{ 8.0, 8.0, 0.01 }, color::Vec3d{ 0.001, 0.001, 40.0 },
            // black, and almost black
            color::Vec3d{ 0.0, 0.0, 0.0 }, color::Vec3d{ 1.0e-6, 1.0e-6, 1.0e-6 },
            // a negative component, which wide-gamut color math makes
            color::Vec3d{ -0.1, 0.5, 0.2 }, color::Vec3d{ 0.2, -0.05, 0.3 }, color::Vec3d{ 0.3, 0.2, -0.4 },
            color::Vec3d{ 1.0, 0.0, 0.0 }, color::Vec3d{ 0.0, 1.0, 0.0 }, color::Vec3d{ 0.0, 0.0, 1.0 },
        };

        for (const color::Vec3d& value : k_Extremes)
        {
            addScene(value);
        }

        std::uniform_real_distribution<double> logarithm(std::log(1.0e-3), std::log(10.0));

        for (int32_t sample = 0; sample < k_RandomSceneCount; ++sample)
        {
            addScene(color::Vec3d{ std::exp(logarithm(random)), std::exp(logarithm(random)), std::exp(logarithm(random)) });
        }

        for (size_t index = 0; index < pattern.Scene.size(); ++index)
        {
            pattern.Ui.push_back(MakeUiTexel(index, random));
        }

        return pattern;
    }

    bool IsFalseColorAmbiguous(const color::Vec3d& scene, double exposure_ev) noexcept
    {
        const color::Vec3d exposed = color::ApplyExposure(scene, exposure_ev);
        const double ev = color::FalseColorEvOf(color::Luminance(color::AP1, exposed));

        if (std::fabs(ev) > k_OutermostZoneBoundary + k_ZoneBoundaryMargin)
        {
            return false;
        }

        const double shifted = ev + 0.5;

        return std::fabs(shifted - std::round(shifted)) < k_ZoneBoundaryMargin;
    }

    // DXGI GetDesc1 and DisplayConfig of the LG ULTRAGEAR+: 0.035 to 600 nits, and an SDR white level of 348 nits
    DisplayColorCapabilities LgCapabilities() noexcept
    {
        DisplayColorCapabilities capabilities{};
        capabilities.hdrSupported = true;
        capabilities.hdrEnabled = true;
        capabilities.sdrWhiteLevel = 348.0f;
        capabilities.MaxLuminance = 600.0f;
        capabilities.MinLuminance = 0.035f;

        return capabilities;
    }

    GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply)
    {
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return GraphicsResource::Null();
        }

        return reply->GetResource();
    }

    bool Submit(rhi::Device& device, const std::function<void(rhi::CommandBuffer&)>& record)
    {
        rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Graphics, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(commandPool.AllocateCommandBuffers(1));
        rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());
        record(commandBuffer);
        RhiAssert(commandBuffer.End());

        const rhi::CommandBufferHandle handle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()));

        return fence.Wait(k_TimeoutNs);
    }

    OutputPixel DecodePixel(const EncodingCase& encoding, const std::byte* texel) noexcept
    {
        OutputPixel pixel{};

        if (encoding.Encoding == color::DisplayEncoding::SrgbNonlinear)
        {
            // B8G8R8A8: the bytes are blue, green, red, alpha
            pixel.Value = { static_cast<double>(texel[2]), static_cast<double>(texel[1]), static_cast<double>(texel[0]) };
            pixel.Alpha = static_cast<double>(texel[3]);

            return pixel;
        }

        if (encoding.Encoding == color::DisplayEncoding::Hdr10Pq)
        {
            // A2B10G10R10: red in the lowest 10 bits, then green, then blue, and alpha in the top 2
            uint32_t word = 0u;
            std::memcpy(&word, texel, sizeof(word));
            pixel.Value = { static_cast<double>(word & 0x3FFu), static_cast<double>((word >> 10u) & 0x3FFu), static_cast<double>((word >> 20u) & 0x3FFu) };
            pixel.Alpha = static_cast<double>(word >> 30u);

            return pixel;
        }

        std::array<uint16_t, 4> halves{};
        std::memcpy(halves.data(), texel, sizeof(halves));
        pixel.Value = { HalfToFloat(halves[0]), HalfToFloat(halves[1]), HalfToFloat(halves[2]) };
        pixel.Alpha = HalfToFloat(halves[3]);

        // exponent 31 is an infinity or a NaN
        for (const uint16_t half : halves)
        {
            pixel.Finite = pixel.Finite && ((half >> 10u) & 0x1Fu) != 0x1Fu;
        }

        return pixel;
    }

    double UnitOf(const EncodingCase& encoding, double reference_encoded) noexcept
    {
        return encoding.MaxCode > 0.0 ? 1.0 : HalfUlp(std::fabs(reference_encoded));
    }

    // the float unit is the ulp at the largest channel of the pixel, because a small result of large terms has the float32
    // error of the terms
    double ErrorInUnits(const EncodingCase& encoding, double gpu_value, double reference_encoded, double largest_reference_encoded) noexcept
    {
        const double reference = encoding.MaxCode > 0.0 ? reference_encoded * encoding.MaxCode : reference_encoded;

        return std::fabs(gpu_value - reference) / UnitOf(encoding, largest_reference_encoded);
    }

    size_t PatternIndexOf(const Runner& runner, size_t pattern_size, uint32_t x, uint32_t y) noexcept
    {
        const size_t rowInHalf = y % runner.HalfRows;

        return (rowInHalf * runner.Width + x) % pattern_size;
    }
}
namespace
{
    bool PrepareRunnerResources(const TestContext& context, Runner& runner)
    {
        DestroyRunnerResources(context, runner);

        const size_t pixelCount = size_t{ runner.Width } * runner.Height;
        const size_t patternSize = context.PatternData.Scene.size();
        std::vector<uint16_t> sceneHalves(pixelCount * 4u);
        std::vector<uint8_t> uiBytes(pixelCount * 4u);

        for (uint32_t y = 0u; y < runner.Height; ++y)
        {
            for (uint32_t x = 0u; x < runner.Width; ++x)
            {
                const size_t index = PatternIndexOf(runner, patternSize, x, y);
                const size_t texel = (size_t{ y } * runner.Width + x) * 4u;
                const color::Vec3d& scene = context.PatternData.Scene[index];
                const bool withUi = y >= runner.HalfRows;
                const color::UiTexel ui = withUi ? context.PatternData.Ui[index] : color::UiTexel{ 0, 0, 0, 0 };

                // the half of 1.0
                constexpr uint16_t k_HalfOne = 0x3C00u;
                sceneHalves[texel + 0u] = FloatToHalf(static_cast<float>(scene.X));
                sceneHalves[texel + 1u] = FloatToHalf(static_cast<float>(scene.Y));
                sceneHalves[texel + 2u] = FloatToHalf(static_cast<float>(scene.Z));
                sceneHalves[texel + 3u] = k_HalfOne;
                std::ranges::copy(ui, uiBytes.begin() + static_cast<std::ptrdiff_t>(texel));
            }
        }

        BufferCreateInfo sceneInfo{};
        sceneInfo.Domain = ResourceDomain::CPUToGPU;
        sceneInfo.Usage = BufferUsageBits::CopySource;
        sceneInfo.Size = sceneHalves.size() * sizeof(uint16_t);
        const RhiBufferResourceData sceneData{ std::as_bytes(std::span{ sceneHalves }), 0u };
        runner.SceneUpload = WaitForResource(context.Resources.CreateBuffer(sceneInfo, std::span{ &sceneData, 1 }));

        BufferCreateInfo uiInfo{};
        uiInfo.Domain = ResourceDomain::CPUToGPU;
        uiInfo.Usage = BufferUsageBits::CopySource;
        uiInfo.Size = uiBytes.size();
        const RhiBufferResourceData uiData{ std::as_bytes(std::span{ uiBytes }), 0u };
        runner.UiUpload = WaitForResource(context.Resources.CreateBuffer(uiInfo, std::span{ &uiData, 1 }));

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = pixelCount * runner.Encoding->BytesPerPixel;
        runner.Readback = WaitForResource(context.Resources.CreateBuffer(readbackInfo, {}));

        ImageCreateInfo outputInfo{};
        outputInfo.Domain = ResourceDomain::GPUOnly;
        outputInfo.Usage = ImageUsageBits::RenderTargetWrite | ImageUsageBits::TransferSource;
        outputInfo.Type = ImageType::Type2D;
        outputInfo.Format = runner.Encoding->Format;
        outputInfo.Width = runner.Width;
        outputInfo.Height = runner.Height;
        outputInfo.Depth = 1u;
        outputInfo.ViewInfo = ImageViewCreateInfo{};
        outputInfo.ViewInfo->ViewType = ImageViewType::Type2D;
        outputInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
        runner.Output = WaitForResource(context.Resources.CreateImage(outputInfo, {}));

        runner.TargetsFilled = false;

        return runner.SceneUpload && runner.UiUpload && runner.Readback && runner.Output;
    }

    void DestroyRunnerResources(const TestContext& context, Runner& runner)
    {
        for (GraphicsResource* resource : { &runner.Output, &runner.SceneUpload, &runner.UiUpload, &runner.Readback })
        {
            if (*resource)
            {
                (void)context.Resources.DestroyResource(*resource)->WaitForCompletion(k_TimeoutNs);
                *resource = GraphicsResource{};
            }
        }
    }

    void SetImageSize(const TestContext& context, Runner& runner, uint32_t width)
    {
        const size_t patternSize = context.PatternData.Scene.size();

        runner.Width = width;
        runner.HalfRows = static_cast<uint32_t>((patternSize + width - 1u) / width);
        runner.Height = 2u * runner.HalfRows;
    }

    std::unique_ptr<Runner> CreateRunner(const TestContext& context, const EncodingCase& encoding)
    {
        auto runner = std::make_unique<Runner>();
        runner->Encoding = &encoding;
        SetImageSize(context, *runner, k_ImageWidth);

        color::ColorPipeCreateInfo createInfo{};
        createInfo.Device = &context.Device;
        createInfo.Resources = &context.Resources;
        createInfo.Library = &context.Library;
        createInfo.Width = runner->Width;
        createInfo.Height = runner->Height;
        createInfo.Capabilities = LgCapabilities();
        createInfo.Surface = color::SurfaceChoice{ encoding.Format, encoding.Space, encoding.Encoding };
        createInfo.Tonemapper = color::TonemapperKind::Passthrough;

        color::ColorPipeResult<std::unique_ptr<color::ColorPipe>> pipe = color::ColorPipe::Create(createInfo);

        if (!pipe.has_value())
        {
            std::cerr << std::format("[{}] ColorPipe::Create failed: {}\n", encoding.Name, color::ToString(pipe.error()));
            return nullptr;
        }

        runner->Pipe = std::move(*pipe);

        if (!PrepareRunnerResources(context, *runner))
        {
            std::cerr << std::format("[{}] creating the output image or the transfer buffers failed\n", encoding.Name);
            return nullptr;
        }

        return runner;
    }

    std::optional<std::vector<OutputPixel>> RunComposite(const TestContext& context, Runner& runner, float exposure_ev)
    {
        const EncodingCase& encoding = *runner.Encoding;
        runner.Pipe->SetExposureEv(exposure_ev);

        const color::ColorTargetView scene = runner.Pipe->SceneTarget();
        const color::ColorTargetView ui = runner.Pipe->UiTarget();
        const rhi::ImageHandle outputImage{ runner.Output.VkHandle };
        const rhi::ImageViewHandle outputView{ runner.Output.VkViewHandle };
        constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0u, 1u, 0u, 1u };
        const bool fill = !runner.TargetsFilled;

        rhi::BufferImageCopy wholeImage{};
        wholeImage.Width = runner.Width;
        wholeImage.Height = runner.Height;

        const bool finished = Submit(context.Device, [&](rhi::CommandBuffer& command_buffer)
        {
            // the targets keep their pixels from run to run, so they are filled once
            if (fill)
            {
                const std::array<rhi::ImageBarrier, 2> toCopy
                {
                    rhi::ImageBarrier{ scene.Image, rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite, rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
                        scene.Layout, rhi::ImageLayout::TransferDestination, k_ColorRange },
                    rhi::ImageBarrier{ ui.Image, rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite, rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
                        ui.Layout, rhi::ImageLayout::TransferDestination, k_ColorRange },
                };
                command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, toCopy });

                command_buffer.CopyBufferToImage(rhi::BufferHandle{ runner.SceneUpload.VkHandle }, scene.Image, rhi::ImageLayout::TransferDestination, std::span{ &wholeImage, 1 });
                command_buffer.CopyBufferToImage(rhi::BufferHandle{ runner.UiUpload.VkHandle }, ui.Image, rhi::ImageLayout::TransferDestination, std::span{ &wholeImage, 1 });

                const std::array<rhi::ImageBarrier, 2> toResting
                {
                    rhi::ImageBarrier{ scene.Image, rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
                        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                        rhi::ImageLayout::TransferDestination, scene.Layout, k_ColorRange },
                    rhi::ImageBarrier{ ui.Image, rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
                        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
                        rhi::ImageLayout::TransferDestination, ui.Layout, k_ColorRange },
                };
                command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, toResting });
            }

            // the pass writes every pixel of the output, so its old contents do not matter
            const rhi::ImageBarrier toAttachment{ outputImage, rhi::PipelineStage::None, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment, k_ColorRange };
            command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toAttachment, 1 } });

            runner.Pipe->RecordComposite(command_buffer, outputView, nullptr);

            const rhi::ImageBarrier toCopySource{ outputImage, rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::Copy, rhi::Access::TransferRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::TransferSource, k_ColorRange };
            command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toCopySource, 1 } });
            command_buffer.CopyImageToBuffer(outputImage, rhi::ImageLayout::TransferSource, rhi::BufferHandle{ runner.Readback.VkHandle }, std::span{ &wholeImage, 1 });

            const rhi::GlobalBarrier toHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
            command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
        });

        if (!finished)
        {
            std::cerr << std::format("[{}] the composite did not finish\n", encoding.Name);
            return std::nullopt;
        }

        runner.TargetsFilled = true;

        const size_t pixelCount = size_t{ runner.Width } * runner.Height;
        const uint64_t readbackBytes = pixelCount * encoding.BytesPerPixel;
        std::shared_ptr<PointerMessageReply> mapReply = context.Resources.MapBuffer(runner.Readback, readbackBytes, 0u);

        if (mapReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("[{}] MapBuffer failed on the readback buffer\n", encoding.Name);
            return std::nullopt;
        }

        const std::byte* mapped = static_cast<const std::byte*>(mapReply->GetPointer());
        std::vector<OutputPixel> pixels;
        pixels.reserve(pixelCount);

        for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            pixels.push_back(DecodePixel(encoding, mapped + pixel * encoding.BytesPerPixel));
        }

        if (context.Resources.UnmapBuffer(runner.Readback, readbackBytes, 0u)->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("[{}] UnmapBuffer failed\n", encoding.Name);
            return std::nullopt;
        }

        return pixels;
    }

    void CompareHalf(const TestContext& context, const Runner& runner, std::span<const OutputPixel> pixels, const color::CompositeSettings& settings, float exposure_ev, bool with_ui, ErrorStats& stats)
    {
        const EncodingCase& encoding = *runner.Encoding;
        const size_t patternSize = context.PatternData.Scene.size();
        const uint32_t firstRow = with_ui ? runner.HalfRows : 0u;

        for (uint32_t row = 0u; row < runner.HalfRows; ++row)
        {
            for (uint32_t x = 0u; x < runner.Width; ++x)
            {
                const uint32_t y = firstRow + row;
                const size_t index = PatternIndexOf(runner, patternSize, x, y);
                const color::Vec3d& scene = context.PatternData.Scene[index];
                const color::UiTexel ui = with_ui ? context.PatternData.Ui[index] : color::UiTexel{ 0, 0, 0, 0 };

                if (settings.Tonemapper == color::TonemapperKind::FalseColorEv && IsFalseColorAmbiguous(scene, exposure_ev))
                {
                    ++stats.Excluded;
                    continue;
                }

                const size_t pixelIndex = size_t{ y } * runner.Width + x;
                const OutputPixel& gpu = pixels[pixelIndex];
                const color::Vec3d reference = color::CompositePixel(settings, scene, ui, exposure_ev);
                const std::array<double, 3> expected{ reference.X, reference.Y, reference.Z };
                const double largest = std::max({ std::fabs(reference.X), std::fabs(reference.Y), std::fabs(reference.Z) });

                ++stats.Compared;
                stats.AlphaOk = stats.AlphaOk && gpu.Alpha == encoding.OpaqueAlpha;
                stats.Finite = stats.Finite && gpu.Finite;

                for (size_t channel = 0u; channel < 3u; ++channel)
                {
                    const double error = ErrorInUnits(encoding, gpu.Value[channel], expected[channel], largest);

                    // a NaN error never compares less, so it is caught here and the worst error stays a number
                    if (std::isnan(error) || !std::isfinite(gpu.Value[channel]))
                    {
                        stats.Finite = false;
                    }
                    else if (error > stats.Worst)
                    {
                        stats.Worst = error;
                        stats.WorstPixel = pixelIndex;
                        stats.WorstChannel = channel;
                        stats.WorstGpu = gpu.Value[channel];
                        stats.WorstReference = encoding.MaxCode > 0.0 ? expected[channel] * encoding.MaxCode : expected[channel];
                    }
                }
            }
        }
    }

    int32_t CheckPassthroughInvariants(const TestContext& context, const Runner& runner, std::span<const OutputPixel> pixels, float exposure_ev)
    {
        (void)context;
        const EncodingCase& encoding = *runner.Encoding;
        const color::DisplayTarget& target = runner.Pipe->Target();
        int32_t failures = 0;

        for (int32_t step = 0; step < k_GrayRampCount; ++step)
        {
            const uint32_t x = static_cast<uint32_t>(step) % runner.Width;
            const uint32_t y = static_cast<uint32_t>(step) / runner.Width;
            const OutputPixel& pixel = pixels[size_t{ y } * runner.Width + x];
            const double unit = UnitOf(encoding, encoding.MaxCode > 0.0 ? pixel.Value[1] / encoding.MaxCode : pixel.Value[1]);

            if (std::fabs(pixel.Value[0] - pixel.Value[1]) > ToleranceOf(encoding) * unit || std::fabs(pixel.Value[2] - pixel.Value[1]) > ToleranceOf(encoding) * unit)
            {
                std::cerr << std::format("[{}] gray ramp step {} at {} EV is not gray: ({}, {}, {})\n", encoding.Name, step, exposure_ev,
                    pixel.Value[0], pixel.Value[1], pixel.Value[2]);
                ++failures;
            }

            if (step > 0)
            {
                const uint32_t previousX = static_cast<uint32_t>(step - 1) % runner.Width;
                const uint32_t previousY = static_cast<uint32_t>(step - 1) / runner.Width;
                const OutputPixel& previous = pixels[size_t{ previousY } * runner.Width + previousX];

                for (size_t channel = 0u; channel < 3u; ++channel)
                {
                    if (pixel.Value[channel] < previous.Value[channel])
                    {
                        std::cerr << std::format("[{}] gray ramp goes down at step {} ({} EV), channel {}: {} after {}\n", encoding.Name, step, exposure_ev,
                            channel, pixel.Value[channel], previous.Value[channel]);
                        ++failures;
                    }
                }
            }
        }

        // the largest value that a channel may have: the encoded peak, plus one unit
        const double peakNits = target.PeakNits;
        const double peakEncoded = color::EncodeForDisplay(color::Vec3d{ peakNits, peakNits, peakNits }, target).X;
        const double peakInUnits = encoding.MaxCode > 0.0 ? peakEncoded * encoding.MaxCode : peakEncoded;
        const double limit = peakInUnits + ToleranceOf(encoding) * UnitOf(encoding, peakEncoded);
        int32_t overPeak = 0;
        int32_t notFinite = 0;

        for (uint32_t y = 0u; y < runner.HalfRows; ++y)
        {
            for (uint32_t x = 0u; x < runner.Width; ++x)
            {
                const OutputPixel& pixel = pixels[size_t{ y } * runner.Width + x];
                notFinite += pixel.Finite ? 0 : 1;

                for (const double value : pixel.Value)
                {
                    overPeak += value > limit ? 1 : 0;
                }
            }
        }

        if (overPeak != 0 || notFinite != 0)
        {
            std::cerr << std::format("[{}] {} channels above the peak ({} nits, limit {}), {} pixels not finite, at {} EV\n", encoding.Name, overPeak,
                peakNits, limit, notFinite, exposure_ev);
            ++failures;
        }

        return failures;
    }

    int32_t RunAndCompare(const TestContext& context, Runner& runner, color::TonemapperKind tonemapper, CaseReport& report)
    {
        int32_t failures = 0;

        if (runner.Pipe->SetTonemapper(tonemapper) != color::ColorPipeError::Success)
        {
            std::cerr << std::format("[{}] SetTonemapper failed\n", runner.Encoding->Name);
            return 1;
        }

        const color::CompositeSettings settings{ runner.Pipe->Target(), tonemapper };

        for (const float exposure : k_Exposures)
        {
            std::optional<std::vector<OutputPixel>> pixels = RunComposite(context, runner, exposure);

            if (!pixels.has_value())
            {
                ++failures;
                continue;
            }

            CompareHalf(context, runner, *pixels, settings, exposure, false, report.SceneOnly);
            CompareHalf(context, runner, *pixels, settings, exposure, true, report.WithUi);

            if (tonemapper == color::TonemapperKind::Passthrough)
            {
                const int32_t invariantFailures = CheckPassthroughInvariants(context, runner, *pixels, exposure);
                ++report.InvariantRuns;
                report.InvariantFailures += invariantFailures;
                failures += invariantFailures;
            }
        }

        return failures;
    }

    int32_t RunAllExposures(const TestContext& context, Runner& runner, std::string_view stage, std::vector<CaseReport>& reports)
    {
        int32_t failures = 0;

        for (const color::TonemapperKind tonemapper : k_Tonemappers)
        {
            CaseReport& report = reports.emplace_back();
            report.Tonemapper = tonemapper;
            report.Encoding = runner.Encoding;
            report.Stage = std::string{ stage };
            failures += RunAndCompare(context, runner, tonemapper, report);
        }

        return failures;
    }

    int32_t CheckTarget(const Runner& runner)
    {
        const color::DisplayTarget& target = runner.Pipe->Target();
        const bool sdr = runner.Encoding->Encoding == color::DisplayEncoding::SrgbNonlinear;
        const float expectedPeak = sdr ? 348.0f : 600.0f;
        const double expectedRedX = runner.Encoding->Encoding == color::DisplayEncoding::Hdr10Pq ? color::Rec2020.Red.X : color::Rec709.Red.X;

        const bool correct = target.PaperWhiteNits == 348.0f && target.PeakNits == expectedPeak && target.BlackNits == 0.035f &&
            target.Encoding == runner.Encoding->Encoding && target.Primaries.Red.X == expectedRedX;

        if (!correct)
        {
            std::cerr << std::format("[{}] the display target is wrong: paper white {}, peak {}, black {}\n", runner.Encoding->Name,
                target.PaperWhiteNits, target.PeakNits, target.BlackNits);
        }

        return correct ? 0 : 1;
    }

    int32_t RunEncoding(const TestContext& context, const EncodingCase& encoding, std::vector<CaseReport>& reports)
    {
        std::unique_ptr<Runner> runner = CreateRunner(context, encoding);

        if (runner == nullptr)
        {
            return 1;
        }

        int32_t failures = CheckTarget(*runner);
        failures += RunAllExposures(context, *runner, "initial", reports);

        runner->Pipe->SetOverrides(color::DisplayOverrides{ 1000.0f, 0.0f, 200.0f });
        failures += RunAllExposures(context, *runner, "after SetOverrides (peak 1000, black 0, paper white 200)", reports);

        // the device is idle, as the swapchain recreation needs
        context.Device.WaitDeviceIdle();
        SetImageSize(context, *runner, k_ResizedWidth);

        if (runner->Pipe->Resize(runner->Width, runner->Height) != color::ColorPipeError::Success || !PrepareRunnerResources(context, *runner))
        {
            std::cerr << std::format("[{}] Resize failed\n", encoding.Name);
            ++failures;
        }
        else
        {
            failures += RunAllExposures(context, *runner, "after Resize", reports);
        }

        context.Device.WaitDeviceIdle();
        DestroyRunnerResources(context, *runner);
        runner.reset();

        return failures;
    }

    double ToleranceOf(const EncodingCase& encoding) noexcept
    {
        return encoding.MaxCode > 0.0 ? k_CodeTolerance : k_UlpTolerance;
    }

    bool Passed(const EncodingCase& encoding, const ErrorStats& stats) noexcept
    {
        return stats.Finite && stats.AlphaOk && stats.Compared > 0u && stats.Worst <= ToleranceOf(encoding);
    }

    std::string Describe(const EncodingCase& encoding, const ErrorStats& stats)
    {
        return std::format("{:.3f} {} (pixel {} channel {}: gpu {:.7g}, reference {:.7g}; {} compared, {} excluded{}{})", stats.Worst,
            encoding.MaxCode > 0.0 ? "codes" : "ulp", stats.WorstPixel, stats.WorstChannel, stats.WorstGpu, stats.WorstReference, stats.Compared,
            stats.Excluded, stats.Finite ? "" : ", NOT FINITE", stats.AlphaOk ? "" : ", WRONG ALPHA");
    }

    int32_t PrintReports(std::span<const CaseReport> reports)
    {
        int32_t failures = 0;
        std::cout << std::format("\nlargest error of each run, limit {} code (UNORM) or {} ulp (float):\n", k_CodeTolerance, k_UlpTolerance);

        for (const CaseReport& report : reports)
        {
            const bool passed = Passed(*report.Encoding, report.SceneOnly) && Passed(*report.Encoding, report.WithUi);
            (passed ? std::cout : std::cerr) << std::format("[{}] {} {} ({}), {}: scene only {}; with UI {}\n", passed ? "pass" : "FAIL",
                report.Tonemapper == color::TonemapperKind::Passthrough ? "Passthrough " : "FalseColorEv", report.Encoding->Name, report.Encoding->FormatName,
                report.Stage, Describe(*report.Encoding, report.SceneOnly), Describe(*report.Encoding, report.WithUi));
            failures += passed ? 0 : 1;
        }

        std::cout << "\nlargest error over all runs, in the unit of the format (codes for UNORM, ulp for float):\n";
        std::cout << std::format("{:<14}{:<10}{:<28}{:>12}{:>12}\n", "tonemapper", "encoding", "format", "scene only", "with UI");

        for (const color::TonemapperKind tonemapper : k_Tonemappers)
        {
            for (const EncodingCase& encoding : k_Encodings)
            {
                double sceneOnly = 0.0;
                double withUi = 0.0;

                for (const CaseReport& report : reports)
                {
                    if (report.Tonemapper == tonemapper && report.Encoding == &encoding)
                    {
                        sceneOnly = std::max(sceneOnly, report.SceneOnly.Worst);
                        withUi = std::max(withUi, report.WithUi.Worst);
                    }
                }

                std::cout << std::format("{:<14}{:<10}{:<28}{:>12.4f}{:>12.4f}\n",
                    tonemapper == color::TonemapperKind::Passthrough ? "Passthrough" : "FalseColorEv", encoding.Name, encoding.FormatName, sceneOnly, withUi);
            }
        }

        int32_t invariantRuns = 0;
        int32_t invariantFailures = 0;

        for (const CaseReport& report : reports)
        {
            invariantRuns += report.InvariantRuns;
            invariantFailures += report.InvariantFailures;
        }

        std::cout << std::format("\nPassthrough invariants (a gray stays gray, the gray ramp never goes down, no channel is above the peak, nothing is NaN): {} runs, {} failures\n\n",
            invariantRuns, invariantFailures);

        return failures;
    }
}
