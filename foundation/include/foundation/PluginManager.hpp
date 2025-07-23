#pragma once
#ifndef PLUGIN_MANAGER_CORE_HPP
#define PLUGIN_MANAGER_CORE_HPP
#include <memory>

struct PluginManagerImpl;

/**
 * @brief Singleton manager for dynamic plugin loading and API access
 * 
 * Provides a centralized system for loading shared libraries as plugins,
 * managing their lifecycle, and providing access to their exported APIs.
 */
class PluginManager {
protected:
    /// Copy constructor deleted - PluginManager is a singleton
    PluginManager(const PluginManager&) = delete;
    /// Copy assignment deleted - PluginManager is a singleton
    PluginManager& operator=(const PluginManager&) = delete;
    /// Protected constructor for singleton pattern
    PluginManager();
    /// Protected destructor for singleton pattern
    ~PluginManager();
public:

    /**
     * @brief Get the singleton instance of PluginManager
     * 
     * @return Reference to the global PluginManager instance
     */
    static PluginManager& GetPluginManager();

    /**
     * @brief Load a plugin from a shared library file
     * 
     * @param fname Path to the plugin file to load
     * @note Plugin must export the required Plugin_API structure
     */
    void LoadPlugin(const char* fname);

    /**
     * @brief Unload a previously loaded plugin
     * 
     * @param fname Path to the plugin file to unload
     */
    void UnloadPlugin(const char* fname);

    /**
     * @brief Retrieve an API interface from a loaded plugin
     * 
     * @param id Unique identifier for the API interface
     * @return Pointer to the API interface, or nullptr if not found
     */
    void* RetrieveAPI(uint32_t id);

    /**
     * @brief Retrieve a base API interface from the engine
     * 
     * @param id Unique identifier for the base API interface
     * @return Pointer to the base API interface, or nullptr if not found
     */
    void* RetrieveBaseAPI(uint32_t id);

    /**
     * @brief Get information about currently loaded plugins
     * 
     * @param num_plugins Pointer to receive the number of loaded plugins
     * @param plugin_ids Array to receive plugin IDs (must be pre-allocated)
     */
    void GetLoadedPlugins(uint32_t* num_plugins, uint32_t* plugin_ids) const;

private:
    std::unique_ptr<PluginManagerImpl> impl;
};

#endif // !PLUGIN_MANAGER_CORE_HPP
