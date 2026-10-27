implementing OceanFft;

// the IFFT kernel runs IFFT_SIZE threads per workgroup (four lines of IFFT_SIZE / 4 threads), and the fused kernels
// run twice that, so 512 is the top value (1024 threads)
[ls_axis_values("128, 256, 512")][ls_axis_kind("tuning")]
extern static const uint IFFT_SIZE = 512u;

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool IFFT_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("tuning")][ls_axis_active_when("IFFT_USE_WAVE_OPS==1")]
extern static const uint IFFT_WAVE_SIZE = 32u;

// unlikely we define this one differently
extern static const uint IFFT_NUM_WAVE_CASCADES = 5u;
// IfftInput carries two slices per cascade (see OceanFftStages.slang)
internal const static uint IFFT_NUM_SLICES = 2u;

// the radix-4 kernels transform 4 lines per workgroup. a slot is one line of one slice: the unfused kernels
// run one slice per group (4 slots), the fused kernels run both slices (8 slots, slot = slice * 4 + line)
internal const static uint IFFT_LINES_PER_GROUP = 4u;
internal const static uint IFFT_UNFUSED_SLOTS = IFFT_LINES_PER_GROUP;
internal const static uint IFFT_FUSED_SLOTS = IFFT_NUM_SLICES * IFFT_LINES_PER_GROUP;
internal const static uint IFFT_FUSED_THREADS = IFFT_NUM_SLICES * IFFT_SIZE;

// these are no all downstream of the above
internal const static uint IFFT_LOG2_SIZE = firstbithigh(IFFT_SIZE);
// threads per line in the radix-4 kernel: one butterfly of 4 elements each
internal const static uint IFFT_RADIX4_THREADS = IFFT_SIZE / 4u;
// 1 for odd log2 sizes, which finish with one radix-2 stage after the radix-4 stages
internal const static uint IFFT_LOG4_REMAINDER = IFFT_LOG2_SIZE % 2u;
