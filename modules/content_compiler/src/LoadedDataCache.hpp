#pragma once
#ifndef CONTENT_COMPILER_LOADED_DATA_CACHE_HPP
#define CONTENT_COMPILER_LOADED_DATA_CACHE_HPP
#include "MeshData.hpp"
#include "InternalTypes.hpp"
#include <vector>
#include <string>

// Storage cache for internal representations of loaded content, vs the interfaces
// we have for storing client representations

/*
    The internal structure used to create the structure API users get - itself just
    effectively a view of *these* objects
*/

ObjectModelDataImpl* TryAndGetModelData(const ccDataHandle handle);
void AddObjectModelData(const ccDataHandle handle, ObjectModelDataImpl data);

#endif //!CONTENT_COMPILER_LOADED_DATA_CACHE_HPP
