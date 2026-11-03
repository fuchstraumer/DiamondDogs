#include <gtest/gtest.h>
#include "Device.hpp"
#include "ExecutableStatistics.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderTypes.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>

// glslangValidator -V --target-env vulkan1.3 --vn k_ProbeComputeSpv, from:
//
// #version 450
// layout(local_size_x = 64) in;
// layout(set = 0, binding = 0) buffer Values { uint values[]; };
// void main()
// {
//     uint hash = gl_GlobalInvocationID.x;
//     for (int round = 0; round < 8; ++round)
//     {
//         hash ^= hash >> 16;
//         hash *= 0x7feb352du;
//         hash ^= hash >> 15;
//         hash *= 0x846ca68bu;
//     }
//     values[gl_GlobalInvocationID.x] = hash;
// }

constexpr static const uint32_t k_ProbeComputeSpv[] =
{
    0x07230203,0x00010600,0x0008000b,0x0000003c,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000005,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x00000033,0x00060010,
    0x00000004,0x00000011,0x00000040,0x00000001,0x00000001,0x00030003,0x00000002,0x000001c2,
    0x00040005,0x00000004,0x6e69616d,0x00000000,0x00040005,0x00000008,0x68736168,0x00000000,
    0x00080005,0x0000000b,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,0x496e6f69,0x00000044,
    0x00040005,0x00000012,0x6e756f72,0x00000064,0x00040005,0x00000031,0x756c6156,0x00007365,
    0x00050006,0x00000031,0x00000000,0x756c6176,0x00007365,0x00030005,0x00000033,0x00000000,
    0x00040047,0x0000000b,0x0000000b,0x0000001c,0x00040047,0x00000030,0x00000006,0x00000004,
    0x00030047,0x00000031,0x00000002,0x00050048,0x00000031,0x00000000,0x00000023,0x00000000,
    0x00040047,0x00000033,0x00000021,0x00000000,0x00040047,0x00000033,0x00000022,0x00000000,
    0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00040015,0x00000006,0x00000020,
    0x00000000,0x00040020,0x00000007,0x00000007,0x00000006,0x00040017,0x00000009,0x00000006,
    0x00000003,0x00040020,0x0000000a,0x00000001,0x00000009,0x0004003b,0x0000000a,0x0000000b,
    0x00000001,0x0004002b,0x00000006,0x0000000c,0x00000000,0x00040020,0x0000000d,0x00000001,
    0x00000006,0x00040015,0x00000010,0x00000020,0x00000001,0x00040020,0x00000011,0x00000007,
    0x00000010,0x0004002b,0x00000010,0x00000013,0x00000000,0x0004002b,0x00000010,0x0000001a,
    0x00000008,0x00020014,0x0000001b,0x0004002b,0x00000010,0x0000001e,0x00000010,0x0004002b,
    0x00000006,0x00000022,0x7feb352d,0x0004002b,0x00000010,0x00000026,0x0000000f,0x0004002b,
    0x00000006,0x0000002a,0x846ca68b,0x0004002b,0x00000010,0x0000002e,0x00000001,0x0003001d,
    0x00000030,0x00000006,0x0003001e,0x00000031,0x00000030,0x00040020,0x00000032,0x0000000c,
    0x00000031,0x0004003b,0x00000032,0x00000033,0x0000000c,0x00040020,0x00000037,0x0000000c,
    0x00000006,0x0004002b,0x00000006,0x00000039,0x00000040,0x0004002b,0x00000006,0x0000003a,
    0x00000001,0x0006002c,0x00000009,0x0000003b,0x00000039,0x0000003a,0x0000003a,0x00050036,
    0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003b,0x00000007,
    0x00000008,0x00000007,0x0004003b,0x00000011,0x00000012,0x00000007,0x00050041,0x0000000d,
    0x0000000e,0x0000000b,0x0000000c,0x0004003d,0x00000006,0x0000000f,0x0000000e,0x0003003e,
    0x00000008,0x0000000f,0x0003003e,0x00000012,0x00000013,0x000200f9,0x00000014,0x000200f8,
    0x00000014,0x000400f6,0x00000016,0x00000017,0x00000000,0x000200f9,0x00000018,0x000200f8,
    0x00000018,0x0004003d,0x00000010,0x00000019,0x00000012,0x000500b1,0x0000001b,0x0000001c,
    0x00000019,0x0000001a,0x000400fa,0x0000001c,0x00000015,0x00000016,0x000200f8,0x00000015,
    0x0004003d,0x00000006,0x0000001d,0x00000008,0x000500c2,0x00000006,0x0000001f,0x0000001d,
    0x0000001e,0x0004003d,0x00000006,0x00000020,0x00000008,0x000500c6,0x00000006,0x00000021,
    0x00000020,0x0000001f,0x0003003e,0x00000008,0x00000021,0x0004003d,0x00000006,0x00000023,
    0x00000008,0x00050084,0x00000006,0x00000024,0x00000023,0x00000022,0x0003003e,0x00000008,
    0x00000024,0x0004003d,0x00000006,0x00000025,0x00000008,0x000500c2,0x00000006,0x00000027,
    0x00000025,0x00000026,0x0004003d,0x00000006,0x00000028,0x00000008,0x000500c6,0x00000006,
    0x00000029,0x00000028,0x00000027,0x0003003e,0x00000008,0x00000029,0x0004003d,0x00000006,
    0x0000002b,0x00000008,0x00050084,0x00000006,0x0000002c,0x0000002b,0x0000002a,0x0003003e,
    0x00000008,0x0000002c,0x000200f9,0x00000017,0x000200f8,0x00000017,0x0004003d,0x00000010,
    0x0000002d,0x00000012,0x00050080,0x00000010,0x0000002f,0x0000002d,0x0000002e,0x0003003e,
    0x00000012,0x0000002f,0x000200f9,0x00000014,0x000200f8,0x00000016,0x00050041,0x0000000d,
    0x00000034,0x0000000b,0x0000000c,0x0004003d,0x00000006,0x00000035,0x00000034,0x0004003d,
    0x00000006,0x00000036,0x00000008,0x00060041,0x00000037,0x00000038,0x00000033,0x00000013,
    0x00000035,0x0003003e,0x00000038,0x00000036,0x000100fd,0x00010038
};

