#pragma once
#ifndef PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
#define PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
#include <cstdint>

typedef void*(*GetEngineAPI_Fn)(uint32_t api_id);

struct Plugin_API {
    const char* (*PluginName)(void);
    uint32_t (*PluginID)(void);
    // Called when plugin is first loaded
    void (*Load)(GetEngineAPI_Fn engine_api_fn);
    // Called right before dlclose or process detachment
    void (*Unload)(void);
    // Returns pointer to state data for given plugin. Used to later restore state.
    void* (*BeginReload)(GetEngineAPI_Fn engine_api_fn);
    // Complete reloading of a plugin, using state data stored in given pointer. Plugin needs to free data.
    void (*FinishReload)(GetEngineAPI_Fn engine_api_fn, void* state_data);
    // Fixed-timestep update of logical components not requiring updates w/ timestep value
    void (*LogicalUpdate)(void);
    // Called per frame, dt is frametime. use for time-dependent updates like physical systems and simulations
    void (*TimeDependentUpdate)(double dt);
    // For potential future API expansion
    void* ReservedFns[24];
};

#endif //!PLUGIN_MANAGER_CORE_API_DECLARATIONS_HPP
