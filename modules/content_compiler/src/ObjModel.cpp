#include "ObjModel.hpp"
#include "LoadedDataCache.hpp"
#include "svUtil.hpp"
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
#include <algorithm>
#include <vulkan/vulkan_core.h>

namespace
{
    template<size_t numVecElements>
    auto extract_vector(std::vector<std::string_view>& views)->mango::Vector<float, numVecElements>;
}

namespace ObjLoader
{
    using vec3 = mango::Vector<float, 3>;
    using vec2 = mango::Vector<float, 2>;
    using AABB = mango::Box;

    constexpr static uint64_t checksumHashSeed = 0x46a5bf5c1e7a52cc;

    struct OBJgroup
    {
        std::string groupName;
        uint32_t startFaceIndex{ 0u };
        uint32_t endFaceIndex{ 0u };
        uint32_t startMtlIndex{ 0u };
        uint32_t endMtlIndex{ 0u };
    };

    struct OBJvertex
    {
        int32_t posIdx{ 0 };
        int32_t normalIdx{ 0 };
        int32_t uvIdx{ 0 };
    };

    struct OBJface
    {
        uint32_t startVertexIdx{ 0 };
        uint32_t endVertexIdx{ 0 };
        uint32_t indexStart{ 0 };
    };

    struct OBJMtlRange
    {
        std::string mtlName;
        // These indices are into the indices CONTAINER, not like the start literal index and the monotically increasing end index
        uint32_t startFaceIndex;
        uint32_t endFaceIndex;
    };

    /*
        The filesystem object and the memory view + checksum we get just 
        from loading the .obj file into memory. Used to load data into
        ObjFileData, then is effectively released
    */
    struct ObjFile
    {
        ObjFile(const char* fname) : file(fname)
        {
            memory = file;
            checksum = mango::xxhash64(checksumHashSeed, memory);
        }
        mango::filesystem::File file;
        mango::ConstMemory memory;
        mango::XX3HASH64 checksum;
    };

    /*
        Contains the data from the object file, but not the actual file itself. We effectively release that as soon as we can.
    */
    class ObjFileData
    {
    public:

        ObjFileData();

        ObjFileData(const ObjFileData&) = delete;
        ObjFileData& operator=(const ObjFileData&) = delete;

        void ParseFile(const ObjFile& modelFile, const bool loadNormals, const bool loadTangents);
        ObjectModelDataImpl RetrieveData(const bool loadNormals, const bool loadTangents);

    private:

        void parseFile(const ObjFile& modelFile, bool loadNormals);
        void transferData(ObjectModelDataImpl& context, bool loadNormals, bool loadTangents);
        void prepareMaterialsAndGroups(ObjectModelDataImpl& context);
        void releaseData();


        // All of this data is for the file-based representation of the mesh, so I'm putting it in here
        std::vector<vec3> positions;
        std::vector<vec3> normals;
        std::vector<vec2> uvs;
        std::vector<OBJgroup> groups;
        std::vector<OBJvertex> OBJverts;
        std::vector<OBJface> OBJfaces;
        std::vector<OBJMtlRange> OBJMtlRanges;
    };


    ObjFileData::ObjFileData()
    {
        groups.reserve(2048);
        OBJMtlRanges.reserve(2048);
        groups.emplace_back(OBJgroup{ std::string(""), 0u, 0u, 0u, 0u });
        OBJMtlRanges.emplace_back(OBJMtlRange{ std::string(""), 0u, 0u });
    }

    void ObjFileData::ParseFile(const ObjFile& modelFile, const bool loadNormals, const bool loadTangents)
    {
        parseFile(modelFile, loadNormals);
    }

    ObjectModelDataImpl ObjFileData::RetrieveData(const bool loadNormals, const bool loadTangents)
    {
        ObjectModelDataImpl results;
        // transfers data into results in the compatible format for rendering
        transferData(results, loadNormals, loadTangents);
        // converts material and primitive groups into what we care about when rendering#pra
        prepareMaterialsAndGroups(results);
        releaseData();
        return results;
    }