// glslangValidator -V --target-env vulkan1.3 --vn k_LocalMemorySpv, from:
//
// #version 450
// layout(local_size_x = 64) in;
// layout(set = 0, binding = 0) buffer Values { uint values[]; };
// void main()
// {
//     uint scratch[256];
//     for (int i = 0; i < 256; ++i)
//     {
//         scratch[i] = values[(gl_GlobalInvocationID.x + i) & 1023u];
//     }
//     uint index = values[gl_GlobalInvocationID.x] & 255u;
//     values[gl_GlobalInvocationID.x] = scratch[index] + scratch[255u - index];
// }

constexpr static const uint32_t k_LocalMemorySpv[] =
{
    0x07230203,0x00010600,0x0008000b,0x00000046,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000005,0x00000004,0x6e69616d,0x00000000,0x0000001c,0x0000001f,0x00060010,
    0x00000004,0x00000011,0x00000040,0x00000001,0x00000001,0x00030003,0x00000002,0x000001c2,
    0x00040005,0x00000004,0x6e69616d,0x00000000,0x00030005,0x00000008,0x00000069,0x00040005,
    0x00000017,0x61726373,0x00686374,0x00040005,0x0000001a,0x756c6156,0x00007365,0x00050006,
    0x0000001a,0x00000000,0x756c6176,0x00007365,0x00030005,0x0000001c,0x00000000,0x00080005,
    0x0000001f,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,0x496e6f69,0x00000044,0x00040005,
    0x00000031,0x65646e69,0x00000078,0x00040047,0x00000019,0x00000006,0x00000004,0x00030047,
    0x0000001a,0x00000002,0x00050048,0x0000001a,0x00000000,0x00000023,0x00000000,0x00040047,
    0x0000001c,0x00000021,0x00000000,0x00040047,0x0000001c,0x00000022,0x00000000,0x00040047,
    0x0000001f,0x0000000b,0x0000001c,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
    0x00040015,0x00000006,0x00000020,0x00000001,0x00040020,0x00000007,0x00000007,0x00000006,
    0x0004002b,0x00000006,0x00000009,0x00000000,0x0004002b,0x00000006,0x00000010,0x00000100,
    0x00020014,0x00000011,0x00040015,0x00000013,0x00000020,0x00000000,0x0004002b,0x00000013,
    0x00000014,0x00000100,0x0004001c,0x00000015,0x00000013,0x00000014,0x00040020,0x00000016,
    0x00000007,0x00000015,0x0003001d,0x00000019,0x00000013,0x0003001e,0x0000001a,0x00000019,
    0x00040020,0x0000001b,0x0000000c,0x0000001a,0x0004003b,0x0000001b,0x0000001c,0x0000000c,
    0x00040017,0x0000001d,0x00000013,0x00000003,0x00040020,0x0000001e,0x00000001,0x0000001d,
    0x0004003b,0x0000001e,0x0000001f,0x00000001,0x0004002b,0x00000013,0x00000020,0x00000000,
    0x00040020,0x00000021,0x00000001,0x00000013,0x0004002b,0x00000013,0x00000027,0x000003ff,
    0x00040020,0x00000029,0x0000000c,0x00000013,0x00040020,0x0000002c,0x00000007,0x00000013,
    0x0004002b,0x00000006,0x0000002f,0x00000001,0x0004002b,0x00000013,0x00000036,0x000000ff,
    0x0004002b,0x00000013,0x00000043,0x00000040,0x0004002b,0x00000013,0x00000044,0x00000001,
    0x0006002c,0x0000001d,0x00000045,0x00000043,0x00000044,0x00000044,0x00050036,0x00000002,
    0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003b,0x00000007,0x00000008,
    0x00000007,0x0004003b,0x00000016,0x00000017,0x00000007,0x0004003b,0x0000002c,0x00000031,
    0x00000007,0x0003003e,0x00000008,0x00000009,0x000200f9,0x0000000a,0x000200f8,0x0000000a,
    0x000400f6,0x0000000c,0x0000000d,0x00000000,0x000200f9,0x0000000e,0x000200f8,0x0000000e,
    0x0004003d,0x00000006,0x0000000f,0x00000008,0x000500b1,0x00000011,0x00000012,0x0000000f,
    0x00000010,0x000400fa,0x00000012,0x0000000b,0x0000000c,0x000200f8,0x0000000b,0x0004003d,
    0x00000006,0x00000018,0x00000008,0x00050041,0x00000021,0x00000022,0x0000001f,0x00000020,
    0x0004003d,0x00000013,0x00000023,0x00000022,0x0004003d,0x00000006,0x00000024,0x00000008,
    0x0004007c,0x00000013,0x00000025,0x00000024,0x00050080,0x00000013,0x00000026,0x00000023,
    0x00000025,0x000500c7,0x00000013,0x00000028,0x00000026,0x00000027,0x00060041,0x00000029,
    0x0000002a,0x0000001c,0x00000009,0x00000028,0x0004003d,0x00000013,0x0000002b,0x0000002a,
    0x00050041,0x0000002c,0x0000002d,0x00000017,0x00000018,0x0003003e,0x0000002d,0x0000002b,
    0x000200f9,0x0000000d,0x000200f8,0x0000000d,0x0004003d,0x00000006,0x0000002e,0x00000008,
    0x00050080,0x00000006,0x00000030,0x0000002e,0x0000002f,0x0003003e,0x00000008,0x00000030,
    0x000200f9,0x0000000a,0x000200f8,0x0000000c,0x00050041,0x00000021,0x00000032,0x0000001f,
    0x00000020,0x0004003d,0x00000013,0x00000033,0x00000032,0x00060041,0x00000029,0x00000034,
    0x0000001c,0x00000009,0x00000033,0x0004003d,0x00000013,0x00000035,0x00000034,0x000500c7,
    0x00000013,0x00000037,0x00000035,0x00000036,0x0003003e,0x00000031,0x00000037,0x00050041,
    0x00000021,0x00000038,0x0000001f,0x00000020,0x0004003d,0x00000013,0x00000039,0x00000038,
    0x0004003d,0x00000013,0x0000003a,0x00000031,0x00050041,0x0000002c,0x0000003b,0x00000017,
    0x0000003a,0x0004003d,0x00000013,0x0000003c,0x0000003b,0x0004003d,0x00000013,0x0000003d,
    0x00000031,0x00050082,0x00000013,0x0000003e,0x00000036,0x0000003d,0x00050041,0x0000002c,
    0x0000003f,0x00000017,0x0000003e,0x0004003d,0x00000013,0x00000040,0x0000003f,0x00050080,
    0x00000013,0x00000041,0x0000003c,0x00000040,0x00060041,0x00000029,0x00000042,0x0000001c,
    0x00000009,0x00000039,0x0003003e,0x00000042,0x00000041,0x000100fd,0x00010038
};

