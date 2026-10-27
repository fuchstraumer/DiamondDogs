implementing OceanFft;
import LodestoneAttributes;
// Single source of truth for the resources of every OceanFft entry point: they can each refer to them by the
// same set of names. Slang assigns the bindings, and the cook records them in the manifest, so the host reads
// them from there instead of a second copy of these numbers (that matters even more on WebGPU, where we
// aren't doing runtime reflection at all!)

// Every spectrum buffer stores one element per (x, y, cascade), linearized as x + IFFT_SIZE * (y + IFFT_SIZE * cascade)
[ls_element_count("IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES")]
uniform RWStructuredBuffer<half2> h0K; // h0(k), written once by SpectrumInitCS
[ls_element_count("IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES")]
uniform RWStructuredBuffer<half2> h0KConjugate; // conj(h0(-k)), written once by SpectrumConjugateCS
// omega for each wave vector. The wave vector itself follows from the thread position (see WaveSpacing)
[ls_element_count("IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES")]
uniform RWStructuredBuffer<half> WaveData;

// IFFT_NUM_SLICES slices of IFFT_SIZE * IFFT_SIZE elements: slice s of cascade c is z = s * IFFT_NUM_WAVE_CASCADES + c.
// Each half4 holds two complex values, so each slice carries four real fields (see OceanFftStages.slang).
[ls_element_count("IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES * 2")]
uniform RWStructuredBuffer<half4> IfftInput;

ConstantBuffer<CascadeParameters> CascadeParams;
ConstantBuffer<SpectrumParameters> SpectrumParams;

// sin/cos table for the LUT overload of ComplexExp (y pre-conjugated), from ocean_fft_support.py. One table at the
// largest IFFT size serves every size, read with Load, so it needs no sampler. No IFFT kernel reads it yet
static const uint OCEAN_FFT_SINCOS_LUT_SIZE = 1024u;
static const uint OCEAN_FFT_SINCOS_LUT_LOG2_SIZE = 10u;
[ls_extent_1d("1024")]
uniform Texture1D<half2> IfftSinCosLUT;

// Donelan-Banner normalization LUT, only read when DirectionalSpreadingMode is OCEAN_FFT_SPREAD_MODE_DONELAN_BANNER
uniform Texture2D normTexture;
uniform SamplerState normSampler;

// Resolved output, one layer per cascade, for the render passes to sample:
// Displacement: (x, y, z) displacement in meters, w: the Jacobian of the horizontal displacement (foam where it's < 1)
// Derivatives: (dHeight/dx, dHeight/dz, dDx/dx, dDz/dz), with the choppiness already applied to the last two
[format("rgba16f")][ls_extent_3d("IFFT_SIZE", "IFFT_SIZE", "IFFT_NUM_WAVE_CASCADES")]
uniform RWTexture2DArray<float4> Displacement;
[format("rgba16f")][ls_extent_3d("IFFT_SIZE", "IFFT_SIZE", "IFFT_NUM_WAVE_CASCADES")]
uniform RWTexture2DArray<float4> Derivatives;
// k_i^2 only. The mean of k_i is Displacement.w - 1 + 1/n (the Jacobian is 1 + k_i - 1/n), so the Jacobian's mips give it
[format("r16f")][ls_extent_3d("IFFT_SIZE", "IFFT_SIZE", "IFFT_NUM_WAVE_CASCADES")]
uniform RWTexture2DArray<float> Whitecaps;
// persistent foam coverage [0, 1], written in place by OceanFoamSimCS (see OceanFoamSim.slang for its drifting frame)
[format("r16f")][ls_extent_3d("IFFT_SIZE", "IFFT_SIZE", "IFFT_NUM_WAVE_CASCADES")]
uniform RWTexture2DArray<float> Foam;

