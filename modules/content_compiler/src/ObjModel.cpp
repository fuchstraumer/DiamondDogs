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
#include <cassert>

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
    template<size_t numVecElements>
    auto extract_vector(std::vector<std::string_view>& views)->mango::Vector<float, numVecElements>;
    std::string to_lowercase(std::string_view& view);
    auto get_line(std::string_view& view)->std::string_view;
    std::vector<std::string_view> separate_tokens_in_view(std::string_view view, const char delimiter);
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

        struct OBJvertex
        {
            int32_t posIdx{ 0 };
            int32_t normalIdx{ 0 };
            int32_t uvIdx{ 0 };
        };
        std::vector<OBJvertex> OBJverts;

        struct OBJface
        {
            uint32_t startVertexIdx{ 0 };
            uint32_t endVertexIdx{ 0 };
            uint32_t indexStart{ 0 };
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

        bool parsedGroupNameLastLine = false;
        bool parsedMaterialNameLastLine = false;
        std::string mtlLibName;

        while (!modelMemoryView.empty())
        {
            std::string_view line = get_line(modelMemoryView);
            auto tokens = separate_tokens_in_view(line, ' ');
            if (tokens.empty())
            {
                continue;
            }

            if (tokens[0] == "f")
            {
                OBJface face{};
                face.startVertexIdx = static_cast<uint32_t>(OBJverts.size());

                for (size_t i = 0; i < 3; ++i)
                {
                    OBJvertex vertex;
                    std::vector<std::string_view> indexTokens = separate_tokens_in_view(tokens[i + 1], '/');
                    std::from_chars(indexTokens[0].data(), indexTokens[0].data() + indexTokens[0].size(), vertex.posIdx);
                    if (indexTokens.size() > 1)
                    {
                        std::from_chars(indexTokens[1].data(), indexTokens[1].data() + indexTokens[1].size(), vertex.uvIdx);
                    }
                    if (indexTokens.size() > 2 && loadNormals)
                    {
                        std::from_chars(indexTokens[2].data(), indexTokens[2].data() + indexTokens[2].size(), vertex.normalIdx);
                    }

                    if (vertex.posIdx < 0)
                    {
                        vertex.posIdx += static_cast<int32_t>(positions.size()) + 1;
                    }
                    if (vertex.uvIdx < 0)
                    {
                        vertex.uvIdx += static_cast<int32_t>(uvs.size()) + 1;
                    }
                    if (vertex.normalIdx < 0)
                    {
                        vertex.normalIdx += static_cast<int32_t>(normals.size()) + 1;
                    }

                    OBJverts.emplace_back(vertex);
                }

                face.endVertexIdx = static_cast<uint32_t>(OBJverts.size());
                if (face.endVertexIdx == face.startVertexIdx)
                {
                    LOG(ERROR) << "Face has only 1 vertex... missing vertices present!";
                }

                OBJfaces.emplace_back(face);
            }
            // vertex position
            else if (tokens[0] == "v")
            {
               positions.emplace_back(extract_vector<3>(tokens));
            }
            // vertex UV
            else if (tokens[0] == "vt")
            {
                uvs.emplace_back(extract_vector<2>(tokens));
            }
            // vertex normal
            else if (tokens[0] == "vn")
            {
               normals.emplace_back(extract_vector<3>(tokens));
            }
            else if (tokens[0] == "usemtl")
            {
                //std::string_view materialToken = extract_token(modelMemoryView);

                OBJMtlRange* currMtlRange = &OBJMtlRanges.back();
                currMtlRange->endFaceIndex = static_cast<uint32_t>(OBJfaces.size());

                if (currMtlRange->endFaceIndex > currMtlRange->startFaceIndex && !parsedGroupNameLastLine)
                {
                    std::string lastObjName = currMtlRange->objectName;
                    OBJMtlRanges.push_back(OBJMtlRange());
                    currMtlRange = &OBJMtlRanges.back();
                    currMtlRange->objectName = std::move(lastObjName);
                }

                currMtlRange->mtlName = to_lowercase(tokens[1]);

                currMtlRange->startFaceIndex = static_cast<uint32_t>(OBJfaces.size());
                parsedMaterialNameLastLine = true;
                continue;
            }
            // new group
            else if (tokens[0] == "g")
            {
                //std::string_view groupToken = extract_token(modelMemoryView);
                OBJMtlRange* currMtlRange = &OBJMtlRanges.back();
                currMtlRange->endFaceIndex = static_cast<uint32_t>(OBJfaces.size());

                if (currMtlRange->endFaceIndex > currMtlRange->startFaceIndex && !parsedMaterialNameLastLine)
                {
                    OBJMtlRanges.push_back(OBJMtlRange());
                    currMtlRange = &OBJMtlRanges.back();
                }

                currMtlRange->objectName = to_lowercase(tokens[1]);
                currMtlRange->startFaceIndex = static_cast<uint32_t>(OBJfaces.size());

                parsedGroupNameLastLine = true;
                continue;
            }
            else if (tokens[0] == "mtllib")
            {
                mtlLibName = tokens[1];
            }

            parsedGroupNameLastLine = false;
            parsedMaterialNameLastLine = false;
        }

        OBJMtlRanges.back().endFaceIndex = static_cast<uint32_t>(OBJfaces.size());

        // Move data over now, and begin coalescing

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

    std::string to_lowercase(std::string_view& view)
    {
        std::string result;
        result.reserve(view.size());
        for (size_t i = 0; i < view.size(); ++i)
        {
            result.push_back(std::tolower(view[i]));
        }
        result.shrink_to_fit();
        return result;
    }

    auto get_line(std::string_view& view)->std::string_view
    {
        if (view.empty())
        {
            return std::string_view{};
        }
        
        const size_t firstNewline = view.find_first_of('\n');
        const size_t firstReturn = view.find_first_of('\r');
        if (firstReturn != std::string::npos)
        {
            // We probably have both, but in this case we need to return the line
            // w/o the return but cut out the return in the parent view
            std::string_view result_view = view.substr(0, firstReturn);
            view.remove_prefix(firstNewline + 1);
            return result_view;
        }
        else
        {
            std::string_view result_view = view.substr(0, firstNewline);
            view.remove_prefix(firstNewline + 1);
            return result_view;
        }
    }

    std::vector<std::string_view> separate_tokens_in_view(std::string_view view, const char delimiter)
    {
        std::vector<std::string_view> results;
        while (!view.empty())
        {
            size_t distanceToDelimiter = view.find_first_of(delimiter);
            if (distanceToDelimiter != std::string::npos && distanceToDelimiter > 0)
            {
                results.emplace_back(view.substr(0, distanceToDelimiter));
                view.remove_prefix(distanceToDelimiter + 1);
            }
            else if (distanceToDelimiter == 0)
            {
                view.remove_prefix(1);
            }
            else
            {
                results.emplace_back(view);
                break;
            }
        }
        // We assume tokens here are split by spaces
        return results;
    }

    template<size_t numVecElements>
    auto extract_vector(std::vector<std::string_view>& tokens) -> mango::Vector<float, numVecElements>
    {
        mango::Vector<float, numVecElements> result;
        for (size_t i = 0; i < numVecElements; ++i)
        {
            // We offset by 1, because the token at zero is the specifier for this line/row
            std::string_view& currToken = tokens[i + 1];
            std::from_chars_result conv_result = std::from_chars(currToken.data(), currToken.data() + currToken.size(), result[i]);
        }
        if constexpr (numVecElements == 2)
        {
            // extracting UVs: need to flip Y of uv
            result.y = 1.0f - result.y;
        }
        return result;
    }
}