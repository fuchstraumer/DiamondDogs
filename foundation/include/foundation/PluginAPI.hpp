#pragma once
#ifndef PLUGIN_API_HPP
#define PLUGIN_API_HPP

/**
 * @file PluginAPI.hpp
 * @brief Cross-platform macros for shared library symbol visibility
 * 
 * Provides consistent macros for exporting and importing symbols across
 * different compilers and platforms (MSVC and GCC).
 */

#if defined(_MSC_VER)
/// Export C function symbol for MSVC
#define API_C_EXPORT extern "C" __declspec(dllexport)
/// Import C function symbol for MSVC
#define API_C_IMPORT extern "C" __declspec(dllimport)
/// Export C++ symbol for MSVC
#define API_EXPORT __declspec(dllexport)
/// Import C++ symbol for MSVC
#define API_IMPORT __declspec(dllimport)
#endif

#if defined(__GNUC__)
/// Export C function symbol for GCC
#define API_C_EXPORT extern "C" __attribute__((visibility("default")))
/// Export C++ symbol for GCC
#define API_EXPORT __attribute__((visibility("default")))
/// Import symbols are not needed for GCC
#define API_IMPORT 
/// Import C symbols are not needed for GCC
#define API_C_IMPORT 
#endif

#ifdef BUILDING_SHARED_LIBRARY
/// Plugin API macro when building shared library
#define PLUGIN_API API_C_EXPORT
#elif defined(USING_SHARED_LIBRARY)
/// Plugin API macro when using shared library
#define PLUGIN_API API_C_IMPORT
#else
/// Plugin API macro for static linking
#define PLUGIN_API 
#endif

#ifdef BUILDING_SHARED_LIBRARY
/// Object API macro when building shared library
#define OBJECT_API API_EXPORT
#elif defined(USING_SHARED_LIBRARY)
/// Object API macro when using shared library
#define OBJECT_API API_IMPORT
#else
/// Object API macro for static linking
#define OBJECT_API 
#endif

#endif //!PLUGIN_API_HPP
