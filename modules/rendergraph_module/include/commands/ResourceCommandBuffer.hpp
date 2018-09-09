#pragma once
#ifndef VPSK_RESOURCE_COMMAND_BUFFER_HPP
#define VPSK_RESOURCE_COMMAND_BUFFER_HPP
#include <variant>

namespace vpsk {

    struct RendererBufferInfo {
        size_t Size;
        enum usage_e : uint32_t {
            EMPTY_USAGE = 0,
            VERTEX_BUFFER,
            INDEX_BUFFER,
            STORAGE_BUFFER,
            STORAGE_TEXEL_BUFFER,
            UNIFORM_TEXEL_BUFFER,
            INDIRECT_BUFFER,
            TRANSFER_SRC,
            TRANSFER_DEST
        } Usage{ EMPTY_USAGE };
        enum memory_flags_e : uint32_t {
            EMPTY_RESIDENCY = 0,
            DEVICE_LOCAL,
            HOST_VISIBLE,
            HOST_VISIBLE_AND_COHERENT
        } MemoryResidency { EMPTY_RESIDENCY };
    };

    struct RendererImage {
        uint32_t Handle;
        uint32_t SizeX;
        uint32_t SizeY;
        uint32_t SizeZ;

    };

    struct RendererImageView {

    };

    struct RendererSampler {

    };

    struct RendererShader {

    };


}

#endif //!VPSK_RESOURCE_COMMAND_BUFFER_HPP