// glslangValidator -V --target-env vulkan1.3 --vn k_SharedMemorySpv, from:
//
// #version 450
// layout(local_size_x = 64) in;
// layout(set = 0, binding = 0) buffer Values { uint values[]; };
// shared uint tile[1024];
// void main()
// {
//     for (uint i = gl_LocalInvocationIndex; i < 1024u; i += 64u)
//     {
//         tile[i] = values[i];
//     }
//     barrier();
//     values[gl_GlobalInvocationID.x] = tile[(gl_LocalInvocationIndex * 17u) & 1023u];
// }

constexpr static const uint32_t k_SharedMemorySpv[] =
{
    0x07230203,0x00010600,0x0008000b,0x0000003a,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0009000f,0x00000005,0x00000004,0x6e69616d,0x00000000,0x0000000a,0x00000017,0x0000001c,
    0x0000002c,0x00060010,0x00000004,0x00000011,0x00000040,0x00000001,0x00000001,0x00030003,
    0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00030005,0x00000008,
    0x00000069,0x00080005,0x0000000a,0x4c5f6c67,0x6c61636f,0x6f766e49,0x69746163,0x6e496e6f,
    0x00786564,0x00040005,0x00000017,0x656c6974,0x00000000,0x00040005,0x0000001a,0x756c6156,
    0x00007365,0x00050006,0x0000001a,0x00000000,0x756c6176,0x00007365,0x00030005,0x0000001c,
    0x00000000,0x00080005,0x0000002c,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,0x496e6f69,
    0x00000044,0x00040047,0x0000000a,0x0000000b,0x0000001d,0x00040047,0x00000019,0x00000006,
    0x00000004,0x00030047,0x0000001a,0x00000002,0x00050048,0x0000001a,0x00000000,0x00000023,
    0x00000000,0x00040047,0x0000001c,0x00000021,0x00000000,0x00040047,0x0000001c,0x00000022,
    0x00000000,0x00040047,0x0000002c,0x0000000b,0x0000001c,0x00020013,0x00000002,0x00030021,
    0x00000003,0x00000002,0x00040015,0x00000006,0x00000020,0x00000000,0x00040020,0x00000007,
    0x00000007,0x00000006,0x00040020,0x00000009,0x00000001,0x00000006,0x0004003b,0x00000009,
    0x0000000a,0x00000001,0x0004002b,0x00000006,0x00000012,0x00000400,0x00020014,0x00000013,
    0x0004001c,0x00000015,0x00000006,0x00000012,0x00040020,0x00000016,0x00000004,0x00000015,
    0x0004003b,0x00000016,0x00000017,0x00000004,0x0003001d,0x00000019,0x00000006,0x0003001e,
    0x0000001a,0x00000019,0x00040020,0x0000001b,0x0000000c,0x0000001a,0x0004003b,0x0000001b,
    0x0000001c,0x0000000c,0x00040015,0x0000001d,0x00000020,0x00000001,0x0004002b,0x0000001d,
    0x0000001e,0x00000000,0x00040020,0x00000020,0x0000000c,0x00000006,0x00040020,0x00000023,
    0x00000004,0x00000006,0x0004002b,0x00000006,0x00000025,0x00000040,0x0004002b,0x00000006,
    0x00000028,0x00000002,0x0004002b,0x00000006,0x00000029,0x00000108,0x00040017,0x0000002a,
    0x00000006,0x00000003,0x00040020,0x0000002b,0x00000001,0x0000002a,0x0004003b,0x0000002b,
    0x0000002c,0x00000001,0x0004002b,0x00000006,0x0000002d,0x00000000,0x0004002b,0x00000006,
    0x00000031,0x00000011,0x0004002b,0x00000006,0x00000033,0x000003ff,0x0004002b,0x00000006,
    0x00000038,0x00000001,0x0006002c,0x0000002a,0x00000039,0x00000025,0x00000038,0x00000038,
    0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003b,
    0x00000007,0x00000008,0x00000007,0x0004003d,0x00000006,0x0000000b,0x0000000a,0x0003003e,
    0x00000008,0x0000000b,0x000200f9,0x0000000c,0x000200f8,0x0000000c,0x000400f6,0x0000000e,
    0x0000000f,0x00000000,0x000200f9,0x00000010,0x000200f8,0x00000010,0x0004003d,0x00000006,
    0x00000011,0x00000008,0x000500b0,0x00000013,0x00000014,0x00000011,0x00000012,0x000400fa,
    0x00000014,0x0000000d,0x0000000e,0x000200f8,0x0000000d,0x0004003d,0x00000006,0x00000018,
    0x00000008,0x0004003d,0x00000006,0x0000001f,0x00000008,0x00060041,0x00000020,0x00000021,
    0x0000001c,0x0000001e,0x0000001f,0x0004003d,0x00000006,0x00000022,0x00000021,0x00050041,
    0x00000023,0x00000024,0x00000017,0x00000018,0x0003003e,0x00000024,0x00000022,0x000200f9,
    0x0000000f,0x000200f8,0x0000000f,0x0004003d,0x00000006,0x00000026,0x00000008,0x00050080,
    0x00000006,0x00000027,0x00000026,0x00000025,0x0003003e,0x00000008,0x00000027,0x000200f9,
    0x0000000c,0x000200f8,0x0000000e,0x000400e0,0x00000028,0x00000028,0x00000029,0x00050041,
    0x00000009,0x0000002e,0x0000002c,0x0000002d,0x0004003d,0x00000006,0x0000002f,0x0000002e,
    0x0004003d,0x00000006,0x00000030,0x0000000a,0x00050084,0x00000006,0x00000032,0x00000030,
    0x00000031,0x000500c7,0x00000006,0x00000034,0x00000032,0x00000033,0x00050041,0x00000023,
    0x00000035,0x00000017,0x00000034,0x0004003d,0x00000006,0x00000036,0x00000035,0x00060041,
    0x00000020,0x00000037,0x0000001c,0x0000001e,0x0000002f,0x0003003e,0x00000037,0x00000036,
    0x000100fd,0x00010038
};

