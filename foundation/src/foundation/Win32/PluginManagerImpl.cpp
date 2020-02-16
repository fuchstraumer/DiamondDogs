#include "PluginManagerImpl.hpp"
#include "foundation/PluginManager.hpp"
#include "PDB_Helpers.hpp"
#include <iostream>
#include <filesystem>
#include <functional>

constexpr static auto PATH_SEPARATOR = '\\';
constexpr static auto PATH_SEPARATOR_INVALID = '/';

static void* GetEngineApiFnPtr(uint32_t id)
{
    auto& instance = PluginManager::GetPluginManager();
    return instance.RetrieveAPI(id);
}

static void SplitPath(std::string path, std::string& parent_dir, std::string& base_name, std::string& ext)
{
    std::replace(path.begin(), path.end(), PATH_SEPARATOR_INVALID, PATH_SEPARATOR);
    const size_t sep_pos = path.rfind(PATH_SEPARATOR);
    const size_t dot_pos = path.rfind('.');

    if (sep_pos == std::string::npos)
    {
        parent_dir = "";
        if (dot_pos == std::string::npos)
        {
            ext = "";
            base_name = path;
        }
        else
        {
            ext = path.substr(dot_pos);
            base_name = path.substr(0, dot_pos);
        }
    }
    else
    {
        parent_dir = path.substr(0, sep_pos + 1);
        if (dot_pos == std::string::npos || sep_pos > dot_pos)
        {
            ext = "";
            base_name = path.substr(sep_pos + 1);
        }
        else
        {
            ext = path.substr(dot_pos);
            base_name = path.substr(sep_pos + 1, dot_pos - sep_pos - 1);
        }
    }
}

static std::string ReplaceExtension(const std::string& file_path, const std::string& ext)
{
    std::string folder, filename, old_ext;
    SplitPath(file_path, folder, filename, old_ext);
    return folder + filename + ext;
}


PluginManagerImpl::PluginManagerImpl() {}

void PluginManagerImpl::LoadPlugin(const char* fname)
{
    namespace fs = std::filesystem;

    fs::path plugin_path(fname);
    if (!fs::exists(plugin_path))
    {
        std::cerr << "Given path to plugin does not exist";
    }

    std::string new_pdb = ReplaceExtension(plugin_path.string(), ".pdb");
    // will need to handle this better eventually, since wchar_t needed for wide strings and localization...
    const std::wstring new_pdb_wstr{ new_pdb.begin(), new_pdb.end() };

    if (!ProcessPDB(plugin_path.wstring(), new_pdb_wstr))
    {
        std::cerr << "Couldn't process/find PDB for loaded library - debugging may be affected!\n";
    }

    const auto absolute_path = fs::absolute(plugin_path).wstring();
    HMODULE new_handle = LoadLibrary(absolute_path.c_str());

    if (!new_handle)
    {
        DWORD err = GetLastError();
        std::cerr << "failed to load plugin: " << std::to_string(err) << " \n";
    }
    else
    {
        plugins.emplace(absolute_path, new_handle);

        void* get_api_fn = GetProcAddress(new_handle, "GetPluginAPI");
        if (!get_api_fn)
        {
            std::cerr << "Couldn't find function address in plugin.";
        }

        void* core_api_ptr = reinterpret_cast<GetEngineAPI_Fn>(get_api_fn)(0);
        if (!core_api_ptr)
        {
            std::cerr << "Plugin failed to return core API pointer!\n";
        }

        Plugin_API* api = reinterpret_cast<Plugin_API*>(core_api_ptr);
        uint32_t id = api->PluginID();
        pluginFilesToIDMap.emplace(absolute_path, id);
        coreApiPointers.emplace(id, api);

        void* unique_api = reinterpret_cast<GetEngineAPI_Fn>(get_api_fn)(id);
        apiPointers.emplace(id, unique_api);

        api->Load(GetEngineApiFnPtr);
       
    }
}

void PluginManagerImpl::UnloadPlugin(const char* fname)
{
    namespace fs = std::filesystem;
    std::string fnameTemp{ fname };
    const std::wstring wfname{ fnameTemp.begin(), fnameTemp.end() };
    const std::wstring plugin_path = fs::absolute(fs::path(fname)).wstring();

    auto iter = plugins.find(plugin_path);
    if (iter != std::end(plugins))
    {
        uint32_t id = pluginFilesToIDMap.at(plugin_path);
        Plugin_API* api = coreApiPointers.at(id);
        api->Unload();
        coreApiPointers.erase(id);
        apiPointers.erase(id);
        pluginFilesToIDMap.erase(wfname);
        FreeLibrary(iter->second);
        plugins.erase(plugin_path);
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

Plugin_API* PluginManagerImpl::GetCoreAPI(uint32_t api_id)
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
