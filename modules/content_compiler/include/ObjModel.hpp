#pragma once
#ifndef RESOURCE_CONTEXT_OBJ_MODEL_HPP
#define RESOURCE_CONTEXT_OBJ_MODEL_HPP
#include "utility/tagged_bool.hpp"
#include "MeshData.hpp"
#include <cstdint>

struct ObjectModelLoader;

using RequiresNormals = tagged_bool<struct RequiresNormalsTag>;
using RequiresTangents = tagged_bool<struct RequiresTangentsTag>;
using OptimizeMesh = tagged_bool<struct OptimizeMeshTag>;

ccDataHandle LoadObjModelFromFile(
    const char* model_filename,
    const char* material_directory,
    RequiresNormals requires_normals,
    RequiresTangents requires_tangents,
    OptimizeMesh optimize_mesh);


#endif //!RESOURCE_CONTEXT_OBJ_MODEL_HPP