constexpr static VkDescriptorSetLayoutBinding k_ProbeSetLayoutBinding
{
    0,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    1,
    VK_SHADER_STAGE_COMPUTE_BIT,
    nullptr
};

constexpr static VkDescriptorSetLayoutCreateInfo k_ProbeSetLayoutCreateInfo
{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr,
    0,
    1,
    &k_ProbeSetLayoutBinding
};

namespace
{
    std::optional<uint64_t> FindUnsignedStatistic(const rhi::ShaderExecutable& executable, std::string_view name);
}

class ExecutableStatisticsTest : public ::testing::Test
{
public:
    std::unique_ptr<rhi::RhiSystem> RhiSystem;
    VkDescriptorSetLayout ProbeSetLayout{ VK_NULL_HANDLE };

    static rhi::RhiSystemCreateInfo GetCreateInfo()
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "ExecutableStatisticsTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::Full;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils", "VK_KHR_surface" };
        createInfo.RequestedDeviceExtensions = { "VK_EXT_shader_object", "VK_KHR_pipeline_executable_properties" };
        const auto shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsUnitTesting" / "ShaderCache";
        createInfo.ShaderCacheDir = shaderCacheDir.string();
        return createInfo;
    }

    void SetUp() override
    {
        RhiSystem = std::make_unique<rhi::RhiSystem>(GetCreateInfo());
        ASSERT_NE(RhiSystem->GetDevice(), nullptr);

        if (!RhiSystem->GetDevice()->HasExtension(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME))
        {
            GTEST_SKIP() << "the device does not have VK_KHR_pipeline_executable_properties";
        }

        const VkDevice device = RhiSystem->GetDevice()->Handle().As<VkDevice>();
        ASSERT_EQ(vkCreateDescriptorSetLayout(device, &k_ProbeSetLayoutCreateInfo, nullptr, &ProbeSetLayout), VK_SUCCESS);
    }

    void TearDown() override
    {
        if (ProbeSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(RhiSystem->GetDevice()->Handle().As<VkDevice>(), ProbeSetLayout, nullptr);
        }

        RhiSystem.reset();
    }

    rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> CaptureFromSpirv(std::span<const uint32_t> spirv)
    {
        rhi::DescriptorSetLayoutHandle setLayoutHandle(reinterpret_cast<uint64_t>(ProbeSetLayout));
        const rhi::ShaderBinaryOptions computeOptions
        {
            spirv,
            rhi::ShaderStageFlags::Compute,
            "main",
            std::span<rhi::PushConstantRange>{},
            std::span<rhi::DescriptorSetLayoutHandle>{ &setLayoutHandle, 1 }
        };

        return rhi::CaptureComputeStatistics(*RhiSystem->GetDevice(), computeOptions);
    }
};

