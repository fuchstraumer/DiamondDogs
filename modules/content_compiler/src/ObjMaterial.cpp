#include "ObjMaterial.hpp"
#include "svUtil.hpp"
#include "mango/filesystem/filesystem.hpp"
#include "mango/core/memory.hpp"
#include "mango/core/hash.hpp"
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <charconv>

struct MaterialFileTextures
{
    std::string_view ambient;
    std::string_view diffuse;
    std::string_view specular;
    std::string_view specularHighlight;
    std::string_view bump;
    std::string_view displacement;
    std::string_view alpha;
    std::string_view reflection;
    std::string_view roughness;
    std::string_view metallic;
    std::string_view sheen;
    std::string_view emissive;
    std::string_view normal;
};

struct FileLoadedMaterial
{
    std::string_view name;
    ObjMaterialData data;
    MaterialFileTextures textures;
};

struct LoadedMtlFileData
{
    std::vector<ObjMaterial> materials;
    std::vector<ccDataHandle> textures;
};

void extractVectorMtlParam(std::vector<std::string_view>& svs, float* dest, const size_t startIndex = 1)
{
    const size_t numIterations = svs.size() - startIndex;
    for (size_t i = 0; i < numIterations; ++i)
    {
        std::from_chars(svs[i].data(), svs[i].data() + svs[i].size(), dest[i]);
    }
}

template<typename T>
void extractScalarMtlParam(const std::string_view& sv, T& val)
{
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
}

