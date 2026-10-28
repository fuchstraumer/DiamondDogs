module ColorComposite;
import LodestoneAttributes;
import LodestoneShims;
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

[ls_axis_enum][ls_axis_kind("Technique")]
extern static const ColorEncoding ENCODE = ColorEncoding::SrgbNonlinear;

[ls_extent_2d("1920", "1080")]
Texture2D<float4> SceneTarget;
// premultiplied, sRGB-encoded
[ls_extent_2d("1920", "1080")]
Texture2D<float4> UiTarget;
[ls_element_count("1")]
StructuredBuffer<float> ExposureEv;
[ls_extent_2d("1920", "1080")]
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
    output.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
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
internal float3 CompositeUi(float3 sceneNits, float4 ui, DisplayParameters display)
{
    if (ui.a <= 0.0f)
    {
        return sceneNits;
    }

    const float3 encoded = saturate(ui.rgb / ui.a);
    const float3 uiNits = Rec709ToTargetPrimaries(SrgbDecode(encoded)) * display.PaperWhiteNits;

    return lerp(sceneNits, uiNits, ui.a);
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
float4 ColorCompositeFS(CompositeVaryings input, uniform DisplayParameters display) : SV_Target
{
    const int3 texel = int3(int2(input.Position.xy), 0);
    const float3 scene = SceneTarget.Load(texel).rgb;
    const float4 ui = UiTarget.Load(texel);
    const float exposureEv = ExposureEv[0];

    float3 combinedScene = scene;
    if (display.BloomIntensity > 0.0f)
    {
        uint w;
        uint h;
        SceneTarget.GetDimensions(w, h);
        const float2 uv = input.Position.xy / float2(float(w), float(h));
        const float3 bloom = BloomTarget.SampleLevel(BloomSampler, uv, 0.0f).rgb;
        combinedScene = lerp(scene, bloom, display.BloomIntensity);
    }

    const float3 exposed = combinedScene * exp2(exposureEv);

    TONEMAPPER tonemapper = {};
    const float3 tonemapped = tonemapper.Tonemap(exposed, display);

    const float3 targetRelative = clamp(Rec2020ToTargetPrimaries(tonemapped), 0.0f, display.Headroom());
    const float3 sceneNits = targetRelative * display.PaperWhiteNits;
    const float3 composited = CompositeUi(sceneNits, ui, display);

    return float4(EncodeForDisplay(composited, display), 1.0f);
}
