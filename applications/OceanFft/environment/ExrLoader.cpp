#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <numbers>
#include <string>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "ExrLoader.hpp"
#include "math/Math.hpp"
#include "OceanSimulation.hpp"

#if __has_include("ColorSpaces.hpp")
#include "ColorSpaces.hpp"
#else
#include "../../../modules/ColorPipe/include/ColorSpaces.hpp"
#endif

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../../third_party/basis_universal/encoder/basisu_miniz.h"

#define TINYEXR_USE_MINIZ (0)
#define TINYEXR_USE_THREAD (1)

#define Z_OK buminiz::MZ_OK
#define uLong buminiz::mz_ulong
#define uLongf buminiz::mz_ulong

typedef unsigned char Byte;
typedef Byte Bytef;

#define compressBound   buminiz::mz_compressBound
#define compress        buminiz::mz_compress
#define uncompress      buminiz::mz_uncompress

#ifdef _MSC_VER
#pragma warning (disable: 4060 4100 4245 4505 4702 4530)
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define TINYEXR_IMPLEMENTATION
#include "../../../third_party/basis_universal/encoder/3rdparty/tinyexr.h"


namespace
{
    constexpr float k_Pi = 3.14159265358979323846f;
}

namespace ocean
{
    std::array<float, 3> EvaluateShIrradiance(
        const SphericalHarmonicsL2& sh,
        const std::array<float, 3>& normal) noexcept
    {
        const float x = normal[0];
        const float y = normal[1];
        const float z = normal[2];

        const float Y0 = 0.28209479f;
        const float Y1 = 0.48860251f * y;
        const float Y2 = 0.48860251f * z;
        const float Y3 = 0.48860251f * x;
        const float Y4 = 1.0925484f * x * y;
        const float Y5 = 1.0925484f * y * z;
        const float Y6 = 0.31539156f * (3.0f * z * z - 1.0f);
        const float Y7 = 1.0925484f * x * z;
        const float Y8 = 0.5462742f * (x * x - y * y);

        const std::array<float, 9> Y{ Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8 };

        std::array<float, 3> result{ 0.0f, 0.0f, 0.0f };
        for (size_t i = 0; i < 9; ++i)
        {
            result[0] += sh[i][0] * Y[i];
            result[1] += sh[i][1] * Y[i];
            result[2] += sh[i][2] * Y[i];
        }

        result[0] = std::max(0.0f, result[0]);
        result[1] = std::max(0.0f, result[1]);
        result[2] = std::max(0.0f, result[2]);
        return result;
    }

