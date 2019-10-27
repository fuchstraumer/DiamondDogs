#pragma once
#ifndef VPSK_COMMAND_TYPES_HPP
#define VPSK_COMMAND_TYPES_HPP
#include <cstdint>
#include <limits>

namespace vpsk {

    struct set_event_cmd_t {
        uint32_t EventHandle;
        uint32_t StageMask;
    };

    struct reset_event_cmd_t : set_event_cmd_t {};
    
    struct wait_events_cmd_t { 
        uint32_t EventCount;
        const uint32_t* EventHandles;
        uint32_t SrcStageMask;
        uint32_t DstStageMask;
        uint32_t MemoryBarrierCount;
        const void* MemoryBarriers;
        uint32_t BufferMemoryBarrierCount;
        const void* BufferMemoryBarriers;
        uint32_t ImageMemoryBarrierCount;
        const void* ImageMemoryBarriers;
    };

    struct pipeline_barrier_cmd_t {
        uint32_t SrcStageMask;
        uint32_t DstStageMask;
        uint32_t DependencyFlags;
        uint32_t MemoryBarrierCount;
        const void* MemoryBarriers;
        uint32_t BufferMemoryBarrierCount;
        const void* BufferMemoryBarriers;
        uint32_t ImageMemoryBarrierCount;
        const void* ImageMemoryBarriers;
    };

    struct begin_renderpass_cmd_t {
        const void* RenderpassBeginInfo;
        uint32_t SubpassContents;
    }; 

    struct next_subpass_cmd_t {
        uint32_t SubpassContents;
    }; 

    struct bind_pipeline_cmd_t { 
        uint32_t BindPoint;
        uint32_t PipelineHandle;
    };

    struct bind_descriptor_sets_cmd_t {
        uint32_t BindPoint;
        uint32_t PipelineLayoutHandle;
        uint32_t FirstSet;
        uint32_t SetCount;
        const void* SetHandles;
        uint32_t DynamicOffsetCount;
        const uint32_t* DynamicOffsets;
    };

    struct push_constants_cmd_t {
        uint32_t PipelineLayoutHandle;
        uint32_t StageFlags;
        uint32_t Offset;
        uint32_t Size;
        const void* Data;
    };

    struct reset_query_pool_cmd_t {
        uint32_t QueryPoolHandle;
        uint32_t FirstQuery;
        uint32_t QueryCount;
    };

    struct begin_query_cmd_t {
        uint32_t QueryPoolHandle;
        uint32_t Entry;
        bool Precise : 1;
    };

    struct end_query_cmd_t {
        uint32_t QueryPoolHandle;
        uint32_t Query;
    };

    struct copy_query_results_cmd_t { 
        uint32_t QueryPoolHandle;
        uint32_t FirstQuery;
        uint32_t QueryCount;
        uint32_t DstBufferHandle;
        uint32_t DstBufferOffset;
        uint32_t Stride;
        uint32_t QueryResultFlags;
    };

    struct write_timestamp_cmd_t {
        uint32_t PipelineStage;
        uint32_t QueryPoolHandle;
        uint32_t Query;
    };

    struct clear_color_image_cmd_t { 
        uint32_t ImageHandle;
        uint32_t ImageLayout;
        union {
            float f32[4];
            int32_t i32[4];
            uint32_t u32[4];
        } ClearColor;
        uint32_t SubrangeCount;
        const void* Subranges;
    };

    struct clear_depth_stencil_image_cmd_t { 
        uint32_t ImageHandle;
        uint32_t ImageLayout;
        float DepthValue;
        uint32_t StencilValue;
        uint32_t SubrangeCount;
        const void* Subranges;
    };

    struct clear_attachments_cmd_t {
        uint32_t AttachmentCount;
        const void* Attachments;
        uint32_t RectCount;
        const void* ClearRects;
    };

    struct fill_buffer_cmd_t { 
        uint32_t BufferHandle;
        uint32_t DstOffset;
        uint32_t Size;
        uint32_t FillValue;
    };

    struct update_buffer_cmd_t { 
        uint32_t BufferHandle;
        uint32_t DstOffset;
        uint32_t DataSize;
        const void* Data;
    };

    struct bind_index_buffer_cmd_t { 
        uint32_t BufferHandle;
        uint32_t Offset;
        uint32_t IndexDataType;
    };

