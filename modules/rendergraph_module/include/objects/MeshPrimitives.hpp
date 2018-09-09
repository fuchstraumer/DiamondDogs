#pragma once
#ifndef VPSK_MESH_PRIMITIVE_GENERATORS_HPP
#define VPSK_MESH_PRIMITIVE_GENERATORS_HPP
#include <memory>

namespace vpsk {

    struct MeshData;

    enum class ExtraFeatures {
        None,
        GenerateTangents
    };

    std::unique_ptr<MeshData> CreateBox(const ExtraFeatures features = ExtraFeatures::None);
    std::unique_ptr<MeshData> CreateIcosphere(const size_t detail_level, const ExtraFeatures features = ExtraFeatures::None);
    void GenerateTangentVectors(MeshData* mesh);

}

#endif //!VPSK_MESH_PRIMITIVE_GENERATORS_HPP