    std::expected<ExrImageResult, std::string> LoadExrEnvironmentMap(
        const std::filesystem::path& path,
        const ExrLoadOptions& options)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return std::unexpected(std::format("EXR skydome file not found: '{}'", path.string()));
        }

        float* outRgba = nullptr;
        int rawWidth = 0;
        int rawHeight = 0;
        const char* err = nullptr;

        const std::string pathStr = path.string();
        const int ret = LoadEXR(&outRgba, &rawWidth, &rawHeight, pathStr.c_str(), &err);

        if (ret != TINYEXR_SUCCESS)
        {
            std::string errMsg = (err != nullptr) ? std::string(err) : std::format("tinyexr error code {}", ret);
            if (err != nullptr)
            {
                FreeEXRErrorMessage(err);
            }
            return std::unexpected(std::format("Failed to load EXR skydome '{}': {}", pathStr, errMsg));
        }

        if (outRgba == nullptr || rawWidth <= 0 || rawHeight <= 0)
        {
            if (outRgba != nullptr)
            {
                std::free(outRgba);
            }
            return std::unexpected(std::format("Invalid image dimensions in EXR '{}': {}x{}", pathStr, rawWidth, rawHeight));
        }

        const uint32_t origW = static_cast<uint32_t>(rawWidth);
        const uint32_t origH = static_cast<uint32_t>(rawHeight);

        // Check if CPU 2x2 box-downsampling is requested
        const bool needDownsample = (origW > options.TargetMaxWidth) && (origW % 2u == 0u) && (origH % 2u == 0u);
        const uint32_t finalW = needDownsample ? (origW / 2u) : origW;
        const uint32_t finalH = needDownsample ? (origH / 2u) : origH;

        // Obtain Rec.709 to AP1 conversion matrix via Bradford adaptation
        const color::Mat3d mat = color::RgbToRgb(color::Rec709, color::AP1, color::ChromaticAdaptation::Bradford);
        const math::Float3x3 rec709ToAp1{
            static_cast<float>(mat.Rows[0][0]), static_cast<float>(mat.Rows[0][1]), static_cast<float>(mat.Rows[0][2]),
            static_cast<float>(mat.Rows[1][0]), static_cast<float>(mat.Rows[1][1]), static_cast<float>(mat.Rows[1][2]),
            static_cast<float>(mat.Rows[2][0]), static_cast<float>(mat.Rows[2][1]), static_cast<float>(mat.Rows[2][2])
        };

        ExrImageResult result{};
        result.Width = finalW;
        result.Height = finalH;
        result.PixelsFp16.resize(static_cast<size_t>(finalW) * static_cast<size_t>(finalH) * 4u);

        // Multithreaded downsampling and color conversion into final FP16 buffer
        const uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
        const uint32_t rowsPerThread = (finalH + threadCount - 1u) / threadCount;

        std::vector<std::thread> workers;
        workers.reserve(threadCount);

        for (uint32_t t = 0u; t < threadCount; ++t)
        {
            const uint32_t startY = t * rowsPerThread;
            const uint32_t endY = std::min(finalH, startY + rowsPerThread);

            if (startY >= endY)
            {
                break;
            }

            workers.emplace_back([&, startY, endY]()
            {
                for (uint32_t y = startY; y < endY; ++y)
                {
                    const size_t dstRowOffset = static_cast<size_t>(y) * static_cast<size_t>(finalW) * 4u;

                    for (uint32_t x = 0u; x < finalW; ++x)
                    {
                        float r = 0.0f;
                        float g = 0.0f;
                        float b = 0.0f;

                        if (needDownsample)
                        {
                            const uint32_t srcX0 = x * 2u;
                            const uint32_t srcY0 = y * 2u;
                            const uint32_t srcX1 = srcX0 + 1u;
                            const uint32_t srcY1 = srcY0 + 1u;

                            const size_t i00 = (static_cast<size_t>(srcY0) * origW + srcX0) * 4u;
                            const size_t i10 = (static_cast<size_t>(srcY0) * origW + srcX1) * 4u;
                            const size_t i01 = (static_cast<size_t>(srcY1) * origW + srcX0) * 4u;
                            const size_t i11 = (static_cast<size_t>(srcY1) * origW + srcX1) * 4u;

                            r = 0.25f * (outRgba[i00] + outRgba[i10] + outRgba[i01] + outRgba[i11]);
                            g = 0.25f * (outRgba[i00 + 1] + outRgba[i10 + 1] + outRgba[i01 + 1] + outRgba[i11 + 1]);
                            b = 0.25f * (outRgba[i00 + 2] + outRgba[i10 + 2] + outRgba[i01 + 2] + outRgba[i11 + 2]);
                        }
                        else
                        {
                            const size_t idx = (static_cast<size_t>(y) * origW + x) * 4u;
                            r = outRgba[idx];
                            g = outRgba[idx + 1];
                            b = outRgba[idx + 2];
                        }

                        // Convert Rec.709 linear to AP1 linear
                        const math::Float3 ap1 = rec709ToAp1 * math::Float3{ r, g, b };
                        const float rAp1 = std::clamp(std::isfinite(ap1.x) ? ap1.x : 0.0f, 0.0f, 65504.0f);
                        const float gAp1 = std::clamp(std::isfinite(ap1.y) ? ap1.y : 0.0f, 0.0f, 65504.0f);
                        const float bAp1 = std::clamp(std::isfinite(ap1.z) ? ap1.z : 0.0f, 0.0f, 65504.0f);

                        const size_t dstIdx = dstRowOffset + static_cast<size_t>(x) * 4u;
                        result.PixelsFp16[dstIdx + 0] = math::FloatToHalf(rAp1);
                        result.PixelsFp16[dstIdx + 1] = math::FloatToHalf(gAp1);
                        result.PixelsFp16[dstIdx + 2] = math::FloatToHalf(bAp1);
                        result.PixelsFp16[dstIdx + 3] = math::FloatToHalf(1.0f);
                    }
                }
            });
        }

        for (std::thread& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        std::free(outRgba);
        outRgba = nullptr;

        // Compute L2 spherical harmonics and dominant directional light from upper hemisphere
        // Use downsampled grid for fast, accurate SH integration
        const uint32_t shW = std::min(finalW, 1024u);
        const uint32_t shH = std::min(finalH, 512u);

        std::array<std::array<double, 3>, 9> shAccum{};
        float maxLuminance = -1.0f;
        std::array<float, 3> peakDirection{ 0.0f, 1.0f, 0.0f };
        std::array<float, 3> peakRadiance{ 0.0f, 0.0f, 0.0f };

        const float dOmegaFactor = (2.0f * k_Pi * k_Pi) / static_cast<float>(shW * shH);

        for (uint32_t sj = 0u; sj < shH; ++sj)
        {
            const float v = (static_cast<float>(sj) + 0.5f) / static_cast<float>(shH);
            const float theta = v * k_Pi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);
            const float dOmega = sinTheta * dOmegaFactor;

            const uint32_t fy = static_cast<uint32_t>(v * static_cast<float>(finalH));
            const size_t rowOffset = static_cast<size_t>(std::min(fy, finalH - 1u)) * static_cast<size_t>(finalW) * 4u;

            for (uint32_t si = 0u; si < shW; ++si)
            {
                const float u = (static_cast<float>(si) + 0.5f) / static_cast<float>(shW);
                const float phi = (u - 0.5f) * 2.0f * k_Pi;

                const float dirX = sinTheta * std::cos(phi);
                const float dirY = cosTheta;
                const float dirZ = sinTheta * std::sin(phi);

                const uint32_t fx = static_cast<uint32_t>(u * static_cast<float>(finalW));
                const size_t pxIdx = rowOffset + static_cast<size_t>(std::min(fx, finalW - 1u)) * 4u;

                // Decode FP16 AP1 back to float for accumulation
                const uint16_t hR = result.PixelsFp16[pxIdx + 0];
                const uint16_t hG = result.PixelsFp16[pxIdx + 1];
                const uint16_t hB = result.PixelsFp16[pxIdx + 2];

                tinyexr::FP16 h16;
                h16.u = hR;
                const float r = tinyexr::half_to_float(h16).f;
                h16.u = hG;
                const float g = tinyexr::half_to_float(h16).f;
                h16.u = hB;
                const float b = tinyexr::half_to_float(h16).f;

                // SH basis functions
                const float Y0 = 0.28209479f;
                const float Y1 = 0.48860251f * dirY;
                const float Y2 = 0.48860251f * dirZ;
                const float Y3 = 0.48860251f * dirX;
                const float Y4 = 1.0925484f * dirX * dirY;
                const float Y5 = 1.0925484f * dirY * dirZ;
                const float Y6 = 0.31539156f * (3.0f * dirZ * dirZ - 1.0f);
                const float Y7 = 1.0925484f * dirX * dirZ;
                const float Y8 = 0.5462742f * (dirX * dirX - dirY * dirY);

                const std::array<float, 9> Y{ Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8 };
                for (size_t k = 0u; k < 9u; ++k)
                {
                    shAccum[k][0] += static_cast<double>(r * Y[k] * dOmega);
                    shAccum[k][1] += static_cast<double>(g * Y[k] * dOmega);
                    shAccum[k][2] += static_cast<double>(b * Y[k] * dOmega);
                }

                // Check upper hemisphere for dominant light
                if (dirY > 0.0f)
                {
                    const float lum = 0.2722287f * r + 0.6740818f * g + 0.0536895f * b;
                    if (lum > maxLuminance)
                    {
                        maxLuminance = lum;
                        peakDirection = { dirX, dirY, dirZ };
                        peakRadiance = { r, g, b };
                    }
                }
            }
        }

        // Apply Funk-Hecke cosine lobe convolution factors for diffuse irradiance
        const double A0 = std::numbers::pi;
        const double A1 = 2.09439510239319549231;
        const double A2 = 0.78539816339744830962;

        const std::array<double, 9> A_factors{ A0, A1, A1, A1, A2, A2, A2, A2, A2 };
        for (size_t k = 0u; k < 9u; ++k)
        {
            result.SphericalHarmonics[k][0] = static_cast<float>(shAccum[k][0] * A_factors[k]);
            result.SphericalHarmonics[k][1] = static_cast<float>(shAccum[k][1] * A_factors[k]);
            result.SphericalHarmonics[k][2] = static_cast<float>(shAccum[k][2] * A_factors[k]);
            result.SphericalHarmonics[k][3] = 1.0f;
        }

        // Dominant light integration in upper hemisphere: integrate sun disk within ~3.5 degree cone
        if (maxLuminance > 0.0f)
        {
            result.DominantLightDirection = peakDirection;

            const float cosCone = std::cos(0.061f); // ~3.5 degrees
            std::array<double, 3> sunIrradiance{ 0.0, 0.0, 0.0 };
            double coneOmega = 0.0;

            for (uint32_t sj = 0u; sj < shH / 2u; ++sj)
            {
                const float v = (static_cast<float>(sj) + 0.5f) / static_cast<float>(shH);
                const float theta = v * k_Pi;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                const float dOmega = sinTheta * dOmegaFactor;

                const uint32_t fy = static_cast<uint32_t>(v * static_cast<float>(finalH));
                const size_t rowOffset = static_cast<size_t>(std::min(fy, finalH - 1u)) * static_cast<size_t>(finalW) * 4u;

                for (uint32_t si = 0u; si < shW; ++si)
                {
                    const float u = (static_cast<float>(si) + 0.5f) / static_cast<float>(shW);
                    const float phi = (u - 0.5f) * 2.0f * k_Pi;

                    const float dirX = sinTheta * std::cos(phi);
                    const float dirY = cosTheta;
                    const float dirZ = sinTheta * std::sin(phi);

                    const float dotPeak = dirX * peakDirection[0] + dirY * peakDirection[1] + dirZ * peakDirection[2];
                    if (dotPeak >= cosCone)
                    {
                        const uint32_t fx = static_cast<uint32_t>(u * static_cast<float>(finalW));
                        const size_t pxIdx = rowOffset + static_cast<size_t>(std::min(fx, finalW - 1u)) * 4u;

                        tinyexr::FP16 h16;
                        h16.u = result.PixelsFp16[pxIdx + 0];
                        const float r = tinyexr::half_to_float(h16).f;
                        h16.u = result.PixelsFp16[pxIdx + 1];
                        const float g = tinyexr::half_to_float(h16).f;
                        h16.u = result.PixelsFp16[pxIdx + 2];
                        const float b = tinyexr::half_to_float(h16).f;

                        const float lum = 0.2722287f * r + 0.6740818f * g + 0.0536895f * b;
                        if (lum > 0.05f * maxLuminance)
                        {
                            const double bgScale = 0.05;
                            const double rAbove = std::max(0.0, static_cast<double>(r) - bgScale * static_cast<double>(peakRadiance[0]));
                            const double gAbove = std::max(0.0, static_cast<double>(g) - bgScale * static_cast<double>(peakRadiance[1]));
                            const double bAbove = std::max(0.0, static_cast<double>(b) - bgScale * static_cast<double>(peakRadiance[2]));
                            sunIrradiance[0] += rAbove * dOmega;
                            sunIrradiance[1] += gAbove * dOmega;
                            sunIrradiance[2] += bAbove * dOmega;
                            coneOmega += static_cast<double>(dOmega);
                        }
                    }
                }
            }

            if (coneOmega > 0.0)
            {
                result.DominantLightRadiance[0] = static_cast<float>(sunIrradiance[0]);
                result.DominantLightRadiance[1] = static_cast<float>(sunIrradiance[1]);
                result.DominantLightRadiance[2] = static_cast<float>(sunIrradiance[2]);
            }
            else
            {
                const float peakOmega = dOmegaFactor * std::max(0.001f, std::sqrt(std::max(0.0f, 1.0f - peakDirection[1] * peakDirection[1])));
                result.DominantLightRadiance[0] = peakRadiance[0] * peakOmega;
                result.DominantLightRadiance[1] = peakRadiance[1] * peakOmega;
                result.DominantLightRadiance[2] = peakRadiance[2] * peakOmega;
            }
        }
        else
        {
            result.DominantLightDirection = { 0.0f, 1.0f, 0.0f };
            result.DominantLightRadiance = { 0.0f, 0.0f, 0.0f };
        }

        return result;
    }
}
