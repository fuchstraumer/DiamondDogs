#include "PluginManagerImpl.hpp"
#include <dlfcn.h>
#include <iostream>
#ifdef __APPLE_CC__
#include <boost/filesystem.hpp>
#else
#include <experimental/filesystem>
#endif
#include <vector>

static void* GetEngineApiFnPtr(uint32_t id) {
    auto& instance = PluginManager::GetPluginManager();
    return instance.RetrieveAPI(id);
}

PluginManagerImpl::PluginManagerImpl() {}

void PluginManagerImpl::LoadPlugin(const char * fname) {
#ifdef __APPLE_CC__
    namespace fs = boost::filesystem;
#else
    namespace fs = std::experimental::filesystem;
#endif
    fs::path plugin_path(fname);
    if (!fs::exists(plugin_path)) {
        std::cerr << "Given path to plugin does not exist: " << plugin_path.string() << "\n";
    }

    const std::string absolute_path = fs::absolute(plugin_path).string();
    plugin_handle new_handle = dlopen(absolute_path.c_str(), RTLD_LOCAL);

    if (!new_handle) {
        throw std::runtime_error("Failed to load plugin!");
    }
    else {
        plugins.emplace(absolute_path, new_handle);
        void* get_api_fn = dlsym(new_handle, "GetPluginAPI");
        if (!get_api_fn) {
            std::cerr << "Couldn't find function address in plugin.";
        }
        void* core_api_ptr = reinterpret_cast<GetEngineAPI_Fn>(get_api_fn)(0);
        if (!core_api_ptr) {
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

void PluginManagerImpl::UnloadPlugin(const char * fname) {
#ifdef __APPLE_CC__
    namespace fs= boost::filesystem;
#else
    namespace fs = std::experimental::filesystem;
#endif
    const std::string plugin_path = fs::absolute(fs::path(fname)).string();

    auto iter = plugins.find(plugin_path);
    if (iter != std::end(plugins)) {
        uint32_t id = pluginFilesToIDMap.at(plugin_path);
        Plugin_API* api = coreApiPointers.at(id);
        api->Unload();
        coreApiPointers.erase(id);
        apiPointers.erase(id);
        pluginFilesToIDMap.erase(fname);
        dlclose(iter->second);
        plugins.erase(plugin_path);
    }
}

void* PluginManagerImpl::GetEngineAPI(uint32_t api_id) {
    auto iter = apiPointers.find(api_id);
    if (iter == std::end(apiPointers)) {
        return nullptr;
    }
    else {
        return iter->second;
    }
}

void PluginManagerImpl::LoadedPlugins(uint32_t* num_plugins, uint32_t* plugin_ids) const {
    *num_plugins = static_cast<uint32_t>(coreApiPointers.size());
    if (plugin_ids != nullptr) {
        std::vector<uint32_t> results;
        for (const auto& entry : coreApiPointers) {
            results.emplace_back(entry.first);
        }
        std::copy(results.begin(), results.end(), plugin_ids);
    }
}

Plugin_API* PluginManagerImpl::GetCoreAPI(uint32_t api_id) {
    auto iter = coreApiPointers.find(api_id);
    if (iter == std::end(coreApiPointers)) {
        return nullptr;
    }
    else {
        return iter->second;
    }
}
