implementing ColorComposite;

// the push constant block of ColorCompositeFS. DisplayParameters in ColorPipe.cpp must match it: four floats
public struct DisplayParameters
{
    public float PaperWhiteNits;
    public float PeakNits;
    // no tonemapper reads this yet
    public float BlackNits;
    public float Padding;

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