    struct bind_vertex_buffers_cmd_t {
        uint32_t FirstBinding;
        uint32_t BindingCount;
        const uint32_t* BufferHandles;
        const uint64_t* Offsets;
    };

    struct draw_unindexed_cmd_t { 
        uint32_t VertexCount;
        uint32_t InstanceCount;
        uint32_t FirstVertex;
        uint32_t FirstInstance;
    };

    struct draw_indexed_cmd_t { 
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        int32_t VertexOffset;
        uint32_t FirstInstance;
    };

    struct draw_indirect_cmd_t {
        uint32_t BufferHandle;
        uint64_t Offset;
        uint32_t DrawCount;
        uint32_t Stride;
    };

    struct draw_indexed_indirect_cmd_t : draw_indirect_cmd_t { };

    struct copy_buffer_to_buffer_cmd_t { 
        uint32_t SrcBufferHandle;
        uint32_t DstBufferHandle;
        uint32_t CopyRegionCount;
        const void* CopyRegions;
    };

    struct copy_image_to_image_cmd_t { 
        uint32_t SrcImageHandle;
        uint32_t SrcImageLayout;
        uint32_t DstImageHandle;
        uint32_t DstImageLayout;
        uint32_t CopyRegionCount;
        const void* CopyRegions;
    };

    struct copy_buffer_to_image_cmd_t { 
        uint32_t SrcBufferHandle;
        uint32_t DstImageHandle;
        uint32_t DstImageLayout;
        uint32_t CopyRegionCount;
        const void* CopyRegions;
    };

    struct copy_image_to_buffer_cmd_t { 
        uint32_t SrcImageHandle;
        uint32_t SrcImageLayout;
        uint32_t DstBufferHandle;
        uint32_t CopyRegionCount;
        const void* CopyRegions;
    };

    struct blit_image_cmd_t {
        uint32_t SrcImageHandle;
        uint32_t SrcImageLayout;
        uint32_t DstImageHandle;
        uint32_t DstImageLayout;
        uint32_t BlitRegionCount;
        const void* BlitRegions;
    };

    struct resolve_image_cmd_t {
        uint32_t SrcImageHandle;
        uint32_t SrcImageLayout;
        uint32_t DstImageHandle;
        uint32_t DstImageLayout;
        uint32_t ResolveRegionCount;
        const void* ResolveRegions;
    };

    struct dynamic_set_scissor_cmd_t { 
        uint32_t FirstScissor;
        uint32_t ScissorCount;
        const void* Scissors;
    };

    struct dynamic_set_viewport_cmd_t { 
        uint32_t FirstViewport;
        uint32_t ViewportCount;
        const void* Viewports;
    };
    
    struct dynamic_set_depth_bounds_cmd_t { 
        float MinDepth;
        float MaxDepth;
    };

    struct dynamic_set_stencil_compare_mask_cmd_t { 
        uint32_t FaceMask;
        uint32_t CompareMask;
    };

    struct dynamic_set_stencil_write_mask_cmd_t { 
        uint32_t FaceMask;
        uint32_t WriteMask;
    };

    struct dynamic_set_stencil_reference_cmd_t { 
        uint32_t FaceMask;
        uint32_t Reference;
    };

    struct dispatch_compute_cmd_t { 
        uint32_t GroupCountX;
        uint32_t GroupCountY;
        uint32_t GroupCountZ;
    };

    struct dispatch_compute_indirect_cmd_t{ 
        uint32_t BufferHandle;
        uint32_t Offset;
    };

    struct dispatch_compute_base_cmd_t { 
        uint32_t BaseGroupX;
        uint32_t BaseGroupY;
        uint32_t BaseGroupZ;
        uint32_t GroupCountX;
        uint32_t GroupCountY;
        uint32_t GroupCountZ;
    };

    struct dynamic_set_line_width_cmd_t { 
        float LineWidth;
    };

    struct dynamic_set_depth_bias_cmd_t {
        float DepthBiasContantFactor;
        float DepthBiasClamp;
        float DepthBiasSlopeFactor;
    };

    struct set_blend_constants_cmd_t {
        float BlendConstants[4];
    };

    struct execute_secondary_cmds_cmd_t {
        uint32_t NumCommands;
        const uint32_t* CommandBuffers;
    };

}

#endif //!VPSK_COMMAND_TYPES_HPP