implementing OceanFft;

[ls_axis_values("128, 256, 512, 1024, 2048, 4096, 8192")][ls_axis_kind("tuning")]
extern static const uint IFFT_SIZE = 512u;

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool IFFT_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("tuning")][ls_axis_active_when("IFFT_USE_WAVE_OPS==1")]
extern static const uint IFFT_WAVE_SIZE = 32u;

// unlikely we define this one differently
extern static const uint IFFT_NUM_WAVE_CASCADES = 4u;

// these are no all downstream of the above
internal const static uint IFFT_LOG2_SIZE = firstbithigh(IFFT_SIZE);
internal const static uint IFFT_LOG4_SIZE = IFFT_LOG2_SIZE / 2u;
// get remainder so we know how many radix-2 stages we need after radix-4 stages
internal const static uint IFFT_LOG4_REMAINDER = IFFT_LOG2_SIZE % 2u;
// IFFT_LOG2_WAVE_SIZE: 5, IFFT_LOG4_WAVE_SIZE: 2
internal const static uint IFFT_LOG2_WAVE_SIZE = IFFT_USE_WAVE_OPS ? firstbithigh(IFFT_WAVE_SIZE) : 0u;
internal const static uint IFFT_LOG4_WAVE_SIZE = IFFT_USE_WAVE_OPS ? 2u : 0u;
