implementing PlanetLab;
__include Buffers;
__include HEALPix;

// Renders the current plate simulation state to a 2D equirectangular RGBA8 storage texture
// for headless FrameCapture PNG dumps, debug layer inspection, and future interactive previews.
//
// RenderMapConstants.Mode:
//   0 = Elevation (bathymetry + topography + subduction trench tint)
//   1 = Plates    (distinct categorical color per plate ID + white overlap highlight)
//   2 = OceanAge  (spreading-ridge age heatmap; continents muted gray)
//   3 = Relief    (uplift relief heatmap in km)
//   4 = Fronts    (subduction red, collision yellow, nearest-front distance gradient)
//   5 = Voronoi   (world & plate Fibonacci cell hash to inspect sampling / stair-stepping)

struct RenderMapParams
{
    uint Width;
    uint Height;
    float Time;
    uint Mode;
};

ConstantBuffer<RenderMapParams> RenderMapConstants;
[format("rgba8")]
RWTexture2D<float4> MapOutput;

static const float Pi = 3.141592653589793f;
static const float TwoPi = 6.283185307179586f;

static const uint RenderModeElevation = 0u;
static const uint RenderModePlates    = 1u;
static const uint RenderModeOceanAge  = 2u;
static const uint RenderModeRelief    = 3u;
static const uint RenderModeFronts    = 4u;
static const uint RenderModeVoronoi   = 5u;

float3 HsvToRgb(float h, float s, float v)
{
    const float3 k = float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f);
    const float3 p = abs(frac(h.xxx + k) * 6.0f - 3.0f);
    return v * lerp(1.0f.xxx, saturate(p - 1.0f), s);
}

float Hash01(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return float(x & 0x00ffffffu) / 16777216.0f;
}

float3 Heatmap(float t)
{
    t = saturate(t);
    const float3 c0 = float3(0.05f, 0.06f, 0.24f);
    const float3 c1 = float3(0.06f, 0.45f, 0.72f);
    const float3 c2 = float3(0.18f, 0.78f, 0.42f);
    const float3 c3 = float3(0.95f, 0.84f, 0.18f);
    const float3 c4 = float3(0.92f, 0.22f, 0.14f);

    if (t < 0.25f) return lerp(c0, c1, t * 4.0f);
    if (t < 0.50f) return lerp(c1, c2, (t - 0.25f) * 4.0f);
    if (t < 0.75f) return lerp(c2, c3, (t - 0.50f) * 4.0f);
    return lerp(c3, c4, (t - 0.75f) * 4.0f);
}

