module ColorMathKernels;
import LodestoneAttributes;
import ddColor;

// ColorMathTest cooks this module with ddColor, applies every ddColor function to the same input samples, and
// compares each output buffer with the double precision reference in modules/ColorPipe. The input and the output
// ranges that this file picks must match the CPU side of the test (main.cpp).

StructuredBuffer<float4> Input;

RWStructuredBuffer<float4> Rec709ToAp1Out;
RWStructuredBuffer<float4> Ap1ToRec709Out;
RWStructuredBuffer<float4> Rec2020ToAp1Out;
RWStructuredBuffer<float4> Ap1ToRec2020Out;
RWStructuredBuffer<float4> DisplayP3ToAp1Out;
RWStructuredBuffer<float4> Ap1ToDisplayP3Out;
RWStructuredBuffer<float4> LuminanceOut;
RWStructuredBuffer<float4> SrgbEncodeOut;
RWStructuredBuffer<float4> SrgbDecodeOut;
RWStructuredBuffer<float4> Gamma22EncodeOut;
RWStructuredBuffer<float4> Gamma22DecodeOut;
RWStructuredBuffer<float4> PqEncodeOut;
RWStructuredBuffer<float4> PqDecodeOut;
RWStructuredBuffer<float4> ScRgbEncodeOut;
RWStructuredBuffer<float4> ScRgbDecodeOut;
RWStructuredBuffer<float4> KelvinOut;

// the samples span [0, 64], and a decode function reads a signal in [0, 1]
static const float k_SampleRange = 64.0;

[shader("compute")]
[numthreads(64, 1, 1)]
void ColorMathCS(uint3 threadId : SV_DispatchThreadID)
{
    uint count = 0;
    uint stride = 0;
    Input.GetDimensions(count, stride);

    if (threadId.x >= count)
    {
        return;
    }

    const uint index = threadId.x;
    const float3 inputRgb = Input[index].xyz;
    const float3 signal = saturate(inputRgb / k_SampleRange);
    const float kelvin = lerp(KelvinMinimum, KelvinMaximum, signal.x);

    Rec709ToAp1Out[index] = float4(Rec709ToAp1(inputRgb), 0.0);
    Ap1ToRec709Out[index] = float4(Ap1ToRec709(inputRgb), 0.0);
    Rec2020ToAp1Out[index] = float4(Rec2020ToAp1(inputRgb), 0.0);
    Ap1ToRec2020Out[index] = float4(Ap1ToRec2020(inputRgb), 0.0);
    DisplayP3ToAp1Out[index] = float4(DisplayP3ToAp1(inputRgb), 0.0);
    Ap1ToDisplayP3Out[index] = float4(Ap1ToDisplayP3(inputRgb), 0.0);
    LuminanceOut[index] = float4(Ap1Luminance(inputRgb), 0.0, 0.0, 0.0);

    SrgbEncodeOut[index] = float4(SrgbEncode(inputRgb), 0.0);
    SrgbDecodeOut[index] = float4(SrgbDecode(signal), 0.0);
    Gamma22EncodeOut[index] = float4(Gamma22Encode(inputRgb), 0.0);
    Gamma22DecodeOut[index] = float4(Gamma22Decode(signal), 0.0);
    PqEncodeOut[index] = float4(PqEncode(inputRgb), 0.0);
    PqDecodeOut[index] = float4(PqDecode(signal), 0.0);
    ScRgbEncodeOut[index] = float4(ScRgbEncode(inputRgb), 0.0);
    ScRgbDecodeOut[index] = float4(ScRgbDecode(inputRgb), 0.0);

    KelvinOut[index] = float4(KelvinToAp1(kelvin), 0.0);
}
