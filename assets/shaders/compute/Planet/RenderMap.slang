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
//   6 = Plumes    (mantle hotspot conduits, thermal swell, and volcanic island/seamount track age)
//   7 = Atlas     (for people: one continuous elevation ramp, hillshade, coastline, contours, scale bar)
//   8 = Raw       (for scripts: elevation in R (high byte) and G (low byte), km = 22 * (R*256+G)/65535 - 11)

// RenderMapConstants.Projection:
//   0 = Equirectangular (default)
//   1 = EqualEarth      (Savric et al. 2018 equal-area world map)
//   2 = Mollweide       (elliptical equal-area world map)
//   3 = Orthographic    (globe view centered on CenterLonRad, CenterLatRad)
//   4 = PolarNorth      (Lambert azimuthal equal-area northern hemisphere)
//   5 = PolarSouth      (Lambert azimuthal equal-area southern hemisphere)

struct RenderMapParams
{
    uint Width;
    uint Height;
    float Time;
    uint Mode;

    uint Projection;
    float CenterLonRad;
    float CenterLatRad;
    float Pad0;

    float CropMinU;
    float CropMinV;
    float CropMaxU;
    float CropMaxV;
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
static const uint RenderModePlumes    = 6u;
static const uint RenderModeAtlas     = 7u;
static const uint RenderModeRaw       = 8u;

static const uint RenderProjectionEquirectangular = 0u;
static const uint RenderProjectionEqualEarth      = 1u;
static const uint RenderProjectionMollweide       = 2u;
static const uint RenderProjectionOrthographic    = 3u;
static const uint RenderProjectionPolarNorth      = 4u;
static const uint RenderProjectionPolarSouth      = 5u;

internal float WrapLongitude(in float lon)
{
    return lon - TwoPi * floor((lon + Pi) / TwoPi);
}

internal bool ProjectUvToSphere(in float u,
                                in float v,
                                out float3 worldPoint,
                                out float lat,
                                out float lon)
{
    worldPoint = float3(1.0f, 0.0f, 0.0f);
    lat = 0.0f;
    lon = 0.0f;

    const uint projection = RenderMapConstants.Projection;

    if (projection == RenderProjectionEquirectangular)
    {
        lon = WrapLongitude(RenderMapConstants.CenterLonRad + (u - 0.5f) * TwoPi);
        lat = (0.5f - v) * Pi;
        float sinLat;
        float cosLat;
        float sinLon;
        float cosLon;
        sincos(lat, sinLat, cosLat);
        sincos(lon, sinLon, cosLon);
        worldPoint = float3(cosLat * cosLon, cosLat * sinLon, sinLat);
        return true;
    }

    const float mapAspect = float(RenderMapConstants.Width) / max(float(RenderMapConstants.Height), 1.0f);

    if (projection == RenderProjectionEqualEarth)
    {
        // Savric et al. (2018) Equal Earth equal-area pseudocylindrical projection.
        const float a1 = 1.340264f;
        const float a2 = -0.081106f;
        const float a3 = 0.000893f;
        const float a4 = 0.003796f;
        const float mFactor = 0.8660254037844386f;
        const float maxTheta = Pi / 3.0f;
        const float maxTheta2 = maxTheta * maxTheta;
        const float maxTheta6 = maxTheta2 * maxTheta2 * maxTheta2;
        const float yMax = maxTheta * (a1 + a2 * maxTheta2 + maxTheta6 * (a3 + a4 * maxTheta2));
        const float xMax = (4.0f * mFactor * Pi) / (3.0f * a1);
        const float projAspect = xMax / yMax;

        float nx = (u - 0.5f) * 2.0f;
        float ny = (0.5f - v) * 2.0f;

        if (mapAspect > projAspect)
        {
            nx *= mapAspect / projAspect;
        }
        else
        {
            ny *= projAspect / mapAspect;
        }

        const float x = nx * xMax;
        const float y = ny * yMax;

        if (abs(y) > yMax)
        {
            return false;
        }

        float theta = clamp(y / a1, -maxTheta, maxTheta);

        [unroll]
        for (uint iter = 0u; iter < 6u; ++iter)
        {
            const float t2 = theta * theta;
            const float t6 = t2 * t2 * t2;
            const float fVal = theta * (a1 + a2 * t2 + t6 * (a3 + a4 * t2)) - y;
            const float dfVal = a1 + 3.0f * a2 * t2 + t6 * (7.0f * a3 + 9.0f * a4 * t2);
            theta = clamp(theta - fVal / dfVal, -maxTheta, maxTheta);
        }

        const float t2 = theta * theta;
        const float t6 = t2 * t2 * t2;
        const float dfVal = a1 + 3.0f * a2 * t2 + t6 * (7.0f * a3 + 9.0f * a4 * t2);
        const float cosTheta = cos(theta);
        const float deltaLon = (3.0f * x * dfVal) / (4.0f * mFactor * max(cosTheta, 1.0e-6f));

        if (abs(deltaLon) > Pi)
        {
            return false;
        }

        const float sinLat = clamp(sin(theta) / mFactor, -1.0f, 1.0f);
        lat = asin(sinLat);
        lon = WrapLongitude(RenderMapConstants.CenterLonRad + deltaLon);
        float sinLon;
        float cosLon;
        sincos(lon, sinLon, cosLon);
        const float cosLat = sqrt(max(1.0f - sinLat * sinLat, 0.0f));
        worldPoint = float3(cosLat * cosLon, cosLat * sinLon, sinLat);
        return true;
    }

    if (projection == RenderProjectionMollweide)
    {
        float nx = (u - 0.5f) * 2.0f;
        float ny = (0.5f - v) * 2.0f;

        if (mapAspect > 2.0f)
        {
            nx *= mapAspect * 0.5f;
        }
        else
        {
            ny *= 2.0f / mapAspect;
        }

        if (nx * nx + ny * ny > 1.0f)
        {
            return false;
        }

        const float theta = asin(clamp(ny, -1.0f, 1.0f));
        const float cosTheta = cos(theta);
        const float deltaLon = Pi * nx / max(cosTheta, 1.0e-6f);

        if (abs(deltaLon) > Pi)
        {
            return false;
        }

        const float sinLat = clamp((2.0f * theta + sin(2.0f * theta)) / Pi, -1.0f, 1.0f);
        lat = asin(sinLat);
        lon = WrapLongitude(RenderMapConstants.CenterLonRad + deltaLon);
        float sinLon;
        float cosLon;
        sincos(lon, sinLon, cosLon);
        const float cosLat = sqrt(max(1.0f - sinLat * sinLat, 0.0f));
        worldPoint = float3(cosLat * cosLon, cosLat * sinLon, sinLat);
        return true;
    }

    float px = (u - 0.5f) * 2.0f;
    float py = (0.5f - v) * 2.0f;

    if (mapAspect > 1.0f)
    {
        px *= mapAspect;
    }
    else
    {
        py /= mapAspect;
    }

    const float r2 = px * px + py * py;

    if (r2 > 1.0f)
    {
        return false;
    }

    if (projection == RenderProjectionOrthographic)
    {
        const float pz = sqrt(max(1.0f - r2, 0.0f));
        float sinCLat;
        float cosCLat;
        float sinCLon;
        float cosCLon;
        sincos(RenderMapConstants.CenterLatRad, sinCLat, cosCLat);
        sincos(RenderMapConstants.CenterLonRad, sinCLon, cosCLon);
        const float3 forward = float3(cosCLat * cosCLon, cosCLat * sinCLon, sinCLat);
        const float3 refUp = (abs(forward.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
        const float3 east = normalize(cross(refUp, forward));
        const float3 north = cross(forward, east);
        worldPoint = normalize(forward * pz + east * px + north * py);
        lat = asin(clamp(worldPoint.z, -1.0f, 1.0f));
        lon = atan2(worldPoint.y, worldPoint.x);
        return true;
    }

    // Lambert azimuthal equal-area hemisphere: zAbs = 1 - r^2 gives uniform area from pole (r=0) to equator (r=1).
    const float zAbs = 1.0f - r2;
    const float sinColat = sqrt(max(1.0f - zAbs * zAbs, 0.0f));
    const float phi = atan2(py, px) + RenderMapConstants.CenterLonRad;
    float sinPhi;
    float cosPhi;
    sincos(phi, sinPhi, cosPhi);

    if (projection == RenderProjectionPolarNorth)
    {
        worldPoint = float3(sinColat * cosPhi, sinColat * sinPhi, zAbs);
    }
    else
    {
        worldPoint = float3(sinColat * cosPhi, -sinColat * sinPhi, -zAbs);
    }

    lat = asin(clamp(worldPoint.z, -1.0f, 1.0f));
    lon = atan2(worldPoint.y, worldPoint.x);
    return true;
}

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

float3 BathymetryTopographyColor(float elevKm, bool isContinental, bool isSubduction, float reliefKm)
{
    float3 color;

    if (!isContinental && reliefKm > 0.45f)
    {
        const float3 deepOcean = float3(0.04f, 0.12f, 0.32f);
        const float3 seamount = float3(0.14f, 0.42f, 0.64f);
        const float3 atollReef = float3(0.24f, 0.86f, 0.80f);
        const float3 islandGreen = float3(0.18f, 0.44f, 0.22f);
        const float3 basaltPeak = float3(0.40f, 0.36f, 0.28f);

        if (elevKm < -0.55f)
        {
            color = lerp(deepOcean, seamount, saturate((reliefKm - 0.45f) / 2.2f));
        }
        else if (elevKm <= 0.05f)
        {
            const float reefMask = saturate(1.0f - abs(elevKm + 0.20f) / 0.35f);
            color = lerp(seamount, atollReef, max(reefMask, saturate((elevKm + 0.55f) * 1.4f)));
        }
        else if (elevKm <= 1.6f)
        {
            color = lerp(islandGreen, basaltPeak, saturate((elevKm - 0.05f) / 1.55f));
        }
        else
        {
            color = lerp(basaltPeak, float3(0.56f, 0.48f, 0.38f), saturate((elevKm - 1.6f) / 2.2f));
        }
    }
    else if (!isContinental && elevKm < 0.0f)
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

    const float baseElev = BaseCrustElevationKm(isContinental, state.GetBirth(), RenderMapConstants.Time);
    const float reliefKm = state.GetRelief();
    const float3 cellWorldPos = HealpixPixToVec(uint(pt), StepParams.HealpixOrder);
    const float swellKm = EvaluatePlumeSwellKm(cellWorldPos, RenderMapConstants.Time);
    return float4(baseElev + reliefKm + swellKm, isContinental ? 1.0f : 0.0f, isSubduction ? 1.0f : 0.0f, reliefKm);
}

internal float4 SampleBilinearElevationAtPoint(in float3 dir, out float dEdU, out float dEdV, out float3 tU, out float3 tV)
{
    uint baseFace = 0u;
    float2 faceUV = float2(0.0f, 0.0f);
    HealpixVecToFaceUV(dir, StepParams.HealpixOrder, baseFace, faceUV);

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

    dEdU = lerp(s10.x - s00.x, s11.x - s01.x, fracUV.y);
    dEdV = lerp(s01.x - s00.x, s11.x - s10.x, fracUV.x);

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
    tU = dPdU - dir * dot(dir, dPdU);
    tV = dPdV - dir * dot(dir, dPdV);

    return lerp(lerp(s00, s10, fracUV.x), lerp(s01, s11, fracUV.x), fracUV.y);
}

internal float3 WarpSphereDirection(in float3 unitDir, in float amplitude, in float frequency, in uint seed)
{
    if (amplitude <= 0.0f)
    {
        return unitDir;
    }

    Fbm fbmRule;
    fbmRule.gain = 0.5f;

    Fractal<Simplex3, Fbm> warpField;
    warpField.rule = fbmRule;
    warpField.frequency = frequency * 0.65f;
    warpField.lacunarity = 2.05f;
    warpField.octaveCount = 3;
    warpField.rotateOctaves = 1;

    warpField.seed = seed + 17u;
    const float wx = warpField.eval(unitDir);
    warpField.seed = seed + 53u;
    const float wy = warpField.eval(unitDir);
    warpField.seed = seed + 109u;
    const float wz = warpField.eval(unitDir);

    const float3 rawWarp = float3(wx, wy, wz);
    const float3 tangentWarp = rawWarp - unitDir * dot(unitDir, rawWarp);
    return normalize(unitDir + amplitude * tangentWarp);
}

internal float EvaluateSculptedDetailKm(in float3 warpedDir, in float baseElevKm, in float reliefKm)
{
    const bool coastalEnabled = (ShapingConstants.Flags & ShapingFlagCoastalNoiseEnabled) != 0u;
    const bool atollEnabled = (ShapingConstants.Flags & ShapingFlagAtollShapingEnabled) != 0u;
    if (!coastalEnabled && !atollEnabled)
    {
        return 0.0f;
    }

    if (baseElevKm < -3.2f && reliefKm < 0.25f)
    {
        return 0.0f;
    }

    float deltaKm = 0.0f;
    const float freq = max(ShapingConstants.NoiseFrequency, 0.5f);
    const uint seed = ShapingConstants.NoiseSeed;

    float swissVal = 0.0f;
    if (coastalEnabled && ShapingConstants.CoastalDetailKm > 0.0f)
    {
        Swiss swissRule;
        swissRule.gain = 0.52f;
        swissRule.warpStrength = 0.14f;
        swissRule.compoundingWarp = 0;
        swissRule.warpFalloff = 1;
        swissRule.smoothClamp = 1;

        Fractal<Simplex3, Swiss> swissField;
        swissField.rule = swissRule;
        swissField.frequency = freq;
        swissField.lacunarity = 2.05f;
        swissField.octaveCount = 5;
        swissField.seed = seed;
        swissField.rotateOctaves = 1;
        swissVal = swissField.eval(warpedDir);

        // Coastal fjords and fractal inlets peak near sea level; attenuate in deep ocean.
        const float coastProximity = exp(-0.30f * baseElevKm * baseElevKm);
        const float shelfWeight = max(coastProximity, smoothstep(-1.6f, 0.4f, baseElevKm) * 0.40f);
        deltaKm += ShapingConstants.CoastalDetailKm * shelfWeight * (swissVal - 0.12f);

        if (reliefKm > 0.05f || baseElevKm > -0.25f)
        {
            RidgedMulti ridgeRule;
            ridgeRule.offset = 1.0f;
            ridgeRule.gain = 2.0f;
            ridgeRule.spectralExponent = 0.9f;

            Fractal<Perlin3, RidgedMulti> ridgeField;
            ridgeField.rule = ridgeRule;
            ridgeField.frequency = freq * 1.45f;
            ridgeField.lacunarity = 2.1f;
            ridgeField.octaveCount = 4;
            ridgeField.seed = seed + 97u;
            ridgeField.rotateOctaves = 1;
            const float ridgeVal = ridgeField.eval(warpedDir);
            const float alpineWeight = saturate(reliefKm / 2.2f);
            deltaKm += ShapingConstants.CoastalDetailKm * (0.28f + 0.85f * alpineWeight) * max(ridgeVal, -0.30f);
        }
    }

    if (atollEnabled && ShapingConstants.AtollLagoonStrength > 0.0f &&
        baseElevKm > -0.85f && reliefKm < 2.2f)
    {
        Cellular3 cellField;
        cellField.frequency = freq * 2.35f;
        cellField.jitter = 0.85f;
        cellField.density = 1.4f;
        cellField.metric = k_CellularEuclidean;
        cellField.minkowskiExponent = 2.0f;
        cellField.output = k_CellularF1;
        cellField.seed = seed + 313u;

        // Warp cellular distance with Swiss fractal noise so lagoons form jagged embayments, not circular polka-dots.
        const float f1 = cellField.eval(warpedDir) + 0.14f * swissVal;
        const float mountainMask = 1.0f - smoothstep(0.65f, 1.8f, reliefKm);
        const float landMask = smoothstep(-0.65f, 0.35f, baseElevKm);
        const float lagoonMask = smoothstep(0.28f, 0.08f, f1) * saturate(0.55f - 0.65f * swissVal);
        const float rimWave = exp(-pow((f1 - 0.44f) / 0.13f, 2.0f));
        const float gapCarve = saturate(0.45f + 0.55f * swissVal);

        deltaKm += ShapingConstants.AtollLagoonStrength * mountainMask * landMask *
                   (-1.45f * lagoonMask + 0.40f * rimWave * gapCarve);
    }

    return deltaKm;
}

internal float3 SculptedAtollSurfaceColor(in float elevKm, in bool isContinental, in bool isSubduction, in float reliefKm)
{
    if ((ShapingConstants.Flags & ShapingFlagAtollShapingEnabled) == 0u)
    {
        return BathymetryTopographyColor(elevKm, isContinental, isSubduction, reliefKm);
    }

    float3 color;
    if (!isContinental && reliefKm > 0.45f)
    {
        return BathymetryTopographyColor(elevKm, isContinental, isSubduction, reliefKm);
    }
    else if (elevKm < 0.0f)
    {
        const float3 abyssal = float3(0.01f, 0.04f, 0.14f);
        const float3 deepBlue = float3(0.02f, 0.10f, 0.28f);
        const float3 shelf = float3(0.04f, 0.28f, 0.48f);
        const float3 lagoon = float3(0.10f, 0.52f, 0.64f);

        if (elevKm < -3.6f)
        {
            color = lerp(abyssal, deepBlue, saturate((elevKm + 6.0f) / 2.4f));
        }
        else if (elevKm < -1.0f)
        {
            color = lerp(deepBlue, shelf, saturate((elevKm + 3.6f) / 2.6f));
        }
        else
        {
            color = lerp(shelf, lagoon, saturate((elevKm + 1.0f) / 1.0f));
        }
    }
    else
    {
        const float3 littoral = float3(0.24f, 0.38f, 0.14f);
        const float3 lushForest = float3(0.14f, 0.34f, 0.12f);
        const float3 uplandOlive = float3(0.32f, 0.42f, 0.16f);
        const float3 highland = float3(0.56f, 0.48f, 0.28f);
        const float3 alpine = float3(0.52f, 0.44f, 0.38f);
        const float3 snow = float3(0.92f, 0.94f, 0.96f);

        if (elevKm < 0.35f)
        {
            color = lerp(littoral, lushForest, saturate(elevKm / 0.35f));
        }
        else if (elevKm < 1.3f)
        {
            color = lerp(lushForest, uplandOlive, saturate((elevKm - 0.35f) / 0.95f));
        }
        else if (elevKm < 2.8f)
        {
            color = lerp(uplandOlive, highland, saturate((elevKm - 1.3f) / 1.5f));
        }
        else if (elevKm < 5.0f)
        {
            color = lerp(highland, alpine, saturate((elevKm - 2.8f) / 2.2f));
        }
        else
        {
            color = lerp(alpine, snow, saturate((elevKm - 5.0f) / 2.5f));
        }
    }

    if (isSubduction)
    {
        color = lerp(color, float3(0.01f, 0.02f, 0.09f), 0.55f);
    }

    return color;
}

// ---- Atlas view (mode 7): a map for people. Modes 0 to 6 keep their colours, because the diagnostics scripts read them.
// The ramp is continuous in elevation (km): no band snaps, and the same colour always means the same height.
static const float AtlasLowestKm = -6.5f;
static const float AtlasHighestKm = 8.0f;
static const float AtlasLandExaggeration = 25.0f;
static const float AtlasSeaExaggeration = 8.0f;
static const float AtlasPlanetRadiusKm = 6370.0f;

internal float3 AtlasRamp(in float elevKm)
{
    if (elevKm < 0.0f)
    {
        // deep navy at the abyss, to pale cyan on the shelf
        const float3 abyss = float3(0.03f, 0.07f, 0.20f);
        const float3 basin = float3(0.06f, 0.17f, 0.40f);
        const float3 slope = float3(0.12f, 0.33f, 0.58f);
        const float3 shelf = float3(0.36f, 0.64f, 0.78f);

        if (elevKm < -4.5f)
        {
            return lerp(abyss, basin, saturate((elevKm - AtlasLowestKm) / 2.0f));
        }

        if (elevKm < -1.5f)
        {
            return lerp(basin, slope, (elevKm + 4.5f) / 3.0f);
        }

        return lerp(slope, shelf, (elevKm + 1.5f) / 1.5f);
    }

    // green lowland, tan upland, brown mountains, grey rock, white above about 6 km
    const float3 lowland = float3(0.30f, 0.50f, 0.27f);
    const float3 plain = float3(0.55f, 0.62f, 0.36f);
    const float3 upland = float3(0.76f, 0.68f, 0.46f);
    const float3 mountain = float3(0.60f, 0.45f, 0.33f);
    const float3 rock = float3(0.66f, 0.63f, 0.61f);
    const float3 snow = float3(0.97f, 0.97f, 0.98f);

    if (elevKm < 0.6f)
    {
        return lerp(lowland, plain, elevKm / 0.6f);
    }

    if (elevKm < 1.8f)
    {
        return lerp(plain, upland, (elevKm - 0.6f) / 1.2f);
    }

    if (elevKm < 3.5f)
    {
        return lerp(upland, mountain, (elevKm - 1.8f) / 1.7f);
    }

    if (elevKm < 5.5f)
    {
        return lerp(mountain, rock, (elevKm - 3.5f) / 2.0f);
    }

    return lerp(rock, snow, saturate((elevKm - 5.5f) / 1.5f));
}

// coverage of a line at each multiple of `spacing`, for a field that changes by `perPixel` units in one pixel
internal float AtlasLineCoverage(in float value, in float spacing, in float perPixel, in float halfWidthPixels)
{
    const float offset = abs(frac(value / spacing + 0.5f) - 0.5f) * spacing;
    return 1.0f - smoothstep(halfWidthPixels, halfWidthPixels + 1.0f, offset / max(perPixel, 1.0e-6f));
}

// the scale bar: AtlasLowestKm .. AtlasHighestKm left to right, a tick every km, a tall white tick at sea level
internal bool AtlasScaleBar(in uint2 pixel, out float3 rgb)
{
    rgb = float3(0.0f, 0.0f, 0.0f);
    const uint barWidth = min(320u, RenderMapConstants.Width / 3u);
    const uint left = 12u;
    const uint bottom = RenderMapConstants.Height - 12u;
    const uint barTop = bottom - 10u;
    const uint tickTop = barTop - 6u;

    if (barWidth < 64u || pixel.x + 2u < left || pixel.x >= left + barWidth + 2u || pixel.y + 2u < tickTop || pixel.y >= bottom + 2u)
    {
        return false;
    }

    // a dark frame around the bar and its ticks
    rgb = float3(0.06f, 0.07f, 0.08f);

    if (pixel.x < left || pixel.x >= left + barWidth || pixel.y < tickTop || pixel.y >= bottom)
    {
        return true;
    }

    const float kmPerPixel = (AtlasHighestKm - AtlasLowestKm) / float(barWidth);
    const float elevKm = AtlasLowestKm + (float(pixel.x - left) + 0.5f) * kmPerPixel;

    if (pixel.y >= barTop)
    {
        rgb = AtlasRamp(elevKm);
        return true;
    }

    const float nearestKm = round(elevKm);
    const bool onTick = abs(elevKm - nearestKm) < 0.5f * kmPerPixel;

    if (onTick && nearestKm == 0.0f)
    {
        rgb = float3(1.0f, 1.0f, 1.0f);
    }
    else if (onTick && pixel.y >= tickTop + 3u)
    {
        rgb = float3(0.75f, 0.75f, 0.75f);
    }

    return true;
}

internal float3 RenderAtlas(in uint2 pixel, in float3 worldPoint, in float lat, in float lon)
{
    float3 scaleRgb;

    if (AtlasScaleBar(pixel, scaleRgb))
    {
        return scaleRgb;
    }

    float dEdU, dEdV;
    float3 tU, tV;
    const float4 s = SampleBilinearElevationAtPoint(worldPoint, dEdU, dEdV, tU, tV);
    const float elevKm = s.x;
    const bool isLand = elevKm >= 0.0f;

    // the gradient in km for each radian of arc, in the world frame
    float3 gradWorld = float3(0.0f, 0.0f, 0.0f);
    const float3 perpA = cross(worldPoint, tU);
    const float3 perpB = cross(worldPoint, tV);
    const float det = dot(tU, perpB);

    if (abs(det) > 1e-12f)
    {
        gradWorld = (dEdU * perpB - dEdV * perpA) / det;
    }

    // hillshade: the cartographic convention, sun from the north-west, 45 degrees high
    const float3 refUp = (abs(worldPoint.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 east = normalize(cross(refUp, worldPoint));
    const float3 north = cross(worldPoint, east);
    const float3 sun = normalize(-0.5f * east + 0.5f * north + 0.7071f * worldPoint);
    const float exaggeration = isLand ? AtlasLandExaggeration : AtlasSeaExaggeration;
    const float3 normal = normalize(worldPoint - exaggeration * gradWorld / AtlasPlanetRadiusKm);
    const float shade = clamp(dot(normal, sun) / dot(worldPoint, sun), 0.55f, 1.3f);
    float3 rgb = AtlasRamp(elevKm) * lerp(1.0f, shade, isLand ? 1.0f : 0.6f);

    // the vertical size of one pixel, in radians of arc
    const float pixelArc = Pi / float(RenderMapConstants.Height);
    const float kmPerPixel = length(gradWorld) * pixelArc;

    // faint contours every km, then the coastline over them
    const float contour = AtlasLineCoverage(elevKm, 1.0f, kmPerPixel, 0.35f) * (abs(elevKm) > 0.5f ? 1.0f : 0.0f);
    rgb = lerp(rgb, rgb * 0.72f, contour * (isLand ? 0.55f : 0.35f));
    const float coast = 1.0f - smoothstep(0.6f, 1.6f, abs(elevKm) / max(kmPerPixel, 1.0e-6f));
    rgb = lerp(rgb, float3(0.08f, 0.09f, 0.10f), coast * 0.85f);

    // a faint graticule every 30 degrees, the equator a little stronger
    const float degreesPerPixel = 180.0f / float(RenderMapConstants.Height);
    const float latDeg = lat * (180.0f / Pi);
    const float lonDeg = lon * (180.0f / Pi);
    const float latLine = AtlasLineCoverage(latDeg, 30.0f, degreesPerPixel, 0.25f);
    const float lonLine = AtlasLineCoverage(lonDeg, 30.0f, degreesPerPixel, 0.25f);
    const float equator = AtlasLineCoverage(latDeg, 180.0f, degreesPerPixel, 0.25f);
    rgb = lerp(rgb, float3(1.0f, 1.0f, 1.0f), max(max(latLine, lonLine) * 0.12f, equator * 0.25f));

    return rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RenderMapCS(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= RenderMapConstants.Width || threadId.y >= RenderMapConstants.Height)
    {
        return;
    }

    const uint mode = RenderMapConstants.Mode;

    if (mode == RenderModeAtlas)
    {
        float3 scaleRgb;

        if (AtlasScaleBar(threadId.xy, scaleRgb))
        {
            MapOutput[threadId.xy] = float4(scaleRgb, 1.0f);
            return;
        }
    }

    const bool hasCrop = (RenderMapConstants.CropMaxU > RenderMapConstants.CropMinU) &&
                         (RenderMapConstants.CropMaxV > RenderMapConstants.CropMinV);
    const float minU = hasCrop ? RenderMapConstants.CropMinU : 0.0f;
    const float minV = hasCrop ? RenderMapConstants.CropMinV : 0.0f;
    const float maxU = hasCrop ? RenderMapConstants.CropMaxU : 1.0f;
    const float maxV = hasCrop ? RenderMapConstants.CropMaxV : 1.0f;

    const float rawU = (float(threadId.x) + 0.5f) / float(RenderMapConstants.Width);
    const float rawV = (float(threadId.y) + 0.5f) / float(RenderMapConstants.Height);
    const float u = lerp(minU, maxU, rawU);
    const float v = lerp(minV, maxV, rawV);

    float3 worldPoint;
    float lat;
    float lon;

    if (!ProjectUvToSphere(u, v, worldPoint, lat, lon))
    {
        if (mode == RenderModeRaw)
        {
            MapOutput[threadId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        else
        {
            MapOutput[threadId.xy] = float4(0.02f, 0.03f, 0.05f, 1.0f);
        }

        return;
    }
    if (mode == RenderModeRaw)
    {
        float dEdU, dEdV;
        float3 tU, tV;
        const float elevKm = SampleBilinearElevationAtPoint(worldPoint, dEdU, dEdV, tU, tV).x;
        const uint code = uint(round(saturate((elevKm + 11.0f) / 22.0f) * 65535.0f));
        MapOutput[threadId.xy] = float4(float(code >> 8u) / 255.0f, float(code & 0xffu) / 255.0f, 0.0f, 1.0f);
        return;
    }

    if (mode == RenderModeAtlas)
    {
        MapOutput[threadId.xy] = float4(RenderAtlas(threadId.xy, worldPoint, lat, lon), 1.0f);
        return;
    }

    if (mode == RenderModeElevation)
    {
        const bool shapingNoiseActive =
            (ShapingConstants.Flags & (ShapingFlagCoastalNoiseEnabled | ShapingFlagAtollShapingEnabled)) != 0u;

        if (!shapingNoiseActive && ShapingConstants.TrenchDepthKm <= 0.0f)
        {
            float dEdU, dEdV;
            float3 tU, tV;
            const float4 s = SampleBilinearElevationAtPoint(worldPoint, dEdU, dEdV, tU, tV);
            const bool isLand = s.y >= 0.5f;
            float3 rgb = BathymetryTopographyColor(s.x, isLand, s.z >= 0.35f, s.w);
            if (!isLand && s.y > 0.02f)
            {
                rgb = lerp(rgb, float3(0.24f, 0.62f, 0.72f), saturate(s.y * 1.6f) * 0.55f);
            }

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

        const float3 warpedPoint = shapingNoiseActive
            ? WarpSphereDirection(worldPoint, ShapingConstants.CoastalWarpAmplitude,
                                  ShapingConstants.NoiseFrequency, ShapingConstants.NoiseSeed)
            : worldPoint;

        float dEdU, dEdV;
        float3 tU, tV;
        const float4 s = SampleBilinearElevationAtPoint(warpedPoint, dEdU, dEdV, tU, tV);
        const bool isSubduction = s.z >= 0.35f;
        const float trenchDepress = ShapingConstants.TrenchDepthKm * saturate((s.z - 0.25f) / 0.75f);
        const float rawElev = s.x + ShapingConstants.AtollLandBias - trenchDepress;
        const float detailDelta = EvaluateSculptedDetailKm(warpedPoint, rawElev, s.w);
        const float sculptedElev = rawElev + detailDelta;
        const bool isContCrust = s.y >= 0.5f;
        const bool isEmergentLand = isContCrust ? (sculptedElev >= 0.0f) : (sculptedElev >= 0.0f && s.w <= 0.45f);

        float3 rgb = SculptedAtollSurfaceColor(sculptedElev, isContCrust && (sculptedElev >= 0.0f || s.w > 0.45f),
                                               isSubduction, s.w);
        if (!isContCrust && s.y > 0.02f && sculptedElev < 0.0f)
        {
            rgb = lerp(rgb, float3(0.14f, 0.50f, 0.62f), saturate(s.y * 1.4f) * 0.45f);
        }

        // Combine HEALPix tectonic slope with a tangent-frame finite difference on the noise field.
        float3 gradWorld = float3(0.0f, 0.0f, 0.0f);
        const float3 perpA = cross(warpedPoint, tU);
        const float3 perpB = cross(warpedPoint, tV);
        const float det = dot(tU, perpB);
        if (abs(det) > 1e-12f)
        {
            gradWorld = (dEdU * perpB - dEdV * perpA) / det;
        }

        const float cellScale = sqrt(4.0f * Pi / float(StepParams.PointCount));
        if (shapingNoiseActive && (rawElev > -2.8f || s.w > 0.25f))
        {
            const float3 refUp = (abs(warpedPoint.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
            const float3 eastAxis = normalize(cross(refUp, warpedPoint));
            const float3 northAxis = cross(warpedPoint, eastAxis);
            const float eps = max(0.35f * cellScale, 0.002f);
            const float detailE = EvaluateSculptedDetailKm(normalize(warpedPoint + eastAxis * eps), rawElev, s.w);
            const float detailN = EvaluateSculptedDetailKm(normalize(warpedPoint + northAxis * eps), rawElev, s.w);
            gradWorld += eastAxis * ((detailE - detailDelta) / eps) + northAxis * ((detailN - detailDelta) / eps);
        }

        const float3 sunDir = normalize(float3(0.6f, 0.4f, 0.7f));
        const float hillScale = isEmergentLand ? 0.18f : 0.10f;
        const float shade = clamp(1.0f - hillScale * dot(gradWorld * cellScale, sunDir), 0.68f, 1.28f);
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
        else if (mode == RenderModePlumes)
        {
            rgb = isContinental ? float3(0.15f, 0.16f, 0.18f) : float3(0.03f, 0.05f, 0.11f);
            const float swellKm = EvaluatePlumeSwellKm(worldPoint, RenderMapConstants.Time);
            if (swellKm > 0.05f)
            {
                rgb = lerp(rgb, float3(0.06f, 0.34f, 0.50f), saturate(swellKm / 1.8f));
            }

            if (state.GetOrogenyType() == OrogenyVolcanic && state.GetRelief() > 0.05f)
            {
                const float volcanicAge = max(RenderMapConstants.Time - state.GetOrogenyTime(), 0.0f);
                const float freshness = 1.0f - saturate(volcanicAge / 80.0f);
                const float3 oldSeamountColor = float3(0.36f, 0.12f, 0.54f);
                const float3 warmChainColor = float3(0.95f, 0.32f, 0.12f);
                const float3 activeCoreColor = float3(1.00f, 0.96f, 0.55f);
                const float3 trackColor = (freshness > 0.75f)
                    ? lerp(warmChainColor, activeCoreColor, (freshness - 0.75f) * 4.0f)
                    : lerp(oldSeamountColor, warmChainColor, freshness / 0.75f);
                rgb = lerp(rgb, trackColor, saturate(0.45f + state.GetRelief() / 3.0f));
            }

            if ((PlumeConstants.Flags & PlumeFlagEnabled) != 0u && PlumeConstants.PlumeCount > 0u)
            {
                const bool driftEnabled = (PlumeConstants.Flags & PlumeFlagDriftEnabled) != 0u;
                const uint activeCount = min(PlumeConstants.PlumeCount, MaxMantlePlumes);
                for (uint plumeIdx = 0u; plumeIdx < activeCount; ++plumeIdx)
                {
                    const MantlePlumeGpu plume = PlumeConstants.Plumes[plumeIdx];
                    const float lifeEnvelope = EvaluatePlumeLifeEnvelope(
                        RenderMapConstants.Time, plume.TimingAndPulse.x, plume.TimingAndPulse.y);
                    if (lifeEnvelope <= 0.0f)
                    {
                        continue;
                    }

                    const float3 plumePos = EvaluatePlumePosition(plume, RenderMapConstants.Time, driftEnabled);
                    const float distKm = GeodesicKm(worldPoint, plumePos);
                    if (abs(distKm - plume.PositionAndRadius.w) < 35.0f)
                    {
                        rgb = lerp(rgb, float3(0.20f, 0.96f, 1.00f), 0.85f);
                    }
                    else if (abs(distKm - plume.SwellAndFlux.x) < 25.0f)
                    {
                        rgb = lerp(rgb, float3(0.15f, 0.72f, 0.92f), 0.45f);
                    }
                }
            }
        }
    }

    MapOutput[threadId.xy] = float4(rgb, 1.0f);
}