float3 BathymetryTopographyColor(float elevKm, bool isContinental, bool isSubduction)
{
    float3 color;

    if (!isContinental && elevKm < 0.0f)
    {
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

float4 SampleElevationCorner(int pt)
{
    if (pt < 0)
    {
        return float4(-6.0f, 0.0f, 0.0f, 0.0f);
    }

    const OwnersData owners = LoadOwnersIn(uint(pt));
    const int plate = owners.GetBestPlate();
    const int cell = owners.GetBestCell();
    if (plate < 0 || cell < 0)
    {
        return float4(-6.0f, 0.0f, 0.0f, 0.0f);
    }

    const uint flat = uint(plate) * StepParams.PointCount + uint(cell);
    const CrustFlagBits flags = LoadCrustFlagsIn(flat);
    const CrustStateData state = LoadCrustStateIn(flat);
    const FrontState front = LoadFrontStateIn(uint(pt));
    const bool isContinental = flags.IsContinental();
    const uint frontType = front.GetSubduction();
    const bool isSubduction = (frontType == 1u || frontType == 2u);

    float baseElev = 0.5f;
    if (!isContinental)
    {
        const float age = max(RenderMapConstants.Time - state.GetBirth(), 0.0f);
        baseElev = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
    }

    const float reliefKm = state.GetRelief();
    return float4(baseElev + reliefKm, isContinental ? 1.0f : 0.0f, isSubduction ? 1.0f : 0.0f, reliefKm);
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

    const uint mode = RenderMapConstants.Mode;
    if (mode == RenderModeElevation)
    {
        uint baseFace = 0u;
        float2 faceUV = float2(0.0f, 0.0f);
        HealpixVecToFaceUV(worldPoint, StepParams.HealpixOrder, baseFace, faceUV);

        const float2 shifted = faceUV - 0.5f;
        const int2 baseXY = int2(floor(shifted));
        const float2 fracUV = shifted - float2(baseXY);

        const int p00 = HealpixSampleIndex(baseFace, baseXY.x, baseXY.y, StepParams.HealpixOrder);
        const int p10 = HealpixSampleIndex(baseFace, baseXY.x + 1, baseXY.y, StepParams.HealpixOrder);
        const int p01 = HealpixSampleIndex(baseFace, baseXY.x, baseXY.y + 1, StepParams.HealpixOrder);
        const int p11 = HealpixSampleIndex(baseFace, baseXY.x + 1, baseXY.y + 1, StepParams.HealpixOrder);

        float4 s00 = (p00 >= 0) ? SampleElevationCorner(p00) : float4(0.0f);
        float4 s10 = (p10 >= 0) ? SampleElevationCorner(p10) : float4(0.0f);
        float4 s01 = (p01 >= 0) ? SampleElevationCorner(p01) : float4(0.0f);
        float4 s11 = (p11 >= 0) ? SampleElevationCorner(p11) : float4(0.0f);

        // Karlbauer et al. (2024) 24-corner bridge across missing rhombic-dodecahedron diagonal corners
        if (p00 < 0) s00 = 0.5f * (s10 + s01);
        if (p10 < 0) s10 = 0.5f * (s00 + s11);
        if (p01 < 0) s01 = 0.5f * (s00 + s11);
        if (p11 < 0) s11 = 0.5f * (s10 + s01);

        const float4 s = lerp(lerp(s00, s10, fracUV.x), lerp(s01, s11, fracUV.x), fracUV.y);
        const bool isLand = s.y >= 0.5f;
        float3 rgb = BathymetryTopographyColor(s.x, isLand, s.z >= 0.35f);
        if (!isLand && s.y > 0.02f)
        {
            rgb = lerp(rgb, float3(0.24f, 0.62f, 0.72f), saturate(s.y * 1.6f) * 0.55f);
        }

        const float dEdU = lerp(s10.x - s00.x, s11.x - s01.x, fracUV.y);
        const float dEdV = lerp(s01.x - s00.x, s11.x - s10.x, fracUV.x);

        float3 v00 = (p00 >= 0) ? HealpixPixToVec(uint(p00), StepParams.HealpixOrder) : float3(0.0f);
        float3 v10 = (p10 >= 0) ? HealpixPixToVec(uint(p10), StepParams.HealpixOrder) : float3(0.0f);
        float3 v01 = (p01 >= 0) ? HealpixPixToVec(uint(p01), StepParams.HealpixOrder) : float3(0.0f);
        float3 v11 = (p11 >= 0) ? HealpixPixToVec(uint(p11), StepParams.HealpixOrder) : float3(0.0f);
        if (p00 < 0) v00 = normalize(v10 + v01);
        if (p10 < 0) v10 = normalize(v00 + v11);
        if (p01 < 0) v01 = normalize(v00 + v11);
        if (p11 < 0) v11 = normalize(v10 + v01);

        const float3 dPdU = lerp(v10 - v00, v11 - v01, fracUV.y);
        const float3 dPdV = lerp(v01 - v00, v11 - v10, fracUV.x);
        const float3 tU = dPdU - worldPoint * dot(worldPoint, dPdU);
        const float3 tV = dPdV - worldPoint * dot(worldPoint, dPdV);
        const float3 perpA = cross(worldPoint, tU);
        const float3 perpB = cross(worldPoint, tV);
        const float det = dot(tU, perpB);

        float shade = 1.0f;
        if (abs(det) > 1e-12f)
        {
            const float3 gradWorld = (dEdU * perpB - dEdV * perpA) / det;
            const float cellScale = sqrt(4.0f * Pi / float(StepParams.PointCount));
            const float3 sunDir = normalize(float3(0.6f, 0.4f, 0.7f));
            shade = clamp(1.0f - 0.14f * dot(gradWorld * cellScale, sunDir), 0.78f, 1.22f);
        }
        rgb *= shade;

        MapOutput[threadId.xy] = float4(rgb, 1.0f);
        return;
    }

    const int pt = int(HealpixVecToPix(worldPoint, StepParams.HealpixOrder));
    const OwnersData owners = LoadOwnersIn(uint(pt));
    const int plate = owners.GetBestPlate();
    const int cell = owners.GetBestCell();

    float3 rgb = float3(0.02f, 0.04f, 0.10f);
    if (plate >= 0 && cell >= 0)
    {
        const uint flat = uint(plate) * StepParams.PointCount + uint(cell);
        const CrustFlagBits flags = LoadCrustFlagsIn(flat);
        const CrustStateData state = LoadCrustStateIn(flat);
        const FrontState front = LoadFrontStateIn(uint(pt));
        const bool isContinental = flags.IsContinental();
        const uint frontType = front.GetSubduction();
        const bool isSubduction = (frontType == 1u || frontType == 2u);
        const bool isCollisionFront = (frontType == 3u);
        const bool hasOverlap = owners.GetSecondPlate() >= 0;

        if (mode == RenderModePlates)
        {
            const float hue = frac(float(plate) * 0.61803398875f);
            const float sat = isContinental ? 0.55f : 0.80f;
            const float val = isContinental ? 0.90f : 0.55f;
            rgb = HsvToRgb(hue, sat, val);
            if (hasOverlap)
            {
                rgb = lerp(rgb, 1.0f.xxx, 0.65f);
            }
        }
        else if (mode == RenderModeOceanAge)
        {
            if (isContinental)
            {
                rgb = float3(0.18f, 0.20f, 0.22f);
            }
            else
            {
                const float age = max(RenderMapConstants.Time - state.GetBirth(), 0.0f);
                // Young ridge (0 My) bright yellow-red -> old ocean (120+ My) dark blue
                rgb = Heatmap(1.0f - saturate(age / 120.0f));
            }
        }
        else if (mode == RenderModeRelief)
        {
            const float relief = state.GetRelief();
            if (relief <= 0.001f)
            {
                rgb = isContinental ? float3(0.16f, 0.18f, 0.20f) : float3(0.04f, 0.06f, 0.12f);
            }
            else
            {
                rgb = Heatmap(saturate(relief / 6.0f));
            }
        }
        else if (mode == RenderModeFronts)
        {
            const CellState cellFront = LoadCellState(flat);
            rgb = isContinental ? float3(0.24f, 0.26f, 0.24f) : float3(0.08f, 0.10f, 0.14f);
            if (cellFront.GetFrontPoint() >= 0)
            {
                const float distNorm = 1.0f - saturate(cellFront.GetDistance() / 1800.0f);
                rgb = lerp(rgb, float3(0.10f, 0.78f, 0.92f), distNorm * 0.75f);
            }
            if (isCollisionFront)
            {
                rgb = float3(0.96f, 0.85f, 0.15f);
            }
            else if (isSubduction)
            {
                rgb = float3(0.95f, 0.15f, 0.15f);
            }
            else if (hasOverlap)
            {
                rgb = float3(0.96f, 0.85f, 0.15f);
            }
        }
        else if (mode == RenderModeVoronoi)
        {
            const float worldShade = 0.35f + 0.65f * Hash01(uint(pt));
            const float plateHue = frac(float(plate) * 0.61803398875f);
            const float cellShade = 0.75f + 0.25f * Hash01(uint(cell) ^ (uint(plate) * 2654435761u));
            rgb = HsvToRgb(plateHue, isContinental ? 0.35f : 0.75f, worldShade * cellShade);
        }
    }

    MapOutput[threadId.xy] = float4(rgb, 1.0f);
}
