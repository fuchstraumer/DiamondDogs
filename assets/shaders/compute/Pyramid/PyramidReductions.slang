// Copyright 2021 NVIDIA CORPORATION
// SPDX-License-Identifier: Apache-2.0
//
// Part of NvproPyramid, a Slang transcription of NVIDIA's nvpro_pyramid (see NvproPyramid.slang for the
// attribution and the list of changes). This file holds the values of the PYRAMID_REDUCTION axis: what the
// original's NVPRO_PYRAMID_TYPE and NVPRO_PYRAMID_REDUCE* macros defined.
implementing NvproPyramid;
import LodestoneAttributes;

/** The reduction of one block of samples into one sample of the next level. The sample type is float4. */
public interface IPyramidReduction
{
    /** NVPRO_PYRAMID_REDUCE: three samples, each with its weight. The weights are positive and sum to 1. The
     *  general pipeline uses it for the 3-tap kernel of a level with an odd size */
    float4 Reduce(float a0, float4 v0, float a1, float4 v1, float a2, float4 v2);
    /** NVPRO_PYRAMID_REDUCE2: two samples of equal weight */
    float4 Reduce2(float4 v0, float4 v1);
    /** NVPRO_PYRAMID_REDUCE4: a 2x2 square of samples of equal weight */
    float4 Reduce4(float4 v00, float4 v01, float4 v10, float4 v11);
    /** True when one bilinear fetch at the center of a 2x2 block gives Reduce4 of its texels. Then the fast
     *  pipeline's LOAD_REDUCE4 uses the sampler, as the original strongly recommends */
    bool BilinearIsExact();
}

/** The box filter: a mip chain of color, height, normals or any other linear quantity */
[ls_axis_interface_impl("IPyramidReduction")]
public struct PyramidAverage : IPyramidReduction
{
    public float4 Reduce(float a0, float4 v0, float a1, float4 v1, float a2, float4 v2)
    {
        return a0 * v0 + a1 * v1 + a2 * v2;
    }

    public float4 Reduce2(float4 v0, float4 v1)
    {
        return 0.5f * (v0 + v1);
    }

    public float4 Reduce4(float4 v00, float4 v01, float4 v10, float4 v11)
    {
        return 0.25f * ((v00 + v01) + (v10 + v11));
    }

    public bool BilinearIsExact()
    {
        return true;
    }
}

/** The minimum of each block, per channel: for example the nearest depth of a reversed-Z depth pyramid */
[ls_axis_interface_impl("IPyramidReduction")]
public struct PyramidMin : IPyramidReduction
{
    public float4 Reduce(float a0, float4 v0, float a1, float4 v1, float a2, float4 v2)
    {
        return min(min(v0, v1), v2);
    }

    public float4 Reduce2(float4 v0, float4 v1)
    {
        return min(v0, v1);
    }

    public float4 Reduce4(float4 v00, float4 v01, float4 v10, float4 v11)
    {
        return min(min(v00, v01), min(v10, v11));
    }

    public bool BilinearIsExact()
    {
        return false;
    }
}

/** The maximum of each block, per channel: for example the farthest depth of a depth pyramid for occlusion tests */
[ls_axis_interface_impl("IPyramidReduction")]
public struct PyramidMax : IPyramidReduction
{
    public float4 Reduce(float a0, float4 v0, float a1, float4 v1, float a2, float4 v2)
    {
        return max(max(v0, v1), v2);
    }

    public float4 Reduce2(float4 v0, float4 v1)
    {
        return max(v0, v1);
    }

    public float4 Reduce4(float4 v00, float4 v01, float4 v10, float4 v11)
    {
        return max(max(v00, v01), max(v10, v11));
    }

    public bool BilinearIsExact()
    {
        return false;
    }
}
