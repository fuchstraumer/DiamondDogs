implementing VolumeTiledForwardShading;

// Cook-time configuration, in one place. `extern static const` is a link-time constant the cooker
// drives and reflects; `internal static const` is a plain module global. An axis attribute turns a
// constant into a permutation axis: values/boolean give the range, kind classifies it, active_when
// gates it on an earlier axis. Size expressions in VtfBindings name these by name.

// -- capability -------------------------------------------------------------------------------
[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool VTF_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("capability")][ls_axis_active_when("VTF_USE_WAVE_OPS == 1")]
extern static const uint VTF_WAVE_SIZE = 32u; // widest wave the variant is cooked for; runtime reads WaveGetLaneCount()

internal static const uint VTF_MIN_WAVE_SIZE = 16u; // narrowest width a device may report

// -- technique selectors (were [SpecializationConstant] switches; now chosen at link time) -----
[ls_axis_values("0, 1")] [ls_axis_kind("technique")]
extern static const uint VTF_REDUCTION_TYPE = 0u;   // 0: reduce the light list  1: reduce pass-0 AABBs
[ls_axis_values("0, 1")] [ls_axis_kind("technique")]
extern static const uint VTF_BUILD_STAGE = 0u;      // 0: leaf level  1: interior level
[ls_axis_values("0, 1")] [ls_axis_kind("technique")]
extern static const uint VTF_SORT_PASS = 0u;        // 0: merge-path partition  1: merge

// -- thread counts (each sizes a groupshared array, so each binds at cook time) ----------------
[ls_axis_values("128, 256, 512")] [ls_axis_kind("tuning")]
extern static const uint VTF_REDUCE_LIGHTS_NUM_THREADS = 256u; // 512 -> 16 KiB, the whole WebGPU workgroup budget
[ls_axis_values("128, 256")] [ls_axis_kind("tuning")]
extern static const uint VTF_BUILD_BVH_NUM_THREADS = 256u;     // must be a multiple of VTF_BVH_BRANCHING
[ls_axis_values("32, 64, 128")] [ls_axis_kind("tuning")]
extern static const uint VTF_ASSIGN_LIGHTS_NUM_THREADS = 32u;  // must be a multiple of VTF_BVH_BRANCHING
[ls_axis_values("128, 256")] [ls_axis_kind("tuning")]
extern static const uint VTF_MERGE_SORT_NUM_THREADS = 256u;
[ls_axis_values("2, 4, 8")] [ls_axis_kind("tuning")]
extern static const uint VTF_MERGE_VALUES_PER_THREAD = 4u;     // * NUM_THREADS must stay <= 2048, or LDS overflows
[ls_axis_values("128, 256, 512")] [ls_axis_kind("tuning")]
extern static const uint VTF_RADIX_SORT_NUM_THREADS = 256u;    // also the chunk size one radix pass covers
[ls_axis_values("256, 512, 1024")] [ls_axis_kind("tuning")]
extern static const uint VTF_MORTON_NUM_THREADS = 1024u;
[ls_axis_values("256, 512, 1024")] [ls_axis_kind("tuning")]
extern static const uint VTF_SMALL_ENTRYPOINT_NUM_THREADS = 1024u;

// -- data-structure shape (not axes) ----------------------------------------------------------
internal static const uint VTF_BVH_BRANCHING = 32u;      // 32-ary tree; wave width can't exceed it without a reshape
internal static const uint VTF_BVH_MAX_LEVELS = 7u;

// -- capacities (drive resource sizes in VtfBindings.slang) ----------
[ls_axis_values("1024, 2048, 4096, 8192")] [ls_axis_kind("tuning")]
extern static const uint VTF_MAX_POINT_LIGHTS = 8192u;
[ls_axis_values("256, 512, 1024, 2048")] [ls_axis_kind("tuning")]
extern static const uint VTF_MAX_SPOT_LIGHTS = 1024u;
[ls_axis_values("1, 2, 4")] [ls_axis_kind("tuning")]
extern static const uint VTF_MAX_DIRECTIONAL_LIGHTS = 4u;

// -- cluster grid -----------------------------------------------------------------------------
[ls_axis_values("16, 24, 32")] [ls_axis_kind("tuning")]
extern static const uint VTF_CLUSTER_GRID_X = 24u;
[ls_axis_values("8, 16, 24")] [ls_axis_kind("tuning")]
extern static const uint VTF_CLUSTER_GRID_Y = 16u;
[ls_axis_values("16, 24, 32")] [ls_axis_kind("tuning")]
extern static const uint VTF_CLUSTER_GRID_Z = 24u;

// -- tuning -----------------------------------------------------------------------------------
[ls_axis_values("256, 512, 1024")] [ls_axis_kind("tuning")]
extern static const uint VTF_MAX_LIGHTS_PER_CLUSTER = 1024u;     // per-cluster list, once for point and once for spot; 1024 -> 8 KiB
[ls_axis_values("256, 512, 1024")] [ls_axis_kind("tuning")]
extern static const uint VTF_NODE_STACK_LIMIT = 1024u;
[ls_axis_values("10, 16, 21")] [ls_axis_kind("tuning")]
extern static const uint VTF_MORTON_BITS = 10u;                 // bits per Morton axis; 10 -> a 30-bit code
[ls_axis_values("30, 32")] [ls_axis_kind("tuning")]
extern static const uint VTF_RADIX_BITS = 30u;                  // bits the radix sort tests; 30 agrees with a 10-bit Morton code
[ls_axis_boolean] [ls_axis_kind("technique")]
extern static const bool VTF_UPDATE_UNIQUE_CLUSTERS = true;     // true also writes the cluster-debug indirect args
[ls_axis_boolean] [ls_axis_kind("technique")]
extern static const bool VTF_DEBUG_POINT_LIGHTS = true;         // true: point-light proxies  false: spot-light proxies

internal const static uint VTF_DEBUG_CLUSTER_VERTS = 24u;       // one box: 12 edges * 2 verts

// -- derived (follow from the above; none is an axis) -----------------------------------------
internal const static uint VTF_CLUSTER_COUNT = VTF_CLUSTER_GRID_X * VTF_CLUSTER_GRID_Y * VTF_CLUSTER_GRID_Z;
internal const static uint VTF_RADIX_SORT_MAX_WAVES = (VTF_RADIX_SORT_NUM_THREADS + VTF_MIN_WAVE_SIZE - 1u) / VTF_MIN_WAVE_SIZE;
internal const static uint VTF_BALLOT_WORD_COUNT = (VTF_WAVE_SIZE + 31u) / 32u;
internal const static uint VTF_MERGE_VALUES_PER_GROUP = VTF_MERGE_SORT_NUM_THREADS * VTF_MERGE_VALUES_PER_THREAD;
internal const static uint VTF_MORTON_COORD_SCALE = (1u << VTF_MORTON_BITS) - 1u;
internal const static float VTF_FLT_MAX = 3.402823466e+38f;
