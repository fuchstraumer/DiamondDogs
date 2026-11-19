implementing PlanetLab;
__include Buffers;
__include Fibonacci;

// Renders the current plate simulation state to a 2D equirectangular RGBA8 storage texture
// for headless FrameCapture PNG dumps and future interactive previews.

struct RenderMapParams
{
    uint Width;
    uint Height;
    float Time;
};

ConstantBuffer<RenderMapParams> RenderMapConstants;
RWTexture2D<float4> MapOutput;

static const float Pi = 3.141592653589793f;
static const float TwoPi = 6.283185307179586f;

float3 BathymetryTopographyColor(float elevKm, bool isContinental, bool isSubduction)
{
    float3 color;

    if (!isContinental && elevKm < 0.0f)
    {
        // Deep abyssal trench (-6 km) -> mid-ocean ridge (-2.6 km) -> shallow arc/shelf (0 km)
        const float3 abyssal = float3(0.03f, 0.08f, 0.22f);
        const float3 ocean   = float3(0.06f, 0.20f, 0.44f);
        const float3 ridge   = float3(0.12f, 0.42f, 0.62f);
        const float3 shelf   = float3(0.28f, 0.66f, 0.74f);

        if (elevKm < -4.2f)
        {
            color = lerp(abyssal, ocean, saturate((elevKm + 6.0f) / 1.8f));
        }
        else if (elevKm < -2.4f)
        {
            color = lerp(ocean, ridge, saturate((elevKm + 4.2f) / 1.8f));
        }
        else
        {
            color = lerp(ridge, shelf, saturate((elevKm + 2.4f) / 2.4f));
        }
    }
    else
    {
        // Coastal lowland (0 km) -> plains (0.8 km) -> highlands (2.5 km) -> alpine (5 km) -> snow (7.5+ km)
        const float3 lowland  = float3(0.22f, 0.48f, 0.24f);
        const float3 plains   = float3(0.46f, 0.58f, 0.28f);
        const float3 highland = float3(0.68f, 0.56f, 0.34f);
        const float3 alpine   = float3(0.48f, 0.38f, 0.32f);
        const float3 snow     = float3(0.94f, 0.96f, 0.98f);

        const float h = max(elevKm, 0.0f);
        if (h < 0.8f)
        {
            color = lerp(lowland, plains, saturate(h / 0.8f));
        }
        else if (h < 2.2f)
        {
            color = lerp(plains, highland, saturate((h - 0.8f) / 1.4f));
        }
        else if (h < 4.8f)
        {
            color = lerp(highland, alpine, saturate((h - 2.2f) / 2.6f));
        }
        else
        {
            color = lerp(alpine, snow, saturate((h - 4.8f) / 2.8f));
        }
    }

    if (isSubduction)
    {
        color = lerp(color, float3(0.02f, 0.04f, 0.12f), 0.55f);
    }

    return color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RenderMapCS(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= RenderMapConstants.Width || threadId.y >= RenderMapConstants.Height)
    {
        return;
    }

    const float u = (float(threadId.x) + 0.5f) / float(RenderMapConstants.Width);
    const float v = (float(threadId.y) + 0.5f) / float(RenderMapConstants.Height);
    const float lon = (u - 0.5f) * TwoPi;
    const float lat = (0.5f - v) * Pi;

    float sinLat, cosLat, sinLon, cosLon;
    sincos(lat, sinLat, cosLat);
    sincos(lon, sinLon, cosLon);
    const float3 worldPoint = float3(cosLat * cosLon, cosLat * sinLon, sinLat);

    const int pt = FibonacciNearest(worldPoint, int(Params.PointCount));
    const OwnersData owners = LoadOwnersIn(uint(pt));
    const int plate = owners.GetBestPlate();
    const int cell = owners.GetBestCell();

    float3 rgb = float3(0.02f, 0.04f, 0.10f);
    if (plate >= 0 && cell >= 0)
    {
        const uint flat = uint(plate) * Params.PointCount + uint(cell);
        const CrustFlagBits flags = LoadCrustFlagsIn(flat);
        const CrustStateData state = LoadCrustStateIn(flat);
        const FrontState front = LoadFrontStateIn(uint(pt));

        float baseElev = 0.5f;
        if (!flags.IsContinental())
        {
            const float age = max(RenderMapConstants.Time - state.GetBirth(), 0.0f);
            baseElev = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
        }

        const float elevKm = baseElev + state.GetRelief();
        rgb = BathymetryTopographyColor(elevKm, flags.IsContinental(), front.GetSubduction() == 1u);
    }

    MapOutput[threadId.xy] = float4(rgb, 1.0f);
}
