#pragma once
#ifndef DIAMOND_DOGS_LOOM_DECLARATIONS_HPP
#define DIAMOND_DOGS_LOOM_DECLARATIONS_HPP
#include <cstdint>
#include "LoomKeys.hpp"

namespace loom
{

    enum class ResourceLifetime : uint8_t
    {
        Invalid = 0,
        Frame = 1,
        Persistent = 2
    };

    enum class CpuAccess : uint8_t
    {
        Invalid = 0,
        None = 1,
        Upload = 2,
        Readback = 3
    };

    enum class OnNoHistory : uint8_t
    {
        Invalid = 0,
        Skip = 1,
        Clear = 2
    };
    
    // not implemented yet, stub for future expansion of queue affinities
    enum class QueueAffinity : uint8_t
    {
        Invalid = 0,
        Graphics = 1,
    };

    enum class RunPolicy : uint8_t
    {
        Invalid = 0,
        EveryFrame = 1,
        Once = 2,
        WhenInputsChange = 3,
        OnTrigger = 4, // graph.Trigger(key) fires this item
    };

    struct SizeSource
    {
        enum class Kind : uint8_t
        {
            Invalid = 0,
            Derived = 1,
            Bytes = 2
        };
        Kind Kind{ Kind::Invalid };
        uint64_t Size{ 0 };

        static SizeSource Derived() noexcept;
        static SizeSource Bytes(int64_t byte_count) noexcept;

    };

    struct ExtentSource
    {
        enum class Kind : uint8_t
        {
            Invalid = 0,
            Derived = 1,
            Fixed = 2,
            Target = 3
        };
        Kind Kind{ Kind::Invalid };
        int32_t Width{ 0 };
        int32_t Height{ 0 };
        int32_t Depth{ 0 };
        float Scale{ 1.0f };

        static ExtentSource Derived() noexcept;
        static ExtentSource Fixed(int32_t width, int32_t height, int32_t depth) noexcept;
        static ExtentSource OfTarget(FrameTargetKey target, float scale) noexcept;
    };

    struct MipSource
    {
        enum class Kind : uint8_t
        {
            Invalid = 0,
            Loaded = 1,
            Generated = 2,
            None = 3
        };
        // loaded from init data or computed, most likely? what else...
        Kind Kind{ Kind::Invalid };
        int32_t Count{ 1 };
        static MipSource One() noexcept;
        // Requests fully generated mip levels for the resource, down to 1x1
        static MipSource Full() noexcept; 
        // Generated mip levels of given count
        static MipSource Levels(int32_t count) noexcept;
    };

    struct BufferDeclaration
    {
        ResourceKey<Buffer> Key;
        ResourceLifetime Lifetime{ ResourceLifetime::Invalid };
        SizeSource Size;
        CpuAccess Cpu{ CpuAccess::Invalid };
        int32_t HistoryDepth{ 0 };
        OnNoHistory NoHistoryAction{ OnNoHistory::Invalid };
    };

    struct ImageDeclaration
    {
        ResourceKey<Image> Key;
        ResourceLifetime Lifetime{ ResourceLifetime::Invalid };
        // TextureFormat Format{}; // todo: bring this in from req. includes
        ExtentSource Extent;
        MipSource Mips;
        int32_t Layers{ -1 }; // -1 default means not setting this can be checked exactly as an error
        CpuAccess Cpu{ CpuAccess::Invalid };
        int32_t HistoryDepth{ 0 };
        OnNoHistory NoHistoryAction{ OnNoHistory::Invalid };
    };

    struct SamplerDeclaration
    {
        ResourceKey<Sampler> Key;
        // where do we get it's info?
    };

    struct ImageExpectation{};
    struct OutputFallback{};

    struct ImportDeclaration
    {
        ResourceKey<Image> Key;
        ImageExpectation Expectation;
    };

    struct OutputDeclaration
    {
        FrameTargetKey Target;
        OutputFallback OnSkip;
    };
}

#endif // !DIAMOND_DOGS_LOOM_DECLARATIONS_HPP
