implementing PlanetLab;
// Nested HEALPix (Gorski et al. 2005, ApJ 622:759):
// 12 equal-area curvilinear diamond base faces (0..3 north cap, 4..7 equatorial belt, 8..11 south cap),
// each subdivided into sideLength x sideLength pixels (sideLength = 2^order) in 2D Morton (Z-order) layout.
// 32-bit index safe up to order = 13 (805,306,368 pixels).
internal static const float HealpixTwoPi = 6.28318530718f;
internal static const float HealpixHalfPi = 1.57079632679f;
internal static const float HealpixInvHalfPi = 0.6366197723675814f;

// Ring index multiplier for each base face's southern vertex (Gorski et al. 2005, "jrll"):
// south vertex sits at ring = multiplier * sideLength.
internal static const int k_FaceSouthRingMultiplier[12] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };

// Azimuthal center offset of each base face in units of pi/4 (Gorski et al. 2005, "jpll").
internal static const int k_FaceAzimuthOffset[12] = { 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7 };

// 8-neighbor steps in face-local (faceX, faceY): SW, W, NW, N, NE, E, SE, S
internal static const int k_NeighborDeltaX[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
internal static const int k_NeighborDeltaY[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };

// Destination base face indexed [seamSector][baseFace], where seamSector = 4 + signX + 3 * signY.
// -1 marks the 24 seven-neighbor pixels touching the 8 degree-3 rhombic-dodecahedron vertices.
internal static const int k_AdjacentBaseFace[9][12] =
{
    {  8,  9, 10, 11, -1, -1, -1, -1, 10, 11,  8,  9 }, // 0: S  (x < 0, y < 0)
    {  5,  6,  7,  4,  8,  9, 10, 11,  9, 10, 11,  8 }, // 1: SE (y < 0)
    { -1, -1, -1, -1,  5,  6,  7,  4, -1, -1, -1, -1 }, // 2: E  (x >= sideLength, y < 0)
    {  4,  5,  6,  7, 11,  8,  9, 10, 11,  8,  9, 10 }, // 3: SW (x < 0)
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11 }, // 4: Interior (same base face)
    {  1,  2,  3,  0,  0,  1,  2,  3,  5,  6,  7,  4 }, // 5: NE (x >= sideLength)
    { -1, -1, -1, -1,  7,  4,  5,  6, -1, -1, -1, -1 }, // 6: W  (x < 0, y >= sideLength)
    {  3,  0,  1,  2,  3,  0,  1,  2,  4,  5,  6,  7 }, // 7: NW (y >= sideLength)
    {  2,  3,  0,  1, -1, -1, -1, -1,  0,  1,  2,  3 }, // 8: N  (x >= sideLength, y >= sideLength)
};

internal static const uint k_SeamFlipX  = 1u;
internal static const uint k_SeamFlipY  = 2u;
internal static const uint k_SeamSwapXY = 4u;

// Coordinate transform across a base-face seam, indexed [seamSector][baseFace >> 2] (0=north, 1=belt, 2=south).
// Important: apply k_SeamFlipX and k_SeamFlipY BEFORE k_SeamSwapXY.
internal static const uint k_SeamTransformMask[9][3] =
{
    { 0u, 0u, k_SeamFlipX | k_SeamFlipY },                 // 0: S
    { 0u, 0u, k_SeamFlipY | k_SeamSwapXY },                // 1: SE
    { 0u, 0u, 0u },                                        // 2: E
    { 0u, 0u, k_SeamFlipX | k_SeamSwapXY },                // 3: SW
    { 0u, 0u, 0u },                                        // 4: Interior
    { k_SeamFlipX | k_SeamSwapXY, 0u, 0u },                // 5: NE
    { 0u, 0u, 0u },                                        // 6: W
    { k_SeamFlipY | k_SeamSwapXY, 0u, 0u },                // 7: NW
    { k_SeamFlipX | k_SeamFlipY, 0u, 0u },                 // 8: N
};

// Interleaves the low 16 bits into even bit positions for 2D Morton encoding.
internal uint SpreadBits16(uint value)
{
    value &= 0x0000FFFFu;
    value = (value | (value << 8u)) & 0x00FF00FFu;
    value = (value | (value << 4u)) & 0x0F0F0F0Fu;
    value = (value | (value << 2u)) & 0x33333333u;
    value = (value | (value << 1u)) & 0x55555555u;
    return value;
}

