#include "ReferenceSet.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <format>
#include <fstream>
#include <print>
#include <span>

namespace planet
{
    namespace
    {
        // the element size of each type that write_gpu_reference.py writes, by its numpy name
        [[nodiscard]] size_t ElementSize(std::string_view type) noexcept
        {
            if (type == "float32" || type == "int32" || type == "uint32")
            {
                return 4u;
            }

            if (type == "uint8" || type == "int8")
            {
                return 1u;
            }

            return 0u;
        }

        template<typename T>
        void Widen(std::span<const std::byte> bytes, std::vector<double>& values)
        {
            for (size_t index = 0; index < values.size(); ++index)
            {
                T value{};
                std::memcpy(&value, bytes.data() + index * sizeof(T), sizeof(T));
                values[index] = static_cast<double>(value);
            }
        }
    }

    std::optional<ReferenceSet> ReferenceSet::Open(const std::filesystem::path& folder)
    {
        std::ifstream file(folder / "manifest.json");

        if (!file)
        {
            std::println(stderr, "{} has no manifest.json. Run write_gpu_reference.py in applications/PlanetLab first",
                         folder.string());
            return std::nullopt;
        }

        const nlohmann::json manifest = nlohmann::json::parse(file, nullptr, false);

        if (manifest.is_discarded() || !manifest.contains("arrays"))
        {
            std::println(stderr, "{}: manifest.json is not valid", folder.string());
            return std::nullopt;
        }

        ReferenceSet set;
        set.folder = folder;
        set.pointCount = manifest.value("point_count", 0);
        set.plateCount = manifest.value("plate_count", 0);
        set.time = manifest.value("time", 0.0f);

        for (const nlohmann::json& entry : manifest["arrays"])
        {
            std::string name = entry.value("file", std::string{});
            int64_t count = 1;

            for (const nlohmann::json& extent : entry["shape"])
            {
                count *= extent.get<int64_t>();
            }

            name.resize(name.size() - std::string_view{ ".bin" }.size());
            set.arrays.emplace(std::move(name), ArrayInfo{ entry.value("type", std::string{}), count });
        }

        return set;
    }

    int32_t ReferenceSet::PointCount() const noexcept
    {
        return pointCount;
    }

    int32_t ReferenceSet::PlateCount() const noexcept
    {
        return plateCount;
    }

    float ReferenceSet::Time() const noexcept
    {
        return time;
    }

    std::optional<std::vector<float>> ReferenceSet::Floats(std::string_view name) const
    {
        std::optional<std::vector<double>> values = read(name);

        if (!values.has_value())
        {
            return std::nullopt;
        }

        std::vector<float> result(values->size());

        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<float>((*values)[index]);
        }

        return result;
    }

    std::optional<std::vector<int32_t>> ReferenceSet::Ints(std::string_view name) const
    {
        std::optional<std::vector<double>> values = read(name);

        if (!values.has_value())
        {
            return std::nullopt;
        }

        std::vector<int32_t> result(values->size());

        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<int32_t>((*values)[index]);
        }

        return result;
    }

    std::optional<std::vector<uint32_t>> ReferenceSet::Words(std::string_view name) const
    {
        std::optional<std::vector<double>> values = read(name);

        if (!values.has_value())
        {
            return std::nullopt;
        }

        std::vector<uint32_t> result(values->size());

        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<uint32_t>((*values)[index]);
        }

        return result;
    }

    std::optional<std::vector<uint8_t>> ReferenceSet::Bytes(std::string_view name) const
    {
        std::optional<std::vector<double>> values = read(name);

        if (!values.has_value())
        {
            return std::nullopt;
        }

        std::vector<uint8_t> result(values->size());

        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<uint8_t>((*values)[index]);
        }

        return result;
    }

    std::optional<std::vector<double>> ReferenceSet::read(std::string_view name) const
    {
        const auto found = arrays.find(std::string{ name });

        if (found == arrays.end())
        {
            std::println(stderr, "{}: the manifest has no array {}", folder.string(), name);
            return std::nullopt;
        }

        const ArrayInfo& info = found->second;
        const size_t elementSize = ElementSize(info.Type);
        const size_t byteSize = elementSize * static_cast<size_t>(info.ElementCount);
        std::ifstream file(folder / std::format("{}.bin", name), std::ios::binary);
        std::vector<std::byte> bytes(byteSize);

        if (elementSize == 0u || !file || !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteSize)))
        {
            std::println(stderr, "{}: {}.bin is missing, short, or of the unknown type '{}'", folder.string(), name, info.Type);
            return std::nullopt;
        }

        std::vector<double> values(static_cast<size_t>(info.ElementCount));

        if (info.Type == "float32")
        {
            Widen<float>(bytes, values);
        }
        else if (info.Type == "int32")
        {
            Widen<int32_t>(bytes, values);
        }
        else if (info.Type == "uint32")
        {
            Widen<uint32_t>(bytes, values);
        }
        else if (info.Type == "int8")
        {
            Widen<int8_t>(bytes, values);
        }
        else
        {
            Widen<uint8_t>(bytes, values);
        }

        return values;
    }
}
