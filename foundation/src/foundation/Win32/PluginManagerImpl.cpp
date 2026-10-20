#include "PluginManagerImpl.hpp"
#include "foundation/PluginManager.hpp"
#include "PDB_Helpers.hpp"
#include <iostream>
#include <filesystem>
#include <functional>

constexpr static auto k_PathSeparator = '\\';
constexpr static auto k_PathSeparatorInvalid = '/';

static void* GetEngineApiFnPtr(uint32_t id)
{
    auto& instance = PluginManager::GetPluginManager();
    return instance.RetrieveAPI(id);
}

static void SplitPath(std::string path, std::string& parent_dir, std::string& base_name, std::string& ext)
{
    std::replace(path.begin(), path.end(), k_PathSeparatorInvalid, k_PathSeparator);
    const size_t sepPos = path.rfind(k_PathSeparator);
    const size_t dotPos = path.rfind('.');

    if (sepPos == std::string::npos)
    {
        parent_dir = "";
        if (dotPos == std::string::npos)
        {
            ext = "";
            base_name = path;
        }
        else
        {
            ext = path.substr(dotPos);
            base_name = path.substr(0, dotPos);
        }
    }
    else
    {
        parent_dir = path.substr(0, sepPos + 1);
        if (dotPos == std::string::npos || sepPos > dotPos)
        {
            ext = "";
            base_name = path.substr(sepPos + 1);
        }
        else
        {
            ext = path.substr(dotPos);
            base_name = path.substr(sepPos + 1, dotPos - sepPos - 1);
        }
    }
}

static std::string ReplaceExtension(const std::string& file_path, const std::string& ext)
{
    std::string folder, filename, oldExt;
    SplitPath(file_path, folder, filename, oldExt);
    return folder + filename + ext;
}


PluginManagerImpl::PluginManagerImpl() {}

void PluginManagerImpl::LoadPlugin(const char* fname)
{
    namespace fs = std::filesystem;

    fs::path pluginPath(fname);
    if (!fs::exists(pluginPath))
    {
        std::cerr << "Given path to plugin does not exist";
    }

    std::string newPdb = ReplaceExtension(pluginPath.string(), ".pdb");
    // will need to handle this better eventually, since wchar_t needed for wide strings and localization...
    const std::wstring newPdbWstr{ newPdb.begin(), newPdb.end() };

    if (!ProcessPDB(pluginPath.wstring(), newPdbWstr))
    {
        std::cerr << "Couldn't process/find PDB for loaded library - debugging may be affected!\n";
    }

    // This used to take a wstring... what changed?
    const std::wstring absolutePathWstr = fs::absolute(pluginPath).wstring();
    const std::string absolutePathStr = fs::absolute(pluginPath).string();
    HMODULE newHandle = LoadLibrary(absolutePathStr.c_str());

    if (!newHandle)
    {
        DWORD err = GetLastError();
        std::cerr << "failed to load plugin: " << std::to_string(err) << " \n";
    }
    else
    {
        plugins.emplace(absolutePathWstr, newHandle);

        void* getApiFn = GetProcAddress(newHandle, "GetPluginAPI");
        if (!getApiFn)
        {
            std::cerr << "Couldn't find function address in plugin.";
        }

        void* coreApiPtr = reinterpret_cast<GetEngineApiFn>(getApiFn)(0);
        if (!coreApiPtr)
        {
            std::cerr << "Plugin failed to return core API pointer!\n";
        }

        PluginApi* api = reinterpret_cast<PluginApi*>(coreApiPtr);
        uint32_t id = api->PluginID();
        pluginFilesToIDMap.emplace(absolutePathWstr, id);
        coreApiPointers.emplace(id, api);

        void* uniqueApi = reinterpret_cast<GetEngineApiFn>(getApiFn)(id);
        apiPointers.emplace(id, uniqueApi);

        api->Load(GetEngineApiFnPtr);
       
    }
}

void PluginManagerImpl::UnloadPlugin(const char* fname)
{
    namespace fs = std::filesystem;
    std::string fnameTemp{ fname };
    const std::wstring wfname{ fnameTemp.begin(), fnameTemp.end() };
    const std::wstring pluginPath = fs::absolute(fs::path(fname)).wstring();

    auto iter = plugins.find(pluginPath);
    if (iter != std::end(plugins))
    {
        uint32_t id = pluginFilesToIDMap.at(pluginPath);
        PluginApi* api = coreApiPointers.at(id);
        api->Unload();
        coreApiPointers.erase(id);
        apiPointers.erase(id);
        pluginFilesToIDMap.erase(wfname);
        FreeLibrary(iter->second);
        plugins.erase(pluginPath);
    }
}

void* PluginManagerImpl::GetEngineAPI(uint32_t api_id)
{
    auto iter = apiPointers.find(api_id);
    if (iter == std::end(apiPointers))
    {
        return nullptr;
    }
    else
    {
        return iter->second;
    }
}

void PluginManagerImpl::LoadedPlugins(uint32_t* num_plugins, uint32_t* plugin_ids) const
{
    *num_plugins = static_cast<uint32_t>(coreApiPointers.size());
    if (plugin_ids != nullptr)
    {
        std::vector<uint32_t> results;
        for (const auto& entry : coreApiPointers)
        {
            results.emplace_back(entry.first);
        }
        std::copy(results.begin(), results.end(), plugin_ids);
    }
}

PluginApi* PluginManagerImpl::GetCoreAPI(uint32_t api_id)
{
    auto iter = coreApiPointers.find(api_id);
    if (iter == std::end(coreApiPointers))
    {
        return nullptr;
    }
    else
    {
        return iter->second;
    }
}