// Extracts even bit positions back into the low 16 bits for 2D Morton decoding.
internal uint CompactBits16(uint value)
{
    value &= 0x55555555u;
    value = (value | (value >> 1u)) & 0x33333333u;
    value = (value | (value >> 2u)) & 0x0F0F0F0Fu;
    value = (value | (value >> 4u)) & 0x00FF00FFu;
    value = (value | (value >> 8u)) & 0x0000FFFFu;
    return value;
}

internal void HealpixDecode(uint pixelIndex, uint order, out uint baseFace, out int faceX, out int faceY)
{
    const uint sideLength = 1u << order;
    const uint pixelsPerFace = sideLength * sideLength;
    baseFace = pixelIndex >> (2u * order);
    const uint inFaceMorton = pixelIndex & (pixelsPerFace - 1u);
    faceX = int(CompactBits16(inFaceMorton));
    faceY = int(CompactBits16(inFaceMorton >> 1u));
}

internal uint HealpixEncode(uint baseFace, uint faceX, uint faceY, uint order)
{
    return (baseFace << (2u * order)) | SpreadBits16(faceX) | (SpreadBits16(faceY) << 1u);
}

internal float3 HealpixPixToVec(in uint ipix, in uint order)
{
    uint baseFace = 0u;
    int faceX = 0;
    int faceY = 0;
    HealpixDecode(ipix, order, baseFace, faceX, faceY);
    const uint sideLength = 1u << order;
    const int sideLengthSigned = int(sideLength);
    const int ringIndex = (k_FaceSouthRingMultiplier[baseFace] << order) - faceX - faceY - 1;
    const float polarCapZScale = 1.0f / (3.0f * float(sideLength * sideLength));
    const float equatorialZStep = 2.0f / (3.0f * float(sideLength));

    // pixels in one pi/2 quadrant of this iso-latitude ringt (total = 4 * count)
    int ringQuarterCount;
    // 1 on odd equatorial rings who pixel centers are shifted by half a pixel in phi
    int ringHalfPixelShift;
    float z = 0.0f;

    if (ringIndex < sideLengthSigned)
    {
        // north polar cap: z = 1 - (ring*ring) / (3*sideLength*sideLength)
        ringQuarterCount = ringIndex;
        z = 1.0f - float(ringQuarterCount * ringQuarterCount) * polarCapZScale;
        ringHalfPixelShift = 0;
    }
    else if (ringIndex > 3 * sideLengthSigned)
    {
        // south polar cap: symmetric to north polar cap
        ringQuarterCount = 4 * sideLengthSigned - ringIndex;
        z = float(ringQuarterCount * ringQuarterCount) * polarCapZScale - 1.0f;
        ringHalfPixelShift = 0;
    }
    else
    {
        // equatorial belt: constant-dz cylindrical equal-area rings
        ringQuarterCount = sideLengthSigned;
        z = float(2 * sideLengthSigned - ringIndex) * equatorialZStep;
        ringHalfPixelShift = (ringIndex - sideLengthSigned) & 1;
    }

    // 1-based pixel index around the iso-latitude ring, wrapped into [1, 4 * ringQuarterCount]
    int ringPixelIndex = (k_FaceAzimuthOffset[baseFace] * ringQuarterCount + faceX - faceY + 1 + ringHalfPixelShift) >> 1;
    const int pixelsInRing = 4 * ringQuarterCount;
    if (ringPixelIndex > pixelsInRing)
    {
        // wrap it
        ringPixelIndex -= pixelsInRing;
    }

    if (ringPixelIndex < 1)
    {
        // wrap it other dir
        ringPixelIndex += pixelsInRing;
    }

    const float phi = (float(ringPixelIndex) - 0.5f * float(ringHalfPixelShift + 1)) * (HealpixHalfPi / float(ringQuarterCount));
    const float sinTheta = sqrt(max(0.0f, 1.0f - z * z));
    float sinPhi, cosPhi;
    sincos(phi, sinPhi, cosPhi);
    return float3(sinTheta * cosPhi, sinTheta * sinPhi, z);
}