    void ObjFileData::parseFile(const ObjFile& modelFile, bool loadNormals)
    {

        using namespace svutil;
        const char* const modelMemoryBegin = reinterpret_cast<const char*>(modelFile.memory.address);
        const size_t modelMemorySize = modelFile.memory.size;
        std::string_view modelMemoryView(modelMemoryBegin, modelMemorySize);

        std::string mtlLibName;

        while (!modelMemoryView.empty())
        {
            std::string_view line = get_line(modelMemoryView);
            std::vector<std::string_view> tokens = separate_tokens_in_view(line, ' ');
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
            else if (tokens[0] == "vn" && loadNormals)
            {
                normals.emplace_back(extract_vector<3>(tokens));
            }
            else if (tokens[0] == "usemtl")
            {
                OBJMtlRange* currMtlRange = &OBJMtlRanges.back();
                currMtlRange->endFaceIndex = static_cast<uint32_t>(OBJfaces.size());

                if (currMtlRange->endFaceIndex > currMtlRange->startFaceIndex)
                {
                    OBJMtlRanges.push_back(OBJMtlRange());
                    currMtlRange = &OBJMtlRanges.back();
                }

                currMtlRange->mtlName = to_lowercase(tokens[1]);

                currMtlRange->startFaceIndex = static_cast<uint32_t>(OBJfaces.size());
            }
            // new group
            else if (tokens[0] == "g")
            {
                // Pretty much the exact same process, just a little more to account for materials too
                OBJgroup* currGroup = &groups.back();
                currGroup->endFaceIndex = static_cast<uint32_t>(OBJfaces.size());
                currGroup->endMtlIndex = static_cast<uint32_t>(OBJMtlRanges.size());

                if (currGroup->endFaceIndex > currGroup->startFaceIndex&& currGroup->endMtlIndex > currGroup->startMtlIndex)
                {
                    groups.push_back(OBJgroup());
                    currGroup = &groups.back();
                }

                currGroup->groupName = to_lowercase(tokens[1]);
                currGroup->startFaceIndex = static_cast<uint32_t>(OBJfaces.size());
                currGroup->startMtlIndex = static_cast<uint32_t>(OBJMtlRanges.size());
            }
            else if (tokens[0] == "mtllib")
            {
                mtlLibName = tokens[1];
            }

        }

        OBJMtlRanges.back().endFaceIndex = static_cast<uint32_t>(OBJfaces.size());
    }

    void ObjFileData::transferData(ObjectModelDataImpl& results, bool loadNormals, bool loadTangents)
    { 
        // First, find out how much room we need to allocate for vertices
        size_t numFloatsToReserve = OBJverts.size() * 3u;
        numFloatsToReserve += loadNormals ? OBJverts.size() * 3u : 0u;
        numFloatsToReserve += loadTangents ? OBJverts.size() * 3u : 0u;
        numFloatsToReserve += OBJverts.size() * 2u;
        results.vertexData.resize(numFloatsToReserve, 0.0f);

        const size_t vertexStrideInFloats = 3u + (uvs.empty() ? 0u : 2u) + (loadNormals ? 3u : 0u) + (loadTangents ? 3u : 0u);
        results.vertexStride = sizeof(float) * vertexStrideInFloats;

        const size_t normalsOffset = loadNormals ? 3u : 0u;
        const size_t tangentsOffset = loadTangents ? 3u + normalsOffset : normalsOffset;
        // even if we're not loading them from an obj, we need to account for them now by leaving 3 empty floats
        // in between the normals and UVs
        const size_t uvsOffset = tangentsOffset + 3u;

        // Quickly build the metadata entries
        results.vertexMetadata.emplace_back(VertexMetadataEntry{ 0, (uint32_t)VK_FORMAT_R32G32B32_SFLOAT, 0u });
        uint32_t uvsLocation = 1;
        if (loadNormals)
        {
            results.vertexMetadata.emplace_back(VertexMetadataEntry{ 1, (uint32_t)VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(normalsOffset * sizeof(float)) });
            ++uvsLocation;
        }
        if (loadTangents)
        {
            results.vertexMetadata.emplace_back(VertexMetadataEntry{ 2, (uint32_t)VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(tangentsOffset * sizeof(float)) });
            ++uvsLocation;
        }
        results.vertexMetadata.emplace_back(VertexMetadataEntry{ uvsLocation, (uint32_t)VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(uvsOffset * sizeof(float)) });

        const size_t numVerts = OBJverts.size();
        auto& vertexDataRef = results.vertexData;
        for (size_t i = 0; i < numVerts; ++i)
        {
            const OBJvertex& currentVertex = OBJverts[i];
            const size_t j = i * vertexStrideInFloats;

            // I use std::copy in here mostly in the hope it might optimize to something nice
            if (currentVertex.posIdx > 0)
            {
                auto& position = positions[currentVertex.posIdx - 1];
                std::copy(position.data(), position.data() + 3u, vertexDataRef.begin() + j);
            }

            if (currentVertex.normalIdx > 0)
            {
                auto& normal = normals[currentVertex.normalIdx - 1];
                std::copy(normal.data(), normal.data() + 3u, vertexDataRef.begin() + j + normalsOffset);
            }

            if (currentVertex.uvIdx > 0)
            {
                auto& uv = uvs[currentVertex.uvIdx - 1];
                std::copy(uv.data(), uv.data() + 2u, vertexDataRef.begin() + j + uvsOffset);
            }

        }

        const size_t numFaces = OBJfaces.size();
        auto& indexDataRef = results.indices;
        indexDataRef.reserve(numFaces * 3u);

        for (size_t i = 0; i < numFaces; ++i)
        {
            OBJface& face = OBJfaces[i];

            face.indexStart = static_cast<uint32_t>(indexDataRef.size());
            uint32_t baseIndex = face.indexStart;

            for (uint32_t vertexIndex = face.startVertexIdx + 2; vertexIndex < face.endVertexIdx; ++vertexIndex)
            {
                indexDataRef.emplace_back(baseIndex);
                indexDataRef.emplace_back(vertexIndex - 1u);
                indexDataRef.emplace_back(vertexIndex);
            }
        }

        // suggested by original implementer of this method
        OBJfaces.emplace_back(OBJface{ 0, 0, static_cast<uint32_t>(results.indices.size()) });

    }

