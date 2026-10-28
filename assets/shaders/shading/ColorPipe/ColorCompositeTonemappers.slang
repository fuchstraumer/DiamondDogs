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
    float3 Tonemap(float3 exposed_ap1, DisplayParameters display);
};

[ls_axis_interface_impl("ITonemapper")]
public struct Passthrough : ITonemapper
{
    public float3 Tonemap(float3 exposed_ap1, DisplayParameters display)
    {
        return clamp(Ap1ToRec2020(exposed_ap1), 0.0, display.Headroom());
    }
};

// 11 zones of 1 EV, -5 EV to +5 EV around middle gray. k_FalseColorZonesSrgb in ColorComposite.cpp must match
internal static const int k_FalseColorZoneCount = 11;
internal static const int k_FalseColorLowestZoneEv = -5;
internal static const float k_MiddleGray = 0.18;
// the luminance below which the EV stops falling, so that log2 never sees 0
internal static const float k_LuminanceFloor = 1.0e-30;

internal static const float3 k_FalseColorZonesSrgb[11] =
{
    float3(0.10, 0.00, 0.20),
    float3(0.00, 0.00, 0.55),
    float3(0.00, 0.35, 0.75),
    float3(0.00, 0.65, 0.65),
    float3(0.10, 0.65, 0.25),
    float3(0.46, 0.46, 0.46),
    float3(0.85, 0.85, 0.10),
    float3(1.00, 0.60, 0.00),
    float3(1.00, 0.25, 0.00),
    float3(0.85, 0.00, 0.35),
    float3(1.00, 1.00, 1.00),
};

[ls_axis_interface_impl("ITonemapper")]
public struct FalseColorEv : ITonemapper
{
    public float3 Tonemap(float3 exposed_ap1, DisplayParameters display)
    {
        const float luminance = Ap1Luminance(exposed_ap1);
        const float ev = log2(max(luminance, k_LuminanceFloor) / k_MiddleGray);
        const int nearestEv = int(floor(ev + 0.5));
        const int zone = clamp(nearestEv, k_FalseColorLowestZoneEv, k_FalseColorLowestZoneEv + k_FalseColorZoneCount - 1) - k_FalseColorLowestZoneEv;
        const float3 linearRec709 = SrgbDecode(k_FalseColorZonesSrgb[zone]);

        return Ap1ToRec2020(Rec709ToAp1(linearRec709));
    }
};

// Khronos PBR Neutral tonemapper
internal float3 PbrNeutralCurve(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }

    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    const float g = 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(float3(newPeak), color, g);
}

internal float3 TonemapKhronosNeutral(float3 exposed_ap1, DisplayParameters display)
{
    const float3 rec709 = max(Ap1ToRec709(exposed_ap1), 0.0);
    const float3 tonemappedRec709 = PbrNeutralCurve(rec709);
    const float3 rec2020 = Ap1ToRec2020(Rec709ToAp1(tonemappedRec709));
    return clamp(rec2020, 0.0, display.Headroom());
}

[ls_axis_interface_impl("ITonemapper")]
public struct KhronosNeutral : ITonemapper
{
    public float3 Tonemap(float3 exposed_ap1, DisplayParameters display)
    {
        return TonemapKhronosNeutral(exposed_ap1, display);
    }
};

// AgX tonemapper
internal static const float3x3 k_AgxInsetMatrix = float3x3(
    0.842398929049532, 0.0423989290495314, 0.0423989290495315,
    0.0784301651048638, 0.8784301651048639, 0.0784301651048638,
    0.0791709058456046, 0.0791709058456047, 0.8791709058456048
);

internal static const float3x3 k_AgxOutsetMatrix = float3x3(
     1.1970013386880847, -0.0529986613119142, -0.0529986613119143,
    -0.0980377063810797,  1.15196229361892,   -0.0980377063810798,
    -0.0989636323070057, -0.0989636323070059,  1.151036367692994
);

internal static const float k_AgxMinEv = -12.473931188332412;
internal static const float k_AgxMaxEv = 4.026068811667588;
internal static const float k_AgxInvEvRange = 1.0 / (4.026068811667588 - (-12.473931188332412));

internal float AgxDefaultContrastApprox(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

internal float3 AgxDefaultContrastApprox(float3 x)
{
    return float3(AgxDefaultContrastApprox(x.r), AgxDefaultContrastApprox(x.g), AgxDefaultContrastApprox(x.b));
}

internal float3 TonemapAgX(float3 exposed_ap1, DisplayParameters display)
{
    const float3 rec709 = max(Ap1ToRec709(exposed_ap1), 0.0);
    const float3 inset = mul(rec709, k_AgxInsetMatrix);
    const float3 logColor = clamp(log2(max(inset, 1.0e-10)), k_AgxMinEv, k_AgxMaxEv);
    const float3 normLog = (logColor - k_AgxMinEv) * k_AgxInvEvRange;
    const float3 curved = AgxDefaultContrastApprox(normLog);
    const float3 outset = mul(curved, k_AgxOutsetMatrix);
    const float3 srgbLinear = clamp(outset, 0.0, 1.0);
    const float3 rec2020 = Ap1ToRec2020(Rec709ToAp1(srgbLinear));
    return clamp(rec2020, 0.0, display.Headroom());
}

[ls_axis_interface_impl("ITonemapper")]
public struct AgX : ITonemapper
{
    public float3 Tonemap(float3 exposed_ap1, DisplayParameters display)
    {
        return TonemapAgX(exposed_ap1, display);
    }
};
