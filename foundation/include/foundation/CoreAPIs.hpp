#pragma once
#ifndef PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
#define PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
#include <cstdint>

/// Function pointer type for retrieving engine APIs
typedef void*(*GetEngineAPI_Fn)(uint32_t api_id);

/**
 * @brief Standard API structure that all plugins must export
 * 
 * Defines the required functions that the plugin manager will call
 * for plugin lifecycle management and updates.
 */
struct Plugin_API {
    /// Get the human-readable name of this plugin
    const char* (*PluginName)(void);
    /// Get the unique numerical identifier for this plugin
    uint32_t (*PluginID)(void);
    /// Called when plugin is first loaded
    void (*Load)(GetEngineAPI_Fn engine_api_fn);
    /// Called right before dlclose or process detachment
    void (*Unload)(void);
    /// Returns pointer to state data for given plugin, used to later restore state
    void* (*BeginReload)(GetEngineAPI_Fn engine_api_fn);
    /// Complete reloading of a plugin, using state data stored in given pointer
    void (*FinishReload)(GetEngineAPI_Fn engine_api_fn, void* state_data);
    /// Fixed-timestep update of logical components not requiring updates w/ timestep value
    void (*LogicalUpdate)(void);
    /// Called per frame, dt is frametime - use for time-dependent updates like physics
    void (*TimeDependentUpdate)(double dt);
    /// Reserved function pointers for future API expansion
    void* ReservedFns[24];
};

#endif //!PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
