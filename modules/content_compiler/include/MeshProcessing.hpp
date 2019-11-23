#pragma once
#ifndef CONTENT_COMPILER_MESH_PROCESSING_HPP
#define CONTENT_COMPILER_MESH_PROCESSING_HPP
#include "MeshData.hpp"

void SortMeshMaterialRanges(const ccDataHandle handle);
void RemoveUnusedVertices(const ccDataHandle handle);
void RemoveDuplicatedVertices(const ccDataHandle handle);
void GenerateTangents(const ccDataHandle handle);

#endif // !CONTENT_COMPILER_MESH_PROCESSING_HPP
