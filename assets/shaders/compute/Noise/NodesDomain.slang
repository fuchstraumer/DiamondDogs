// Domain nodes: each one chooses the position where its source is evaluated.
implementing NoiseNodes;

/// A vector field made of three scalar fields, one for each axis. Give the three fields different seeds, so that the
/// channels are not correlated.
public struct VecFromFields<X : IField3, Y : IField3, Z : IField3> : IVecField3
{
    public X x;
    public Y y;
    public Z z;

    [Differentiable, NoDiffThis]
    public float3 eval(float3 p)
    {
        return float3(x.eval(p), y.eval(p), z.eval(p));
    }

    public float bound()
    {
        return length(float3(x.bound(), y.bound(), z.bound()));
    }
}

/// Evaluates the source at p + amount * offset(p). The largest displacement is amount * offset.bound().
public struct Warp<S : IField3, W : IVecField3> : IField3
{
    public S source;
    public W offset;
    public float amount;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return source.eval(p + amount * offset.eval(p));
    }

    public float bound()
    {
        return source.bound();
    }
}

/// Evaluates the source at mul(float4(p, 1), transform): translate, scale and rotate in one node. It replaces the
/// TranslatePoint, ScalePoint and RotatePoint modules of libnoise.
public struct Affine<S : IField3> : IField3
{
    public S source;
    public float4x3 transform;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return source.eval(mul(float4(p, 1.0), transform));
    }

    public float bound()
    {
        return source.bound();
    }
}