TEST_F(ExecutableStatisticsTest, CapturesComputeStatistics)
{
    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> executables =
        CaptureFromSpirv(k_ProbeComputeSpv);
    ASSERT_TRUE(executables.has_value()) << rhi::ToString(executables.error());
    ASSERT_FALSE(executables->empty());

    for (const rhi::ShaderExecutable& executable : *executables)
    {
        EXPECT_EQ(executable.Stages, rhi::ShaderStageFlags::Compute);
        EXPECT_GT(executable.SubgroupSize, 0);
        ASSERT_FALSE(executable.Statistics.empty());
        EXPECT_FALSE(executable.Statistics.front().Name.empty());
    }
}

TEST_F(ExecutableStatisticsTest, HonorsRequiredSubgroupSize)
{
    constexpr uint32_t requiredSize = 32u;

    VkPhysicalDeviceVulkan13Properties properties13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
    VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties13 };
    vkGetPhysicalDeviceProperties2(RhiSystem->GetDevice()->GetPhysicalDevice().As<VkPhysicalDevice>(), &properties2);

    if (requiredSize < properties13.minSubgroupSize || requiredSize > properties13.maxSubgroupSize)
    {
        GTEST_SKIP() << "subgroup size 32 is outside the device range";
    }

    if ((properties13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
    {
        GTEST_SKIP() << "the device cannot require a subgroup size for compute";
    }

    rhi::DescriptorSetLayoutHandle setLayoutHandle(reinterpret_cast<uint64_t>(ProbeSetLayout));
    rhi::ShaderBinaryOptions computeOptions
    {
        std::span<const uint32_t>(k_ProbeComputeSpv),
        rhi::ShaderStageFlags::Compute,
        "main",
        std::span<rhi::PushConstantRange>{},
        std::span<rhi::DescriptorSetLayoutHandle>{ &setLayoutHandle, 1 }
    };
    computeOptions.RequiredSubgroupSize = requiredSize;
    computeOptions.RequireFullSubgroups = true;

    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> executables =
        rhi::CaptureComputeStatistics(*RhiSystem->GetDevice(), computeOptions);
    ASSERT_TRUE(executables.has_value()) << rhi::ToString(executables.error());
    ASSERT_FALSE(executables->empty());

    for (const rhi::ShaderExecutable& executable : *executables)
    {
        EXPECT_EQ(executable.SubgroupSize, 32);
    }
}

TEST_F(ExecutableStatisticsTest, NvidiaStatisticsFollowTheShader)
{
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(RhiSystem->GetDevice()->GetPhysicalDevice().As<VkPhysicalDevice>(), &deviceProperties);

    if (deviceProperties.vendorID != 0x10DEu)
    {
        GTEST_SKIP() << "the statistic names are specific to NVIDIA";
    }

    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> probe = CaptureFromSpirv(k_ProbeComputeSpv);
    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> local = CaptureFromSpirv(k_LocalMemorySpv);
    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> shared = CaptureFromSpirv(k_SharedMemorySpv);
    ASSERT_TRUE(probe.has_value()) << rhi::ToString(probe.error());
    ASSERT_TRUE(local.has_value()) << rhi::ToString(local.error());
    ASSERT_TRUE(shared.has_value()) << rhi::ToString(shared.error());
    ASSERT_FALSE(probe->empty());
    ASSERT_FALSE(local->empty());
    ASSERT_FALSE(shared->empty());

    const std::optional<uint64_t> probeShared = FindUnsignedStatistic(probe->front(), "Shared Memory Size");
    const std::optional<uint64_t> sharedShared = FindUnsignedStatistic(shared->front(), "Shared Memory Size");
    ASSERT_TRUE(probeShared.has_value());
    ASSERT_TRUE(sharedShared.has_value());
    EXPECT_EQ(*sharedShared, 4096u);
    EXPECT_EQ(*probeShared, 0u);

    const std::optional<uint64_t> probeRegisters = FindUnsignedStatistic(probe->front(), "Register Count");
    const std::optional<uint64_t> localRegisters = FindUnsignedStatistic(local->front(), "Register Count");
    ASSERT_TRUE(probeRegisters.has_value());
    ASSERT_TRUE(localRegisters.has_value());
    EXPECT_GT(*localRegisters, *probeRegisters);

    // the driver sets bit 36 of this value, so only the low 32 bits are the byte count
    const std::optional<uint64_t> probeLocal = FindUnsignedStatistic(probe->front(), "Local Memory Size");
    const std::optional<uint64_t> localLocal = FindUnsignedStatistic(local->front(), "Local Memory Size");
    ASSERT_TRUE(probeLocal.has_value());
    ASSERT_TRUE(localLocal.has_value());
    EXPECT_EQ(*localLocal & 0xFFFFFFFFu, 1024u);
    EXPECT_EQ(*probeLocal & 0xFFFFFFFFu, 0u);
}

TEST_F(ExecutableStatisticsTest, RejectsNonComputeStage)
{
    const rhi::ShaderBinaryOptions vertexOptions
    {
        std::span<const uint32_t>(k_ProbeComputeSpv),
        rhi::ShaderStageFlags::Vertex,
        "main"
    };

    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> executables =
        rhi::CaptureComputeStatistics(*RhiSystem->GetDevice(), vertexOptions);
    ASSERT_FALSE(executables.has_value());
    EXPECT_EQ(executables.error(), rhi::ExecutableStatisticsError::NotComputeStage);
}

TEST_F(ExecutableStatisticsTest, ReportsMissingExtension)
{
    rhi::RhiSystemCreateInfo createInfo = GetCreateInfo();
    createInfo.RequestedDeviceExtensions = { "VK_EXT_shader_object" };
    rhi::RhiSystem bareSystem(createInfo);
    ASSERT_NE(bareSystem.GetDevice(), nullptr);

    if (bareSystem.GetDevice()->HasExtension(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME))
    {
        GTEST_SKIP() << "the device enables VK_KHR_pipeline_executable_properties without a request";
    }

    const rhi::ShaderBinaryOptions computeOptions
    {
        std::span<const uint32_t>(k_ProbeComputeSpv),
        rhi::ShaderStageFlags::Compute,
        "main"
    };

    const rhi::ExecutableStatisticsResult<std::vector<rhi::ShaderExecutable>> executables =
        rhi::CaptureComputeStatistics(*bareSystem.GetDevice(), computeOptions);
    ASSERT_FALSE(executables.has_value());
    EXPECT_EQ(executables.error(), rhi::ExecutableStatisticsError::ExtensionNotEnabled);
}

namespace
{

    std::optional<uint64_t> FindUnsignedStatistic(const rhi::ShaderExecutable& executable, std::string_view name)
    {
        for (const rhi::ExecutableStatistic& statistic : executable.Statistics)
        {
            if (statistic.Name == name)
            {
                if (const uint64_t* value = std::get_if<uint64_t>(&statistic.Value))
                {
                    return *value;
                }

                return std::nullopt;
            }
        }

        return std::nullopt;
    }

}