    void ObjFileData::prepareMaterialsAndGroups(ObjectModelDataImpl& results)
    {
        // This is literally two sets of transformations, which ends up being quite apt with usage of std::transform
        // We're just changing that "space" the indices are referred to in so that it'll match the new storage layout
       
        auto transformMaterialRange = [&](OBJMtlRange& range)->MaterialRange
        {
            uint32_t startIndex = OBJfaces[range.startFaceIndex].indexStart;
            uint32_t endIndex = OBJfaces[range.endFaceIndex].indexStart;
            return MaterialRange{ range.mtlName, startIndex, endIndex - startIndex };
        };

        // otherwise we use back_inserter, which is just as inefficient as repeated push_backs
        results.materialRanges.resize(OBJMtlRanges.size());
        std::transform(OBJMtlRanges.begin(), OBJMtlRanges.end(), results.materialRanges.begin(), transformMaterialRange);

        auto transformPrimitiveGroup = [&](OBJgroup& objGroup)->PrimitiveGroup
        {
            uint32_t startIndex = OBJfaces[objGroup.startFaceIndex].indexStart;
            uint32_t endIndex = OBJfaces[objGroup.endFaceIndex].indexStart;
            return PrimitiveGroup{ objGroup.groupName, objGroup.startMtlIndex, objGroup.endMtlIndex - objGroup.startMtlIndex };
        };

        results.primitiveGroups.resize(groups.size());
        std::transform(groups.begin(), groups.end(), results.primitiveGroups.begin(), transformPrimitiveGroup);

    }

    void ObjFileData::releaseData()
    {
        positions.clear();
        positions.shrink_to_fit();
        normals.clear();
        normals.shrink_to_fit();
        uvs.clear();
        uvs.shrink_to_fit();
        groups.clear();
        groups.shrink_to_fit();
        OBJverts.clear();
        OBJverts.shrink_to_fit();
        OBJfaces.clear();
        OBJfaces.shrink_to_fit();
        OBJMtlRanges.clear();
        OBJMtlRanges.shrink_to_fit();
    }
    
}

ccDataHandle LoadObjModelFromFile(
    const char* model_filename,
    const char* material_directory,
    RequiresNormals requires_normals,
    RequiresTangents requires_tangents,
    OptimizeMesh optimize_mesh)
{
    // Start context by opening requested file: we will run a QUICK hashing algorithm on it as a checksum
    using namespace ObjLoader;

    ObjFileData fileData;
    mango::XX3HASH64 fileChecksum;
    
    {
        ObjFile modelFile(model_filename);
        const ccDataHandle handle{ modelFile.checksum };
        // Check to see if we already loaded this file
        ObjectModelDataImpl* existingData = TryAndGetModelData(handle);
        if (existingData != nullptr)
        {
            return handle;
        }
        fileChecksum = modelFile.checksum;

        fileData.ParseFile(modelFile, static_cast<bool>(requires_normals), static_cast<bool>(requires_tangents));
    }

    ccDataHandle modelHandle = fileChecksum;
    AddObjectModelData(modelHandle, fileData.RetrieveData(static_cast<bool>(requires_normals), static_cast<bool>(requires_tangents)));
    return modelHandle;
}

ObjectModelData RetrieveLoadedObjModel(ccDataHandle handle)
{
    ObjectModelDataImpl* loadedData = TryAndGetModelData(handle);
    if (loadedData != nullptr)
    {
        return (ObjectModelData)*loadedData;
    }
    else
    {
        return ObjectModelData();
    }
}

namespace
{
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
            if (result.y > 1.0f)
            {
                result.y -= 1.0f;
            }
            if (result.x > 1.0f)
            {
                result.x -= 1.0f;
            }
            // extracting UVs: need to flip Y of uv
            result.y = 1.0f - result.y;
        }
        return result;
    }
}