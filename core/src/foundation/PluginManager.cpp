#include "foundation/PluginManager.hpp"
#include "foundation/CoreAPIs.hpp"
#include "PluginManagerImpl.hpp"

PluginManager::PluginManager() : impl(std::make_unique<PluginManagerImpl>()) {}

PluginManager::~PluginManager() {}

PluginManager& PluginManager::GetPluginManager()
{
    static PluginManager manager;
    return manager;
}

void PluginManager::LoadPlugin(const char * fname)
{
    impl->LoadPlugin(fname);
}

void PluginManager::UnloadPlugin(const char * fname)
{
    impl->UnloadPlugin(fname);
}

void* PluginManager::RetrieveAPI(uint32_t id)
{
    return impl->GetEngineAPI(id);
}

void* PluginManager::RetrieveBaseAPI(uint32_t id)
{
    return impl->GetCoreAPI(id);
}

void PluginManager::GetLoadedPlugins(uint32_t * num_plugins, uint32_t * plugin_ids) const
{
    impl->LoadedPlugins(num_plugins, plugin_ids);
}