internal uint HealpixVecToPix(float3 dir, uint order)
{
    const uint sideLength = 1u << order;
    const int sideLengthSigned = int(sideLength);
    const int maxCoord = sideLengthSigned - 1;
    const float absZ = abs(dir.z);

    float phi = atan2(dir.y, dir.x);
    if (phi < 0.0f)
    {
        // wrap phi around
        phi += HealpixTwoPi;
    }

    // longitude in quarter-turns (as units of pi/2), clamped strictly below 4.0
    const float quarterTurns = min(phi * HealpixInvHalfPi, 3.9999995f);

    uint baseFace = 0;
    uint faceX = 0;
    uint faceY = 0;

    if (absZ <= 0.66666666667f)
    {
        // equatorial belt: (quarterTurns, z) into diagonal face-edge coords
        const float beltAlong = float(sideLength) * (0.5f + quarterTurns);
        const float beltAcross = float(sideLength) * dir.z * 0.75f;
        const int diagDescending = int(beltAlong - beltAcross);
        const int diagAscending = int(beltAlong + beltAcross);
        const int descendingBand = diagDescending >> int(order);
        const int ascendingBand = diagAscending >> int(order);

        if (descendingBand == ascendingBand)
        {
            baseFace = uint(descendingBand | 4);
        }
        else if (descendingBand < ascendingBand)
        {
            baseFace = uint(descendingBand);
        }
        else
        {
            baseFace = uint(ascendingBand + 8);
        }

        faceX = uint(diagAscending & maxCoord);
        faceY = uint(sideLengthSigned - (diagDescending & maxCoord) - 1);
    }
    else
    {
        // at the polar caps: radial distance from the pole then scales as sqrt(3 * (1 - |z|)) to preserve
        // equal area properties we need
        // Use (x^2 + y^2) / (1 + |z|) instead of (1 - |z|) to avoid float32 cancellation near the poles
        const int polarQuadrant = min(int(quarterTurns), 3);
        const float quadrantFraction = quarterTurns - float(polarQuadrant);
        const float oneMinusAbsZ = (dir.x * dir.x + dir.y * dir.y) / (1.0f + absZ);
        const float polarRadialPixels = float(sideLength) * sqrt(max(0.0f, 3.0f * oneMinusAbsZ));
        const int clockwiseOffset = min(int(quadrantFraction * polarRadialPixels), maxCoord);
        const int counterClockwiseOffset = min(int((1.0f - quadrantFraction) * polarRadialPixels), maxCoord);

        if (dir.z >= 0.0f)
        {
            baseFace = uint(polarQuadrant);
            faceX = uint(sideLengthSigned - counterClockwiseOffset - 1);
            faceY = uint(sideLengthSigned - clockwiseOffset - 1);
        }
        else
        {
            baseFace = uint(polarQuadrant + 8);
            faceX = uint(clockwiseOffset);
            faceY = uint(counterClockwiseOffset);
        }
    }

    return HealpixEncode(baseFace, faceX, faceY, order);
}

// analytic 8-neight lookup across base-face seams: returns -1 for the 24 missing corner diagonals
internal int HealpixNeighbor(in uint baseFace, in int faceX, in int faceY, in int deltaX, in int deltaY, in uint order)
{
    const int sideLength = int(1u << order);
    int neighborX = faceX + deltaX;
    int neighborY = faceY + deltaY;
    int seamSector = 4;

    if (neighborX < 0)
    {
        neighborX += sideLength;
        seamSector -= 1;
    }
    else if (neighborX >= sideLength)
    {
        neighborX -= sideLength;
        seamSector += 1;
    }

    if (neighborY < 0)
    {
        neighborY += sideLength;
        seamSector -= 3;
    }
    else if (neighborY >= sideLength)
    {
        neighborY -= sideLength;
        seamSector += 3;
    }

    const int dstFace = k_AdjacentBaseFace[seamSector][baseFace];
    if (dstFace < 0)
    {
        return -1; // missing corner diagonal
    }

    const uint transformMask = k_SeamTransformMask[seamSector][baseFace >> 2u];
    if ((transformMask & k_SeamFlipX) != 0u)
    {
        neighborX = sideLength - 1 - neighborX;
    }

    if ((transformMask & k_SeamFlipY) != 0u)
    {
        neighborY = sideLength - 1 - neighborY;
    }

    if ((transformMask & k_SeamSwapXY) != 0u)
    {
        int swapTemp = neighborX;
        neighborX = neighborY;
        neighborY = swapTemp;
    }

    return int(HealpixEncode(uint(dstFace), uint(neighborX), uint(neighborY), order));
}

