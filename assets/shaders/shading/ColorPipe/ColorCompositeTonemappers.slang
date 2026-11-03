implementing ColorComposite;

// the push constant block of ColorCompositeFS. DisplayParameters in ColorPipe.cpp must match it: four floats
public struct DisplayParameters
{
    public float PaperWhiteNits;
    public float PeakNits;
    // no tonemapper reads this yet
    public float BlackNits;
    public float BloomIntensity;

    public float Headroom()
    {
        return PeakNits / PaperWhiteNits;
    }
};

// input: exposed AP1. output: linear Rec.2020, relative to paper white, at most Headroom()
public interface ITonemapper
{
    float3 Tonemap(float3 exposedAp1, DisplayParameters display);
};

[ls_axis_interface_impl("ITonemapper")]
public struct Passthrough : ITonemapper
{
    public float3 Tonemap(float3 exposedAp1, DisplayParameters display)
    {
        return clamp(Ap1ToRec2020(exposedAp1), 0.0f, display.Headroom());
    }
};

// 11 zones of 1 EV, -5 EV to +5 EV around middle gray. k_FalseColorZonesSrgb in ColorComposite.cpp must match
internal static const int K_FALSE_COLOR_ZONE_COUNT = 11;
internal static const int K_FALSE_COLOR_LOWEST_ZONE_EV = -5;
internal static const float K_MIDDLE_GRAY = 0.18f;
// the luminance below which the EV stops falling, so that log2 never sees 0
internal static const float K_LUMINANCE_FLOOR = 1.0e-30f;

internal static const float3 K_FALSE_COLOR_ZONES_SRGB[11] =
{
    float3(0.10f, 0.00f, 0.20f),
    float3(0.00f, 0.00f, 0.55f),
    float3(0.00f, 0.35f, 0.75f),
    float3(0.00f, 0.65f, 0.65f),
    float3(0.10f, 0.65f, 0.25f),
    float3(0.46f, 0.46f, 0.46f),
    float3(0.85f, 0.85f, 0.10f),
    float3(1.00f, 0.60f, 0.00f),
    float3(1.00f, 0.25f, 0.00f),
    float3(0.85f, 0.00f, 0.35f),
    float3(1.00f, 1.00f, 1.00f),
};

[ls_axis_interface_impl("ITonemapper")]
public struct FalseColorEv : ITonemapper
{
    public float3 Tonemap(float3 exposedAp1, DisplayParameters display)
    {
        const float luminance = Ap1Luminance(exposedAp1);
        const float ev = log2(max(luminance, K_LUMINANCE_FLOOR) / K_MIDDLE_GRAY);
        const int nearestEv = int(floor(ev + 0.5f));
        const int zone = clamp(nearestEv, K_FALSE_COLOR_LOWEST_ZONE_EV, K_FALSE_COLOR_LOWEST_ZONE_EV + K_FALSE_COLOR_ZONE_COUNT - 1) - K_FALSE_COLOR_LOWEST_ZONE_EV;
        const float3 linearRec709 = SrgbDecode(K_FALSE_COLOR_ZONES_SRGB[zone]);

        return Ap1ToRec2020(Rec709ToAp1(linearRec709));
    }
};

// Khronos PBR Neutral tonemapper
internal float3 PbrNeutralCurve(float3 color)
{
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }

    const float d = 1.0f - startCompression;
    const float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    const float g = 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(float3(newPeak), color, g);
}

internal float3 TonemapKhronosNeutral(float3 exposedAp1, DisplayParameters display)
{
    const float3 rec709 = max(Ap1ToRec709(exposedAp1), 0.0f);
    const float3 tonemappedRec709 = PbrNeutralCurve(rec709);
    const float3 rec2020 = Ap1ToRec2020(Rec709ToAp1(tonemappedRec709));
    return clamp(rec2020, 0.0f, display.Headroom());
}

[ls_axis_interface_impl("ITonemapper")]
public struct KhronosNeutral : ITonemapper
{
    public float3 Tonemap(float3 exposedAp1, DisplayParameters display)
    {
        return TonemapKhronosNeutral(exposedAp1, display);
    }
};

// AgX tonemapper
internal static const float3x3 k_Ap1ToAgxInset = float3x3(
    1.42421673f, -0.0440291978f, 0.0409732508f,
    -0.444534103f, 0.965543382f, -0.0502755877f,
    0.020317373f, 0.0784858153f, 1.00930234f
);

internal static const float3x3 k_AgxOutsetToRec2020 = float3x3(
    0.731256209f, 0.0333729504f, -0.0325093627f,
    0.313566092f, 1.05138779f, 0.0119789285f,
    -0.0448223002f, -0.0847607378f, 1.02053043f
);

internal static const float K_AGX_MIN_EV = -12.473931188332412f;
internal static const float K_AGX_MAX_EV = 4.026068811667588f;
internal static const float K_AGX_INV_EV_RANGE = 1.0f / (4.026068811667588f - (-12.473931188332412f));

internal float AgxDefaultContrastApprox(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

internal float3 AgxDefaultContrastApprox(float3 x)
{
    return float3(AgxDefaultContrastApprox(x.r), AgxDefaultContrastApprox(x.g), AgxDefaultContrastApprox(x.b));
}

internal float3 TonemapAgX(float3 exposedAp1, DisplayParameters display)
{
    const float3 inset = mul(exposedAp1, k_Ap1ToAgxInset);
    const float3 logColor = clamp(log2(max(inset, 1.0e-10f)), K_AGX_MIN_EV, K_AGX_MAX_EV);
    const float3 normLog = (logColor - K_AGX_MIN_EV) * K_AGX_INV_EV_RANGE;
    const float3 curved = AgxDefaultContrastApprox(normLog);
    const float3 rec2020 = mul(curved, k_AgxOutsetToRec2020);
    return clamp(rec2020, 0.0f, display.Headroom());
}

[ls_axis_interface_impl("ITonemapper")]
public struct AgX : ITonemapper
{
    public float3 Tonemap(float3 exposedAp1, DisplayParameters display)
    {
        return TonemapAgX(exposedAp1, display);
    }
};
