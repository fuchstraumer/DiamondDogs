#pragma once
#ifndef VPSK_RENDER_RESOURCE_HPP
#define VPSK_RENDER_RESOURCE_HPP
#include <cstdint>
namespace vpsk {

    struct RendererResource {
        enum resource_type : uint16_t {
            INDIRECT_BUFFER, VERTEX_BUFFER, INDEX_BUFFER, STORAGE_BUFFER,
            UNIFORM_TEXEL_BUFFER, STORAGE_TEXEL_BUFFER, UNIFORM_BUFFER,
            STORAGE_BUFFER, COLOR_ATTACHMENT, DEPTH_ATTACHMENT, TEXTURE,
            STORAGE_IMAGE, VERTEX_BUFFER_SCHEMA, 
        };
        enum memory_type : uint8_t {
            DEVICE_LOCAL, HOST_COHERENT, HOST_VISIBLE
        };
        enum update_frequency : uint8_t {
            STATIC, UPDATEABLE, DYNAMIC
        };
        RendererResource(uint16_t type, uint8_t memory_flags, uint8_t update_flags, uint32_t index);
        uint16_t Type() const noexcept;
        uint8_t MemoryType() const noexcept;
        uint8_t UpdateFrequency() const noexcept;
        uint32_t Index() const noexcept;
        uint64_t handle;
    };

    RendererResource::RendererResource(uint16_t type, uint8_t memory_flags, uint8_t update_flags, uint32_t index) : handle(0) {
        handle &= index;
        handle &= (update_flags << 32);
        handle &= (memory_flags << 40);
        handle &= (type << 48);
    }

    uint16_t RendererResource::Type() const noexcept {
        return uint16_t(handle >> 48);
    }

    uint32_t RendererResource::Index() const noexcept {
        return uint32_t(handle);
    }

}

#endif //!VPSK_RENDER_RESOURCE_HPP