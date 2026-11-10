// Spike S3: can an interface axis take a generic type expression (Composed<A, B>) as a value? Lodestone links each
// value as "export struct GRAPH : ISpikeGraph = <impl name>;" (PermutationValue.cpp), with the bare type name.
// Three candidates are tagged here:
//   - SpikeConstantGraph, a plain struct (the control);
//   - SpikeComposedGraph<A, B>, a generic struct (expected to fail: the link line has no type arguments);
//   - SpikeNamedComposition, a plain struct that wraps SpikeComposedGraph<SpikeLeafA, SpikeLeafB> (the workaround).
module SpikeGenericAxis;
import LodestoneAttributes;

public interface ISpikeLeaf
{
    float eval(float3 p);
}

public struct SpikeLeafA : ISpikeLeaf
{
    public float eval(float3 p)
    {
        return p.x;
    }
}

public struct SpikeLeafB : ISpikeLeaf
{
    public float eval(float3 p)
    {
        return p.y;
    }
}

public interface ISpikeGraph
{
    float eval(float3 p);
}

[ls_axis_interface_impl("ISpikeGraph")]
public struct SpikeConstantGraph : ISpikeGraph
{
    public float eval(float3 p)
    {
        return 1.0;
    }
}

// Lodestone drops this tag with no error: the axis gets two values, not three
[ls_axis_interface_impl("ISpikeGraph")]
public struct SpikeComposedGraph<A : ISpikeLeaf, B : ISpikeLeaf> : ISpikeGraph
{
    public float eval(float3 p)
    {
        A a = {};
        B b = {};
        return a.eval(p) * b.eval(p);
    }
}

[ls_axis_interface_impl("ISpikeGraph")]
public struct SpikeNamedComposition : ISpikeGraph
{
    public float eval(float3 p)
    {
        SpikeComposedGraph<SpikeLeafA, SpikeLeafB> composed = {};
        return composed.eval(p);
    }
}

[ls_axis_interface]
public extern struct GRAPH : ISpikeGraph = SpikeConstantGraph;

RWStructuredBuffer<float> gGenericOut;

[shader("compute")]
[numthreads(64, 1, 1)]
void SpikeGenericAxisCS(uint3 id : SV_DispatchThreadID)
{
    GRAPH graph = {};
    gGenericOut[id.x] = graph.eval(float3(id));
}
