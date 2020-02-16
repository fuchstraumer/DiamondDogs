#pragma once
#ifndef PLUGIN_API_HPP
#define PLUGIN_API_HPP

#if defined(_MSC_VER)
#define API_C_EXPORT extern "C" __declspec(dllexport)
#define API_C_IMPORT extern "C" __declspec(dllimport)
#define API_EXPORT __declspec(dllexport)
#define API_IMPORT __declspec(dllimport)
#endif

#if defined(__GNUC__)
#define API_C_EXPORT extern "C" __attribute__((visibility("default")))
#define API_EXPORT __attribute__((visibility("default")))
#define API_IMPORT 
#define API_C_IMPORT 
#endif

#ifdef BUILDING_SHARED_LIBRARY
#define PLUGIN_API API_C_EXPORT
#elif defined(USING_SHARED_LIBRARY)
#define PLUGIN_API API_C_IMPORT
#else
#define PLUGIN_API 
#endif

#ifdef BUILDING_SHARED_LIBRARY
#define OBJECT_API API_EXPORT
#elif defined(USING_SHARED_LIBRARY)
#define OBJECT_API API_IMPORT
#else
#define OBJECT_API 
#endif

#endif //!PLUGIN_API_HPP
