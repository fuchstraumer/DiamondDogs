// The node kernel of Cellular3, for mode A. It writes the value in x and the gradient in yzw, as NoiseKernels does.
// Cellular3 has runtime members for the metric and the output, so one kernel serves every combination.
module NoiseCellularKernels;
import NoiseCellular;
import NoiseGrids;

struct CellularNodeParameters
{
    PlaneGrid grid;
    Cellular3 cellular;
}

ParameterBlock<CellularNodeParameters> gCellularNode;
RWStructuredBuffer<float4> gNodeOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void NoiseCellularCS(uint3 id : SV_DispatchThreadID)
{
    const int2 element = int2(id.xy);

    if (!gCellularNode.grid.contains(element))
    {
        return;
    }

    const float3 position = gCellularNode.grid.position(element);
    gNodeOutput[gCellularNode.grid.linearIndex(element)] = gCellularNode.cellular.evalGrad(position);
}
