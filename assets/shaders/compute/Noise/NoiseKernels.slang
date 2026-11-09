// The node kernels of the noise library, for mode A (one kernel for each node, every input materialized). Each
// kernel evaluates one field over a PlaneGrid and writes the value in x and the gradient in yzw. The grid and the
// output form will change when the noise graph rasterizer exists; today these kernels make every node cook.
module NoiseKernels;
import LodestoneAttributes;
import NoiseBases;
import NoiseFractal;
import NoiseRules;
import NoiseGrids;

[ls_axis_interface]
public extern struct BASIS : IBasis3 = Simplex3;

[ls_axis_interface]
public extern struct RULE : IOctaveRule = Fbm;

struct BasisNodeParameters
{
    PlaneGrid grid;
    float frequency;
    uint seed;
}

struct FractalNodeParameters
{
    PlaneGrid grid;
    Fractal<BASIS, RULE> fractal;
}

ParameterBlock<BasisNodeParameters> gBasisNode;
ParameterBlock<FractalNodeParameters> gFractalNode;
RWStructuredBuffer<float4> gNodeOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void NoiseBasisCS(uint3 id : SV_DispatchThreadID)
{
    const int2 element = int2(id.xy);

    if (!gBasisNode.grid.contains(element))
    {
        return;
    }

    const BASIS basis = {};
    const float3 position = gBasisNode.grid.position(element) * gBasisNode.frequency;
    const float4 noise = basis.evalGrad(MakeLatticePoint(position), gBasisNode.seed);
    gNodeOutput[gBasisNode.grid.linearIndex(element)] = float4(noise.x, noise.yzw * gBasisNode.frequency);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void NoiseFractalCS(uint3 id : SV_DispatchThreadID)
{
    const int2 element = int2(id.xy);

    if (!gFractalNode.grid.contains(element))
    {
        return;
    }

    const float3 position = gFractalNode.grid.position(element);
    gNodeOutput[gFractalNode.grid.linearIndex(element)] = gFractalNode.fractal.evalGrad(position);
}
