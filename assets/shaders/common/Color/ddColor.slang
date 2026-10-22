module ddColor;

// Color math for the HDR pipe, in float. modules/ColorPipe has the same functions in double (the CPU reference), and
// ColorMathTest compares the two on the GPU.
//
// The matrices in ddColorGenerated.slang are written by color_matrix_gen from the same chromaticities as the CPU
// reference. They multiply a row vector: mul(rgb, matrix). Gamut conversions use Bradford adaptation.
__include ddColorGenerated;

internal static const float k_SrgbDecodeBreak = 0.0404482362771082;
internal static const float k_SrgbEncodeBreak = 0.00313066844250063;

internal static const float k_PqPeakNits = 10000.0;
internal static const float k_PqM1 = 2610.0 / 16384.0;
internal static const float k_PqM2 = 2523.0 / 4096.0 * 128.0;
internal static const float k_PqC1 = 3424.0 / 4096.0;
internal static const float k_PqC2 = 2413.0 / 4096.0 * 32.0;
internal static const float k_PqC3 = 2392.0 / 4096.0 * 32.0;

public static const float ScRgbReferenceNits = 80.0;
public static const float KelvinMinimum = 1000.0;
public static const float KelvinMaximum = 15000.0;

public float3 Rec709ToAp1(float3 rgb)
{
    return mul(rgb, Rec709ToAp1Matrix);
}

public float3 Ap1ToRec709(float3 rgb)
{
    return mul(rgb, Ap1ToRec709Matrix);
}

public float3 Rec2020ToAp1(float3 rgb)
{
    return mul(rgb, Rec2020ToAp1Matrix);
}

public float3 Ap1ToRec2020(float3 rgb)
{
    return mul(rgb, Ap1ToRec2020Matrix);
}

public float3 DisplayP3ToAp1(float3 rgb)
{
    return mul(rgb, DisplayP3ToAp1Matrix);
}

public float3 Ap1ToDisplayP3(float3 rgb)
{
    return mul(rgb, Ap1ToDisplayP3Matrix);
}

public float Ap1Luminance(float3 rgb)
{
    return dot(rgb, Ap1LuminanceWeights);
}

internal float SrgbEncodeMagnitude(float linear)
{
    if (linear <= k_SrgbEncodeBreak)
    {
        return 12.92 * linear;
    }

    return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

internal float SrgbDecodeMagnitude(float encoded)
{
    if (encoded <= k_SrgbDecodeBreak)
    {
        return encoded / 12.92;
    }

    return pow((encoded + 0.055) / 1.055, 2.4);
}

public float SrgbEncode(float linear)
{
    return sign(linear) * SrgbEncodeMagnitude(abs(linear));
}

public float SrgbDecode(float encoded)
{
    return sign(encoded) * SrgbDecodeMagnitude(abs(encoded));
}

public float Gamma22Encode(float linear)
{
    return sign(linear) * pow(abs(linear), 1.0 / 2.2);
}

public float Gamma22Decode(float encoded)
{
    return sign(encoded) * pow(abs(encoded), 2.2);
}

// absolute nits in, PQ signal in [0, 1] out
public float PqEncode(float nits)
{
    const float normalized = clamp(nits / k_PqPeakNits, 0.0, 1.0);
    const float power = pow(normalized, k_PqM1);
    const float ratio = (k_PqC1 + k_PqC2 * power) / (1.0 + k_PqC3 * power);

    return pow(ratio, k_PqM2);
}

public float PqDecode(float signal)
{
    const float clamped = clamp(signal, 0.0, 1.0);
    const float power = pow(clamped, 1.0 / k_PqM2);
    const float numerator = max(power - k_PqC1, 0.0);
    const float denominator = k_PqC2 - k_PqC3 * power;

    return k_PqPeakNits * pow(numerator / denominator, 1.0 / k_PqM1);
}

public float ScRgbEncode(float nits)
{
    return nits / ScRgbReferenceNits;
}

public float ScRgbDecode(float scrgb)
{
    return scrgb * ScRgbReferenceNits;
}

public float3 SrgbEncode(float3 linear)
{
    return float3(SrgbEncode(linear.x), SrgbEncode(linear.y), SrgbEncode(linear.z));
}

public float3 SrgbDecode(float3 encoded)
{
    return float3(SrgbDecode(encoded.x), SrgbDecode(encoded.y), SrgbDecode(encoded.z));
}

public float3 Gamma22Encode(float3 linear)
{
    return float3(Gamma22Encode(linear.x), Gamma22Encode(linear.y), Gamma22Encode(linear.z));
}

public float3 Gamma22Decode(float3 encoded)
{
    return float3(Gamma22Decode(encoded.x), Gamma22Decode(encoded.y), Gamma22Decode(encoded.z));
}

public float3 PqEncode(float3 nits)
{
    return float3(PqEncode(nits.x), PqEncode(nits.y), PqEncode(nits.z));
}

public float3 PqDecode(float3 signal)
{
    return float3(PqDecode(signal.x), PqDecode(signal.y), PqDecode(signal.z));
}

public float3 ScRgbEncode(float3 nits)
{
    return nits / ScRgbReferenceNits;
}

public float3 ScRgbDecode(float3 scrgb)
{
    return scrgb * ScRgbReferenceNits;
}

// the Krystek (1985) approximation of the Planckian locus in CIE 1960 uv, converted to xy. Kelvin is clamped to the
// valid range of 1000 K to 15000 K
public float2 KelvinToXy(float kelvin)
{
    const float t = clamp(kelvin, KelvinMinimum, KelvinMaximum);
    const float tSquared = t * t;

    const float uNumerator = 0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * tSquared;
    const float uDenominator = 1.0 + 8.42420235e-4 * t + 7.08145163e-7 * tSquared;
    const float vNumerator = 0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * tSquared;
    const float vDenominator = 1.0 - 2.89741816e-5 * t + 1.61456053e-7 * tSquared;
    const float u = uNumerator / uDenominator;
    const float v = vNumerator / vDenominator;

    const float scale = 2.0 * u - 8.0 * v + 4.0;

    return float2(3.0 * u / scale, 2.0 * v / scale);
}

// the linear AP1 color of a blackbody, normalized to Y = 1
public float3 KelvinToAp1(float kelvin)
{
    const float2 xy = KelvinToXy(kelvin);
    const float3 xyz = float3(xy.x / xy.y, 1.0, (1.0 - xy.x - xy.y) / xy.y);

    return mul(xyz, XyzToAp1Matrix);
}
