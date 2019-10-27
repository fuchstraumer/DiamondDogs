#pragma once
#ifndef PLUGIN_MANAGER_UNIX_IMPL_HPP
#define PLUGIN_MANAGER_UNIX_IMPL_HPP
#include "PluginManager.hpp"
#include "CoreAPIs.hpp"
#include <string>
#include <unordered_map>

using plugin_handle = void*;

struct PluginManagerImpl {
    PluginManagerImpl();
    void LoadPlugin(const char* fname);
    void UnloadPlugin(const char* fname);
    void* GetEngineAPI(uint32_t api_id);
    void LoadedPlugins(uint32_t * num_plugins, uint32_t * plugin_ids) const;
    Plugin_API* GetCoreAPI(uint32_t api_id);
    std::unordered_map<std::string, plugin_handle> plugins;
    std::unordered_map<std::string, uint32_t> pluginFilesToIDMap;
    // Each plugin should expose a Plugin_API*
    std::unordered_map<uint32_t, Plugin_API*> coreApiPointers;
    // The unique plugin APIs loaded are stored here
    std::unordered_map<uint32_t, void*> apiPointers; 
};

#endif //!PLUGIN_MANAGER_UNIX_IMPL_HPP
