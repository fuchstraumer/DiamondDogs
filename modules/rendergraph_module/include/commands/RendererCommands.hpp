#pragma once
#ifndef VPSK_COMMAND_BUFFER_HPP
#define VPSK_COMMAND_BUFFER_HPP
#include <cstdint>

namespace vpsk {

    // Entails work involving the GPU
    namespace graphics_command_type {
        enum e : uint32_t {
            NO_COMMAND = 0,
            BEGIN_RENDERPASS,
            SET_EVENT,
            RESET_EVENT,
            WAIT_EVENTS,
            PIPELINE_BARRIER,
            BIND_PIPELINE,
            BIND_DESCRIPTOR_SETS,
            PUSH_CONSTANTS,
            RESET_QUERY_POOL,
            BEGIN_QUERY,
            END_QUERY,
            COPY_QUERY_RESULTS,
            WRITE_TIMESTAMP,
            CLEAR_COLOR_IMAGE,
            CLEAR_DEPTH_STENCIL_IMAGE,
            CLEAR_ATTACHMENTS,
            FILL_BUFFER,
            UPDATE_BUFFER,
            BIND_INDEX_BUFFER,
            BIND_VERTEX_BUFFERS,
            DRAW_UNINDEXED,
            DRAW_INDEXED,
            DRAW_INDIRECT,
            DRAW_INDEXED_INDIRECT,
            COPY_BUFFER_TO_BUFFER,
            COPY_IMAGE_TO_IMAGE,
            COPY_BUFFER_TO_IMAGE,
            COPY_IMAGE_TO_BUFFER,
            BLIT_IMAGE,
            RESOLVE_IMAGE,
            DYNAMIC_SET_SCISSOR,
            DYNAMIC_SET_VIEWPORT,
            DYNAMIC_SET_DEPTH_BOUNDS,
            DYNAMIC_SET_STENCIL_COMPARE_MASK,
            DYNAMIC_SET_STENCIL_WRITE_MASK,
            DYNAMIC_SET_STENCIL_REFERENCE,
            DYNAMIC_SET_LINE_WIDTH,
            DYNAMIC_SET_DEPTH_BIAS,
            DISPATCH_COMPUTE,
            DISPATCH_COMPUTE_INDIRECT,
            DISPATCH_COMPUTE_BASE,
            SET_BLEND_CONSTANTS,
            EXECUTE_SECONDARY_CMD_BUFFERS,
            // Following commands are compositions of above commands,
            // which we can describe as higher-level commands
            CREATE_MIP_MAPS,
            STAGING_BUFFER_TO_DEVICE_BUFFER,
            STAGING_BUFFER_TO_DEVICE_IMAGE
        };
    }

    // Submitted to a command system, which sorts commands and then records
    // them into command buffers and submits them into a queue as able
    struct RendererCommandsArray {
        RendererCommandsArray(const size_t num_commands);
        // Will clean up command pointers on destruction
        ~RendererCommandsArray();
        void AddCommand(uint64_t key, uint32_t type, void* cmd);
        const size_t NumCommandsReserved;
        size_t NumCommandsUsed;
        uint64_t* SortKeys;
        uint32_t* Types;
        void** Commands;
    };


}

#endif //!VPSK_COMMAND_BUFFER_HPP