implementing OceanFft;

// each IFFT kernel runs one row per workgroup of IFFT_SIZE threads, so 1024 (the Vulkan and WebGPU limit) is the top
[ls_axis_values("128, 256, 512, 1024")][ls_axis_kind("tuning")]
extern static const uint IFFT_SIZE = 512u;

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool IFFT_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("tuning")][ls_axis_active_when("IFFT_USE_WAVE_OPS==1")]
extern static const uint IFFT_WAVE_SIZE = 32u;

// unlikely we define this one differently
extern static const uint IFFT_NUM_WAVE_CASCADES = 4u;
// IfftInput carries two slices per cascade (see OceanTick.slang)
internal const static uint IFFT_NUM_SLICES = 2u;

// these are no all downstream of the above
internal const static uint IFFT_LOG2_SIZE = firstbithigh(IFFT_SIZE);
internal const static uint IFFT_RADIX4_THREADS = IFFT_SIZE / 4u;
internal const static uint IFFT_LOG4_SIZE = IFFT_LOG2_SIZE / 2u;
// get remainder so we know how many radix-2 stages we need after radix-4 stages
internal const static uint IFFT_LOG4_REMAINDER = IFFT_LOG2_SIZE % 2u;
// IFFT_LOG4_WAVE_SIZE is the number of radix-4 stages that DoIfftRadix4 runs inside one wave: it stops while
// bitOffset + 2 < IFFT_LOG2_WAVE_SIZE, so wave 16: 1, wave 32: 2, wave 64: 2, wave 128: 3
internal const static uint IFFT_LOG2_WAVE_SIZE = IFFT_USE_WAVE_OPS ? firstbithigh(IFFT_WAVE_SIZE) : 0u;
internal const static uint IFFT_LOG4_WAVE_SIZE = IFFT_USE_WAVE_OPS ? (IFFT_LOG2_WAVE_SIZE - 1u) / 2u : 0u;
