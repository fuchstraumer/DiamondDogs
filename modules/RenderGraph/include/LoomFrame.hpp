#pragma once
#ifndef DIAMOND_DOGS_LOOM_FRAME_HPP
#define DIAMOND_DOGS_LOOM_FRAME_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// LoomFrame: the constants Loom gives every shader that declares them (step 7, round 2 Q3). Loom owns one uniform
// buffer per frame slot and writes it each Tick. Bind binds a uniform block named k_LoomFrameBindingName with no Bind
// statement, so a pass can have its own params and still read the time and the target extents.
// LoomFrame.slang (modules/RenderGraph/shaders) is the shader side: keep the two equal.
namespace loom
{

    inline constexpr std::string_view k_LoomFrameBindingName{ "g_LoomFrame" };
    inline constexpr uint32_t k_LoomMaxFrameTargets = 4u;

    // One frame target, in import order. Only 4-byte scalars and 2-vectors at 8-byte offsets, in a 32-byte struct, so
    // std140, std430, scalar and WGSL uniform layouts all agree.
    struct LoomTargetInfo
    {
        std::array<uint32_t, 2> TargetExtent;
        std::array<uint32_t, 2> AllocationExtent;
        std::array<uint32_t, 2> RenderExtent;
        std::array<float, 2> RenderToAllocation;    // RenderExtent / AllocationExtent: the UV scale of a scene image
    };

    struct LoomFrameConstants
    {
        float Time;                 // seconds since Realize: steps of about 2 ms after 4.5 hours
        float DeltaTime;            // Tick's dT
        uint32_t FrameIndex;        // wraps at 2^32
        uint32_t TargetCount;
        std::array<LoomTargetInfo, k_LoomMaxFrameTargets> Targets;
    };

    static_assert(sizeof(LoomTargetInfo) == 32u);
    static_assert(offsetof(LoomTargetInfo, RenderToAllocation) == 24u);
    static_assert(offsetof(LoomFrameConstants, Targets) == 16u);
    static_assert(sizeof(LoomFrameConstants) == 16u + 32u * k_LoomMaxFrameTargets);

}

#endif // !DIAMOND_DOGS_LOOM_FRAME_HPP
