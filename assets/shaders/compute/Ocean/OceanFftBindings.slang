implementing OceanFft;
import LodestoneAttributes;
// Single source of truth for the binding locations for the IFFT compute shaders:
// they can each refer to them by the same set of names, we can coordinate the locations
// ourselves (which is somewhat important with WebGPU, since that's so much more manual
// and we aren't doing runtime reflection at all!)
internal const static uint IFFT_COMMON_BIND_LOC = 0;

[ls_element_count("IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES")]
uniform RWStructuredBuffer<half4> IfftInput;
ConstantBuffer<IfftParameters> IfftParams;
uniform Texture1D<half2> IfftSinCosLUT;
uniform SamplerState IfftLUTSampler;
