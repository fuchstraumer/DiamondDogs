#include "ObjModel.hpp"
#pragma warning(push, 0)
#include "easylogging++.h"
#include "mango/filesystem/file.hpp"
#include "mango/core/compress.hpp"
#include "mango/math/math.hpp"
#include "mango/simd/simd.hpp"
#include "mango/core/hash.hpp"
#pragma warning(pop)
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <xhash>
#include <charconv>

namespace std
{
    template<>
    struct hash<mango::XX3HASH128>
    {
        size_t operator()(const mango::XX3HASH128& hash_val) const noexcept
        {
            return hash<uint64_t>()(hash_val.data[0]) ^ hash<uint64_t>()(hash_val.data[1]);
        }
    };
}

namespace
{

    auto find_delimiters(const std::string_view& view, size_t offset = 0)->std::tuple<size_t, size_t, size_t>;
    auto find_end_of_delimiters(const std::string_view& view)->size_t;
    auto find_first_token(const std::string_view& view, size_t offset = 0)->size_t;
    auto find_end_of_token(const std::string_view& view, size_t offset = 0)->size_t;
    auto find_end_of_line(const std::string_view& view, size_t offset = 0)->size_t;
    auto extract_token(std::string_view& view)->std::string_view;
    template<size_t numVecElements>
    auto extract_vector(std::string_view& view)->mango::Vector<float, numVecElements>;

}

namespace ObjLoader
{
    using vec3 = mango::Vector<float, 3>;
    using vec2 = mango::Vector<float, 2>;
    using AABB = mango::Box;

    constexpr static uint64_t checksumHashSeed = 0x46a5bf5c1e7a52cc;
    constexpr static const char* metaStr = "/meta";
    constexpr static const char* vertsStr = "/verts";
    constexpr static const char* indicesStr = "/indices";
    constexpr static const char* materialStr = "/material";

    struct ObjFile
    {
        ObjFile() = default;
        ObjFile(const char* fname) : file(fname)
        {
            memory = file;
            checksum = mango::xx3hash128(checksumHashSeed, memory);
        }
        ObjFile(const ObjFile&) = delete;
        ObjFile& operator=(const ObjFile&) = delete;
        mango::filesystem::File file;
        mango::ConstMemory memory;
        mango::XX3HASH128 checksum;
    };

    struct MaterialRange
    {
        std::string_view MaterialName;
        uint32_t startIndex{ 0u };
        uint32_t indexCount{ 0u };
    };

    struct VertexData
    {
        size_t vertexStride;
        std::vector<VertexMetadataEntry> attributes;
        // vertices are stored as raw floats for maximum flexibility on packing
        std::vector<float> vertices;
    };

    // We use uint16_t indices when we can, since the space savings can be pretty aggressive
    using IndexData = std::variant<std::vector<uint32_t>, std::vector<uint16_t>>;

    struct ParsingContext
    {
        ParsingContext(const char* fname) : modelFile(fname) {}
        VertexData vertexData;
        IndexData indexData;
        ObjFile modelFile;
        AABB bounds;
    };

    void ParseModel(ParsingContext& context, bool loadNormals)
    {
        std::vector<vec3> positions;
        std::vector<vec3> normals;
        std::vector<vec2> uvs;
        std::string mtlLibName;

        struct OBJvertex
        {
            uint32_t posIdx;
            uint32_t normalIdx;
            uint32_t uvIdx;
        };
        std::vector<OBJvertex> OBJverts;

        struct OBJface
        {
            uint32_t startVertexIdx;
            uint32_t endVertexIdx;
            uint32_t indexStart;
        };
        std::vector<OBJface> OBJfaces;

        struct OBJMtlRange
        {
            std::string objectName;
            std::string mtlName;
            // These indices are into the indices CONTAINER, not like the start literal index and the monotically increasing end index
            uint32_t startFaceIndex;
            uint32_t endFaceIndex;
        };
        std::vector<OBJMtlRange> OBJMtlRanges(1, OBJMtlRange{ std::string(""), std::string(""), 0u, 0u });

        const char* const modelMemoryBegin = reinterpret_cast<const char*>(context.modelFile.memory.address);
        const size_t modelMemorySize = context.modelFile.memory.size;
        std::string_view modelMemoryView(modelMemoryBegin, modelMemorySize);

        // as a quick upfront search, let's try to find "mtllib"
        constexpr const char* const mtllibStr = "mtllib";

        std::string_view firstTok = extract_token(modelMemoryView);
        if (firstTok == "mtllib")
        {
            mtlLibName = extract_token(modelMemoryView);

        }

        std::vector<std::string> materialNames;

        bool parsedGroupNameLastLine = false;
        bool parsedMaterialNameLastLine = false;
        std::string lastParsedName;

        while (!modelMemoryView.empty())
        {

            std::string_view nextToken = extract_token(modelMemoryView);
            // usually another new group too, but need to note the material being used
            if (nextToken == "usemtl")
            {
                OBJMtlRange* currMtlRange = &OBJMtlRanges.back();

                //if (!parsedGroupNameLastLine &&)

                parsedMaterialNameLastLine = true;

            }
            // new group
            else if (nextToken == "o" || nextToken == "g")
            {
                std::string_view groupToken = extract_token(modelMemoryView);
                if (parsedMaterialNameLastLine && OBJMtlRanges.back().objectName.empty())
                {
                    OBJMtlRanges.back().objectName = groupToken;
                }
                else
                {
                    

                }
                parsedGroupNameLastLine = true;
            }
            // indices
            else if (nextToken == "f")
            {

            }
            // vertex position
            else if (nextToken == "v")
            {
               positions.emplace_back(extract_vector<3>(modelMemoryView));
            }
            // vertex UV
            else if (nextToken == "vt")
            {
                uvs.emplace_back(extract_vector<2>(modelMemoryView));
            }
            // vertex normal
            else if (nextToken == "vn")
            {
               normals.emplace_back(extract_vector<3>(modelMemoryView));
            }
            // either a comment we need to trim, or an empty token because we're in a series of newlines
            else if (nextToken.empty())
            {
                size_t endOfDelim = find_end_of_delimiters(modelMemoryView);
                modelMemoryView.remove_prefix(endOfDelim);
            }
            else if (nextToken == "#")
            {
                size_t endOfCurrLine = find_end_of_line(modelMemoryView, 0);
                modelMemoryView.remove_prefix(endOfCurrLine);
            }
            else if (nextToken == "s")
            {
                // smooth shading off. not really something we need to deal with.
                // also continue because this is near g/o/usemtl: we don't want to reset that state if it just comes in between one of those
                continue;
            }
            else
            {
                LOG(ERROR) << "Invalid token of " << nextToken << " was read!";
                throw std::runtime_error("Read invalid token from .obj file!");
            }

            parsedGroupNameLastLine = false;
            parsedMaterialNameLastLine = false;
        }

    }
    
