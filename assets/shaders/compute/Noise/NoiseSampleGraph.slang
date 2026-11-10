// A fused graph (mode B) written by hand: one type expression that uses every node of NoiseNodes and every basis and
// rule, so that one cook checks that they compose and differentiate together. The scheduler of the noise graph will
// write expressions of this form.
module NoiseSampleGraph;
import NoiseBases;
import NoiseFractal;
import NoiseRules;
import NoiseNodes;
import NoiseGrids;
import NoiseCellular;

typealias Ridges = Fractal<Simplex3, RidgedMulti>;
typealias Hills = Fractal<Perlin3, Fbm>;
typealias Swells = Fractal<Value3, Billow>;
typealias Cliffs = Fractal<Simplex3, Swiss>;
typealias Dunes = Fractal<Perlin3, Jordan>;

typealias Land = Warp<Ridges, VecFromFields<Hills, Swells, Hills>>;
typealias Plains = Terrace<ScaleBias<Cliffs>>;
typealias Mix = Blend<Land, Plains, Clamp<Affine<Dunes>>>;
typealias Detail = Multiply<Curve<Exponent<Abs<Hills>>>, Cellular3>;
typealias Floor = Max<Min<Invert<Const>, Const>, Power<Const, Const>>;
typealias SampleGraph = Select<Add<Mix, Detail>, Floor, Swells>;

struct SampleGraphParameters
{
    PlaneGrid grid;
    SampleGraph graph;
}

ParameterBlock<SampleGraphParameters> gSampleGraph;
RWStructuredBuffer<float4> gSampleGraphOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void NoiseSampleGraphCS(uint3 id : SV_DispatchThreadID)
{
    const int2 element = int2(id.xy);

    if (!gSampleGraph.grid.contains(element))
    {
        return;
    }

    const float3 position = gSampleGraph.grid.position(element);
    gSampleGraphOutput[gSampleGraph.grid.linearIndex(element)] = gSampleGraph.graph.evalGrad(position);
}
