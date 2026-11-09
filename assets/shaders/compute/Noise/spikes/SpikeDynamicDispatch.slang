// Spike S2: a field that holds its source as an interface value (dynamic dispatch), as the planet notes proposed for
// "one shader for any graph". Expected to fail: Shifted.eval calls IDispatchField.eval, which dispatches back to
// Shifted.eval, so the call graph is recursive, and a nested existential has no bounded size.
module SpikeDynamicDispatch;

[anyValueSize(32)]
public interface IDispatchField
{
    float eval(float3 p);
}

public struct DispatchConstant : IDispatchField
{
    public float value;

    public float eval(float3 p)
    {
        return value;
    }
}

public struct DispatchShifted : IDispatchField
{
    public IDispatchField source;
    public float3 offset;

    public float eval(float3 p)
    {
        return source.eval(p + offset);
    }
}

StructuredBuffer<IDispatchField> gGraph;
RWStructuredBuffer<float> gDispatchOut;

[shader("compute")]
[numthreads(64, 1, 1)]
void SpikeDynamicDispatchCS(uint3 id : SV_DispatchThreadID)
{
    const IDispatchField root = gGraph[0];
    gDispatchOut[id.x] = root.eval(float3(id));
}