// Calabretta & Roukema (2007) continuous intra-face coordinates in [0, sideLength).
internal void HealpixVecToFaceUV(float3 dir, uint order, out uint baseFace, out float2 faceUV)
{
    const uint sideLength = 1u << order;
    const float sideLengthF = float(sideLength);
    const float maxUV = sideLengthF - 1e-4f;
    const float absZ = abs(dir.z);

    float phi = atan2(dir.y, dir.x);
    if (phi < 0.0f)
    {
        phi += HealpixTwoPi;
    }

    const float quarterTurns = min(phi * HealpixInvHalfPi, 3.9999995f);

    if (absZ <= 0.6666666667f)
    {
        const float beltAlong = sideLengthF * (0.5f + quarterTurns);
        const float beltAcross = sideLengthF * dir.z * 0.75f;
        const float diagDescendingCont = max(0.0f, beltAlong - beltAcross);
        const float diagAscendingCont = max(0.0f, beltAlong + beltAcross);
        const int descendingBand = min(int(floor(diagDescendingCont)) >> int(order), 4);
        const int ascendingBand = min(int(floor(diagAscendingCont)) >> int(order), 4);

        if (descendingBand == ascendingBand)
        {
            baseFace = uint(descendingBand | 4);
        }
        else if (descendingBand < ascendingBand)
        {
            baseFace = uint(descendingBand);
        }
        else
        {
            baseFace = uint(ascendingBand + 8);
        }

        faceUV.x = clamp(diagAscendingCont - float(ascendingBand << order), 0.0f, maxUV);
        faceUV.y = clamp(sideLengthF - (diagDescendingCont - float(descendingBand << order)), 0.0f, maxUV);
    }
    else
    {
        const int polarQuadrant = min(int(quarterTurns), 3);
        const float quadrantFraction = quarterTurns - float(polarQuadrant);
        const float oneMinusAbsZ = (dir.x * dir.x + dir.y * dir.y) / (1.0f + absZ);
        const float polarRadialPixels = sideLengthF * sqrt(max(0.0f, 3.0f * oneMinusAbsZ));
        const float cwCont = min(quadrantFraction * polarRadialPixels, maxUV);
        const float ccwCont = min((1.0f - quadrantFraction) * polarRadialPixels, maxUV);

        if (dir.z >= 0.0f)
        {
            baseFace = uint(polarQuadrant);
            faceUV = float2(sideLengthF - ccwCont, sideLengthF - cwCont);
        }
        else
        {
            baseFace = uint(polarQuadrant + 8);
            faceUV = float2(cwCont, ccwCont);
        }
    }
}

// Resolves a face-local (faceX, faceY) coordinate across base-face seams; returns -1 for the 24 missing corner diagonals.
internal int HealpixSampleIndex(uint baseFace, int faceX, int faceY, uint order)
{
    return HealpixNeighbor(baseFace, faceX, faceY, 0, 0, order);
}


// Verification probe - run by PlaneetGenCheck
struct HealpixProbeParams
{
    uint Order;
    uint PixelCount;
};
ConstantBuffer<HealpixProbeParams> HealpixProbeConstants;
Points HealpixProbeVecs;      // .xyz = PixToVec(pixelIndex), .w = asfloat(VecToPix(vec))
Ints HealpixProbeNeighbors;   // 8 * PixelCount: 8 neighbors per pixel (-1 if missing)
[shader("compute")]
[numthreads(64, 1, 1)]
void HealpixProbeCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint pixelIndex = threadId.x;
    if (pixelIndex >= HealpixProbeConstants.PixelCount)
    {
        return;
    }
    const uint order = HealpixProbeConstants.Order;
    const float3 direction = HealpixPixToVec(pixelIndex, order);
    const uint roundTripIndex = HealpixVecToPix(direction, order);
    HealpixProbeVecs.Store(pixelIndex, float4(direction, asfloat(roundTripIndex)));
    uint baseFace;
    int faceX;
    int faceY;
    HealpixDecode(pixelIndex, order, baseFace, faceX, faceY);
    const uint neighborBase = pixelIndex * 8u;
    [unroll]
    for (uint dirIndex = 0u; dirIndex < 8u; ++dirIndex)
    {
        const int neighbor = HealpixNeighbor(baseFace, faceX, faceY, k_NeighborDeltaX[dirIndex], k_NeighborDeltaY[dirIndex], order);
        HealpixProbeNeighbors.Store(neighborBase + dirIndex, neighbor);
    }
}
