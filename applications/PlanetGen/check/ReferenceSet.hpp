#pragma once
#ifndef DIAMOND_DOGS_PLANET_REFERENCE_SET_HPP
#define DIAMOND_DOGS_PLANET_REFERENCE_SET_HPP
#include "math/MathStorage.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace planet
{
    /** A folder of reference files from PlanetLab's write_gpu_reference.py: manifest.json and one raw little-endian
     *  file for each array. Each read converts the stored type to the type that the shader uses. A read prints the
     *  cause and returns nothing when the file is missing or its type is not a known one. */
    class ReferenceSet
    {
    public:
        [[nodiscard]] static std::optional<ReferenceSet> Open(const std::filesystem::path& folder);

        [[nodiscard]] int32_t PointCount() const noexcept;
        [[nodiscard]] int32_t PlateCount() const noexcept;
        /** @brief My, the time of the capture. */
        [[nodiscard]] float Time() const noexcept;

        [[nodiscard]] std::optional<std::vector<float>> Floats(std::string_view name) const;
        [[nodiscard]] std::optional<std::vector<math::Float3>> Float3s(std::string_view name) const;
        [[nodiscard]] std::optional<std::vector<math::Float3x3>> Float3x3s(std::string_view name) const;
        [[nodiscard]] std::optional<std::vector<int32_t>> Ints(std::string_view name) const;
        [[nodiscard]] std::optional<std::vector<uint32_t>> Words(std::string_view name) const;
        /** @brief A uint8 array, one byte for each element. */
        [[nodiscard]] std::optional<std::vector<uint8_t>> Bytes(std::string_view name) const;

    private:
        struct ArrayInfo
        {
            std::string Type;
            int64_t ElementCount{ 0 };
        };

        [[nodiscard]] std::optional<std::vector<double>> read(std::string_view name) const;

        std::filesystem::path folder;
        std::unordered_map<std::string, ArrayInfo> arrays;
        int32_t pointCount{ 0 };
        int32_t plateCount{ 0 };
        float time{ 0.0f };
    };
}

#endif // !DIAMOND_DOGS_PLANET_REFERENCE_SET_HPP