ccDataHandle LoadMaterialFile(const char* fname, const char* directory)
{
    namespace fs = std::filesystem;
    
    fs::path filePath(fname);
    if (directory)
    {
        fs::path dirPath(directory);
        if (fs::exists(dirPath))
        {
            filePath = fs::path(directory) / filePath;
        }
    }

    if (!fs::exists(filePath))
    {
        throw std::runtime_error("Invalid material path passed to material loader");
    }

    mango::filesystem::File materialFile(filePath.string());
    mango::ConstMemory materialFileMemory = materialFile;

    mango::XX3HASH64 mtlHash = mango::xx3hash64(0xfeed, materialFileMemory);
    ccDataHandle resultMaterialHandle{ mtlHash };

    std::string_view materialStrView(reinterpret_cast<const char*>(materialFileMemory.address), materialFileMemory.size);

    // We store to an intemediary format first
    std::vector<FileLoadedMaterial> loadedMaterials;
    // hash of string view is lexicographic, so this is valid to do
    std::unordered_set<std::string_view> uniqueTextures;

    bool hasD{ false };
    bool hasTr{ false };
#
    while (!materialStrView.empty())
    {
        using namespace svutil;
        auto line = get_line(materialStrView);

        // Lots of material files use this format, sticking a tab before new parameters
        // It structures the output but since we're just iterating line-by-line it's sort of a moot point
        size_t tabIndex = line.find_first_of('\t');
        if (tabIndex == 0)
        {
            line.remove_prefix(1);
        }

        size_t spaceIndex = line.find_last_of(' ');
        if (spaceIndex <= 4 && tabIndex > 0)
        {
            line.remove_prefix(spaceIndex + 1);
        }

        auto tokens = separate_tokens_in_view(line, ' ');

        if (tokens.empty())
        {
            continue;
        }


        if (tokens[0] == "newmtl")
        {
            loadedMaterials.emplace_back(FileLoadedMaterial());
            loadedMaterials.back().name = tokens[1];
            // reset these
            hasD = false;
            hasTr = false;
        }
        else if (tokens[0].substr(0, 4) == "map_")
        {
            tokens[0].remove_prefix(4);
            if (tokens[0] == "Ka" || tokens[0] == "ka")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.ambient = tokens[1];
            }
            else if (tokens[0] == "Kd" || tokens[0] == "kd")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.diffuse = tokens[1];
            }
            else if (tokens[0] == "Ks" || tokens[0] == "ks")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.specular = tokens[1];
            }
            else if (tokens[0] == "Ns" || tokens[0] == "ns")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.specularHighlight = tokens[1];
            }
            else if (tokens[0] == "bump" || tokens[0] == "Bump")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.bump = tokens[1];
            }
            else if (tokens[0] == "d" || tokens[0] == "D")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.alpha = tokens[1];
            }
            else if (tokens[0] == "Pr" || tokens[0] == "pr")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.roughness = tokens[1];
            }
            else if (tokens[0] == "Pm" || tokens[0] == "pm")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.metallic = tokens[1];
            }
            else if (tokens[0] == "Ps" || tokens[0] == "ps")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.sheen = tokens[1];
            }
            else if (tokens[0] == "Ke" || tokens[0] == "ke")
            {
                uniqueTextures.emplace(tokens[1]);
                loadedMaterials.back().textures.emissive = tokens[1];
            }
        }
        else if (tokens[0] == "disp")
        {
            uniqueTextures.emplace(tokens[1]);
            loadedMaterials.back().textures.displacement = tokens[1];
        }
        else if (tokens[0] == "refl")
        {
            uniqueTextures.emplace(tokens[1]);
            loadedMaterials.back().textures.reflection = tokens[1];
        }
        else if (tokens[0] == "bump")
        {
            uniqueTextures.emplace(tokens[1]);
            loadedMaterials.back().textures.bump = tokens[1];
        }
        else if (tokens[0] == "norm")
        {
            uniqueTextures.emplace(tokens[1]);
            loadedMaterials.back().textures.normal = tokens[1];
        }
        else if (tokens[0] == "Ka" || tokens[0] == "ka")
        {
            extractVectorMtlParam(tokens, loadedMaterials.back().data.ambient);
        }
        else if (tokens[0] == "Kd" || tokens[0] == "kd")
        {
            extractVectorMtlParam(tokens, loadedMaterials.back().data.diffuse);
        }
        else if (tokens[0] == "Ks" || tokens[0] == "ks")
        {
            extractVectorMtlParam(tokens, loadedMaterials.back().data.specular);
        }
        else if (tokens[0] == "Kt" || tokens[0] == "Tf" || tokens[0] == "kt" || tokens[0] == "tf")
        {
            extractVectorMtlParam(tokens, loadedMaterials.back().data.transmittance);
        }
        else if (tokens[0] == "Ni" || tokens[0] == "ni")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.ior);
        }
        else if (tokens[0] == "Ns" || tokens[0] == "ns")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.shininess);
        }
        else if (tokens[0] == "illum")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.illuminationModel);
        }
        else if ((tokens[0] == "Tr" || tokens[0] == "tr") && !hasD)
        {
            hasTr = true;
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.dissolve);
        }
        else if ((tokens[0] == "d" || tokens[0] == "D") && !hasTr)
        {
            hasD = true;
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.dissolve);
        }
        else if (tokens[0] == "Pr" || tokens[0] == "pr")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.roughness);
        }
        else if (tokens[0] == "Pm" || tokens[0] == "pm")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.roughness);
        }
        else if (tokens[0] == "Ps" || tokens[0] == "ps")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.sheen);
        }
        else if (tokens[0] == "Pc" || tokens[0] == "pc")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.clearcoatThickness);
        }
        else if (tokens[0] == "Pcr" || tokens[0] == "pcr")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.clearcoatRoughness);
        }
        else if (tokens[0] == "aniso")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.anisotropy);
        }
        else if (tokens[0] == "anisor")
        {
            extractScalarMtlParam(tokens[1], loadedMaterials.back().data.anisotropyRotation);
        }
    }

    std::vector<std::string_view> uniqueTexturesVec;
    uniqueTexturesVec.reserve(uniqueTextures.size());
    for (auto sv : uniqueTextures)
    {
        uniqueTexturesVec.emplace_back(sv);
    }

    

    LoadedMtlFileData finalData;


    return ccDataHandle();
}