    std::unordered_map<mango::XX3HASH128, ObjectModelData> loadedFiles;
}

ObjectModelData* LoadModelFromFile(
    const char* model_filename,
    const char* material_directory,
    RequiresNormals requires_normals,
    RequiresTangents requires_tangents,
    OptimizeMesh optimize_mesh)
{
    // Start context by opening requested file: we will run a QUICK hashing algorithm on it as a checksum
    using namespace ObjLoader;
    ParsingContext context(model_filename);

    // Check to see if we already loaded this file
    auto existingLoadedDataIter = loadedFiles.find(context.modelFile.checksum);
    if (existingLoadedDataIter != loadedFiles.end())
    {
        ObjectModelData* result = &existingLoadedDataIter->second;
        return result;
    }

    ParseModel(context, static_cast<bool>(requires_normals));

    return nullptr;
}

namespace
{

    auto find_delimiters(const std::string_view& view, size_t offset) -> std::tuple<size_t, size_t, size_t>
    {
        size_t firstNewline = view.find_first_of('\n', offset);
        size_t firstReturn = view.find_first_of('\r', offset);
        size_t firstSpace = view.find_first_of(' ', offset);
        return std::tuple(firstNewline, firstReturn, firstSpace);
    }

    auto find_end_of_delimiters(const std::string_view& view)->size_t
    {
        size_t firstNewline = view.find_first_not_of('\n');
        size_t firstReturn = view.find_first_not_of('\r');
        return firstNewline > firstReturn ? firstNewline : firstReturn;
    }

    auto find_first_token(const std::string_view& view, size_t offset)->size_t
    {
        return view.find_first_not_of(' ', offset);
    }

    auto find_end_of_token(const std::string_view& view, size_t offset)->size_t
    {
        const auto [firstNewline, firstReturn, firstSpace] = find_delimiters(view, offset);
        if (firstNewline < firstReturn && firstNewline < firstSpace)
        {
            return firstNewline;
        }
        else if (firstReturn < firstNewline && firstReturn < firstSpace)
        {
            return firstReturn;
        }
        else if (firstSpace < firstNewline && firstSpace < firstReturn)
        {
            return firstSpace;
        }
        else
        {
            return std::string_view::npos;
        }
    }

    auto find_end_of_line(const std::string_view& view, size_t offset)->size_t
    {
        const auto [firstNewline, firstReturn, unused] = find_delimiters(view, offset);
        return firstNewline > firstReturn ? firstNewline : firstReturn;
    }

    auto extract_token(std::string_view& view)->std::string_view
    {
        size_t startOfToken = find_first_token(view);
        view.remove_prefix(startOfToken);
        size_t endOfToken = find_end_of_token(view);
        std::string_view token = view.substr(0, endOfToken);
        view.remove_prefix(endOfToken);
        return token;
    };

    template<size_t numVecElements>
    auto extract_vector(std::string_view& view) -> mango::Vector<float, numVecElements>
    {
        size_t startOfNumbers = view.find_first_not_of(' ');
        view.remove_prefix(startOfNumbers);
        size_t endOfNumbers = find_end_of_line(view, 0);
        std::string_view numbersView = view.substr(startOfNumbers, endOfNumbers);
        mango::Vector<float, numVecElements> result;
        for (size_t i = 0; i < numVecElements; ++i)
        {
            size_t endOfCurrent = find_end_of_token(numbersView);
            std::from_chars_result conv_result = std::from_chars(numbersView.data(), numbersView.data() + endOfCurrent, result[i]);
            numbersView.remove_prefix(endOfCurrent + 1); // + 1 removes the space we expect to always encounter
        }
        if constexpr (numVecElements == 2)
        {
            // extracting UVs: need to flip Y of uv
            result.y = 1.0f - result.y;
        }
        view.remove_prefix(endOfNumbers + 1);
        return result;
    }
}