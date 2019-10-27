#pragma once
#ifndef PLUGIN_MANAGER_CORE_HPP
#define PLUGIN_MANAGER_CORE_HPP
#include <memory>

struct PluginManagerImpl;

class PluginManager {
protected:
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager();
    ~PluginManager();
public:

    static PluginManager& GetPluginManager();

    void LoadPlugin(const char* fname);
    void UnloadPlugin(const char* fname);
    void* RetrieveAPI(uint32_t id);
    void* RetrieveBaseAPI(uint32_t id);
    void GetLoadedPlugins(uint32_t* num_plugins, uint32_t* plugin_ids) const;

private:
    std::unique_ptr<PluginManagerImpl> impl;
};

#endif // !PLUGIN_MANAGER_CORE_HPP
