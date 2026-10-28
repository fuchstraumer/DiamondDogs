module ColorComposite;
import LodestoneAttributes;
import ddColor;

// CPU reference: modules/ColorPipe/src/ColorComposite.cpp

__include ColorCompositeTonemappers;

// public, because the per-variant module that the cook makes names the cases
public enum ColorEncoding
{
    SrgbNonlinear,
    ScRgbLinear,
    Hdr10Pq,
};

[ls_axis_interface]
public extern struct TONEMAPPER : ITonemapper = Passthrough;

[ls_axis_enum]
extern static const ColorEncoding ENCODE = ColorEncoding::SrgbNonlinear;

Texture2D<float4> SceneTarget;
// premultiplied, sRGB-encoded
Texture2D<float4> UiTarget;
StructuredBuffer<float> ExposureEv;
Texture2D<float4> BloomTarget;
SamplerState BloomSampler;

public struct CompositeVaryings
{
    public float4 Position : SV_Position;
};

// one triangle that covers the screen
[shader("vertex")]
CompositeVaryings ColorCompositeVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    CompositeVaryings output;
    output.Position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

internal float3 Rec2020ToTargetPrimaries(float3 rec2020)
{
    if (ENCODE == ColorEncoding::Hdr10Pq)
    {
        return rec2020;
    }

    return Ap1ToRec709(Rec2020ToAp1(rec2020));
}

internal float3 Rec709ToTargetPrimaries(float3 rec709)
{
    if (ENCODE == ColorEncoding::Hdr10Pq)
    {
        return Ap1ToRec2020(Rec709ToAp1(rec709));
    }

    return rec709;
}

// the texel is premultiplied, so the color is divided by alpha before the sRGB decode
internal float3 CompositeUi(float3 scene_nits, float4 ui, DisplayParameters display)
{
    if (ui.a <= 0.0)
    {
        return scene_nits;
    }

    const float3 encoded = saturate(ui.rgb / ui.a);
    const float3 uiNits = Rec709ToTargetPrimaries(SrgbDecode(encoded)) * display.PaperWhiteNits;

    return lerp(scene_nits, uiNits, ui.a);
}

internal float3 EncodeForDisplay(float3 nits, DisplayParameters display)
{
    if (ENCODE == ColorEncoding::SrgbNonlinear)
    {
        return SrgbEncode(saturate(nits / display.PaperWhiteNits));
    }

    if (ENCODE == ColorEncoding::ScRgbLinear)
    {
        return ScRgbEncode(nits);
    }

    return PqEncode(nits);
}

[shader("fragment")]
float4 ColorCompositeFS(CompositeVaryings input, uniform DisplayParameters Display) : SV_Target
{
    const int3 texel = int3(int2(input.Position.xy), 0);
    const float3 scene = SceneTarget.Load(texel).rgb;
    const float4 ui = UiTarget.Load(texel);
    const float exposureEv = ExposureEv[0];

    float3 combinedScene = scene;
    if (Display.BloomIntensity > 0.0)
    {
        uint w, h;
        SceneTarget.GetDimensions(w, h);
        const float2 uv = input.Position.xy / float2(w, h);
        const float3 bloom = BloomTarget.SampleLevel(BloomSampler, uv, 0.0).rgb;
        combinedScene = lerp(scene, bloom, Display.BloomIntensity);
    }

    const float3 exposed = combinedScene * exp2(exposureEv);

    TONEMAPPER tonemapper = {};
    const float3 tonemapped = tonemapper.Tonemap(exposed, Display);

    const float3 targetRelative = clamp(Rec2020ToTargetPrimaries(tonemapped), 0.0, Display.Headroom());
    const float3 sceneNits = targetRelative * Display.PaperWhiteNits;
    const float3 composited = CompositeUi(sceneNits, ui, Display);

    return float4(EncodeForDisplay(composited, Display), 1.0);
}
