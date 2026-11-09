// Pointwise value nodes. The vocabulary (the names and what each node does) follows libnoise, which the author's CUDA_Noise
// ported; the math is standard and written here, with no libnoise code (libnoise is LGPL).
implementing NoiseNodes;

public struct Const : IField3
{
    public float value;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return value;
    }

    public float bound()
    {
        return abs(value);
    }
}

public struct ScaleBias<S : IField3> : IField3
{
    public S source;
    public float scale;
    public float bias;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return source.eval(p) * scale + bias;
    }

    public float bound()
    {
        return source.bound() * abs(scale) + abs(bias);
    }
}

public struct Abs<S : IField3> : IField3
{
    public S source;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return abs(source.eval(p));
    }

    public float bound()
    {
        return source.bound();
    }
}

public struct Invert<S : IField3> : IField3
{
    public S source;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return -source.eval(p);
    }

    public float bound()
    {
        return source.bound();
    }
}

public struct Clamp<S : IField3> : IField3
{
    public S source;
    public float lower;
    public float upper;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return clamp(source.eval(p), lower, upper);
    }

    public float bound()
    {
        return min(source.bound(), max(abs(lower), abs(upper)));
    }
}

/// Maps the source from [-1, 1] to [0, 1], raises it to the exponent, and maps it back.
public struct Exponent<S : IField3> : IField3
{
    public S source;
    public float exponent;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const float unit = max(source.eval(p) * 0.5 + 0.5, 0.0);
        return pow(unit, exponent) * 2.0 - 1.0;
    }

    public float bound()
    {
        const float largestUnit = source.bound() * 0.5 + 0.5;
        return max(1.0, abs(pow(largestUnit, exponent) * 2.0 - 1.0));
    }
}

public struct Add<L : IField3, R : IField3> : IField3
{
    public L left;
    public R right;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return left.eval(p) + right.eval(p);
    }

    public float bound()
    {
        return left.bound() + right.bound();
    }
}

public struct Multiply<L : IField3, R : IField3> : IField3
{
    public L left;
    public R right;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return left.eval(p) * right.eval(p);
    }

    public float bound()
    {
        return left.bound() * right.bound();
    }
}

public struct Min<L : IField3, R : IField3> : IField3
{
    public L left;
    public R right;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return min(left.eval(p), right.eval(p));
    }

    public float bound()
    {
        return max(left.bound(), right.bound());
    }
}

public struct Max<L : IField3, R : IField3> : IField3
{
    public L left;
    public R right;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return max(left.eval(p), right.eval(p));
    }

    public float bound()
    {
        return max(left.bound(), right.bound());
    }
}

/// Returns sign(base) * |base|^exponent, so that a negative base gives a real result.
public struct Power<B : IField3, E : IField3> : IField3
{
    public B base;
    public E exponent;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const float baseValue = base.eval(p);
        const float magnitude = pow(abs(baseValue), exponent.eval(p));
        return baseValue < 0.0 ? -magnitude : magnitude;
    }

    public float bound()
    {
        return k_UnknownBound;
    }
}

/// Returns lerp(left, right, saturate(control)).
public struct Blend<L : IField3, R : IField3, C : IField3> : IField3
{
    public L left;
    public R right;
    public C control;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return lerp(left.eval(p), right.eval(p), saturate(control.eval(p)));
    }

    public float bound()
    {
        return max(left.bound(), right.bound());
    }
}

/// Returns smoothstep(edge0, edge1, x). Slang's smoothstep has no derivative, and Select must have one.
[Differentiable]
float DifferentiableSmoothStep(no_diff float edge0, no_diff float edge1, float x)
{
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

/// Returns first where lower <= control <= upper, and second elsewhere. A smoothstep of half-width falloff blends the
/// two at each edge.
public struct Select<F : IField3, S : IField3, C : IField3> : IField3
{
    public F first;
    public S second;
    public C control;
    public float lower;
    public float upper;
    public float falloff;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        // a smoothstep with equal edges divides by zero, so a zero falloff becomes a very small one
        const float halfWidth = max(falloff, 1.0e-6);
        const float controlValue = control.eval(p);
        const float aboveLower = DifferentiableSmoothStep(lower - halfWidth, lower + halfWidth, controlValue);
        const float belowUpper = 1.0 - DifferentiableSmoothStep(upper - halfWidth, upper + halfWidth, controlValue);
        return lerp(second.eval(p), first.eval(p), aboveLower * belowUpper);
    }

    public float bound()
    {
        return max(first.bound(), second.bound());
    }
}

/// Returns the index of the control point at or below value, in [0, count - 2]. The points are sorted by input.
[Differentiable]
int FindSegment(no_diff float value, no_diff float2 points[k_MaxControlPoints], no_diff int count)
{
    int segment = 0;

    [MaxIters(16)]
    for (int index = 1; index < count - 1; ++index)
    {
        if (value >= points[index].x)
        {
            segment = index;
        }
    }

    return segment;
}

/// Maps the source through a Catmull-Rom spline. Each control point is (input, output), sorted by input. The host
/// writes at least 4 points. Outside the first and last input, the end segments extrapolate.
public struct Curve<S : IField3> : IField3
{
    public S source;
    public float2 points[k_MaxControlPoints];
    public int pointCount;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const float value = source.eval(p);
        const int count = clamp(pointCount, 2, k_MaxControlPoints);
        const int segment = FindSegment(detach(value), points, count);

        const float2 p0 = points[max(segment - 1, 0)];
        const float2 p1 = points[segment];
        const float2 p2 = points[segment + 1];
        const float2 p3 = points[min(segment + 2, count - 1)];

        const float t = (value - p1.x) / (p2.x - p1.x);
        const float t2 = t * t;
        const float t3 = t2 * t;

        // the uniform Catmull-Rom basis (Catmull and Rom, 1974)
        return 0.5 * (2.0 * p1.y + (p2.y - p0.y) * t + (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2
                      + (3.0 * p1.y - p0.y - 3.0 * p2.y + p3.y) * t3);
    }

    public float bound()
    {
        // a Catmull-Rom spline can overshoot its control points
        return k_UnknownBound;
    }
}

/// Maps the source onto terraces: between two levels, the output eases from the lower level to the upper one along t^2,
/// so each terrace is flat at its lower edge and steep at its upper edge. invert != 0 flips that. The host writes at
/// least 2 levels, sorted.
public struct Terrace<S : IField3> : IField3
{
    public S source;
    // .x is the level and .y is unused: Curve and Terrace share FindSegment, and a uniform array element takes 16 bytes
    // either way
    public float2 levels[k_MaxControlPoints];
    public int levelCount;
    public int invert;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const float value = source.eval(p);
        const int count = clamp(levelCount, 2, k_MaxControlPoints);
        const int segment = FindSegment(detach(value), levels, count);

        float lowerLevel = levels[segment].x;
        float upperLevel = levels[segment + 1].x;
        float t = saturate((value - lowerLevel) / (upperLevel - lowerLevel));

        if (invert != 0)
        {
            t = 1.0 - t;
            const float swap = lowerLevel;
            lowerLevel = upperLevel;
            upperLevel = swap;
        }

        return lerp(lowerLevel, upperLevel, t * t);
    }

    public float bound()
    {
        const int count = clamp(levelCount, 2, k_MaxControlPoints);
        return max(abs(levels[0].x), abs(levels[count - 1].x));
    }
}
