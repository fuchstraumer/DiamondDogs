# PlanetGen v2 — Unified Nested HEALPix Architecture, GPU BKE Collision, and Sparse Virtual Plate Paging

> **Status:** Architectural Blueprint (2026-11-19)  
> **Supersedes:** Fibonacci-sphere v1 simulation lattice (`applications/PlanetGen/` v1 remains a verified, benchmarked baseline: `106.5 µs` @ 100k, `290.8 µs` @ 500k, 9/9 checks PASS).  
> **Core Shift:** Replace the unstructured 1D Spherical Fibonacci spiral with a **Native Nested HEALPix** (`12 × 4^order`) equal-area spherical quadtree across tectonics, collision CCL, sparse virtual plate paging, and future climate/hydrology/erosion/rendering pipelines.

---

## 1. Executive Summary & Architectural Motivation

### 1.1 Why Pivot Now?
PlanetGen v1 proved that full GPU plate tectonics runs **~30×–150× faster than 60 fps** (`106.5 µs` @ 100k, `290.8 µs` @ 500k on RTX 4080) and delivered three reusable pillars we keep intact:
1. **Verification harness (`PlanetGenCheck`)** — headless Vulkan Full-validation test runner.
2. **Benchmarking & Nsight pipeline (`PlanetGenBench` + `scripts/profiler`)** — locked-clock GPU timer & shader-stall database.
3. **Interactive capture & tooling (`PlanetGenTimelapse` + `FrameCapture` + `AgentTools`)** — live multi-step driver, debug GBuffer modes, and JSON-RPC subregion inspection.

At the same time, v1 exposed **four structural ceilings** caused by the 1D Fibonacci spiral lattice:
1. **Spatial vs. Memory Order Mismatch (`OwnersCS`):** As documented in [README.md](README.md#L277-L299) and [noise-shaders-agent.md](../../docs/noise-shaders-agent.md#L307-L322), sorting threads by Nested HEALPix cut plates tested per wave from **7.96 → 2.82**, yet regressed runtime because buffers remained indexed in 1D spiral order (gather traffic jumped from 10 → 28 sectors/plate). Only a **native HEALPix-indexed lattice** captures both the 2.8× cull win *and* coalesced memory gathers.
2. **95% Dead-Cell Memory Explosion (`UpliftCS` 500k L2 Cliff):** Dense `[PlateCount × PointCount]` arrays allocate 160 MB (`CrustState`) + 80 MB (`CellFronts`) = **240 MB** at 500k when only ~5% of cells are alive, spilling the 4080's 64 MB L2 and causing a 9.5× slowdown in `UpliftCS`.
3. **Unstructured Graph Overhead for Collision CCL & Stencils:** Continental collision (`Collide`/`Terrane`), coastal shelves (`coast_distance`), thermal diffusion, and erosion all require neighbor queries. On Fibonacci, that forces a static `6 × N` neighbor table (12 MB at 500k) and 1×1 node-level Union-Find (Oliveira-Lotufo) with high atomic contention.
4. **Grid Fragmentation Across Future Subsystems:** Our own climate, hydrology, and noise architecture docs ([Procedural_Planet_Generation_Reference.md](../../../DocParse/results/Procedural_Planet_Generation_Reference.md), [NoiseGrids.slang](../../assets/shaders/compute/Noise/NoiseGrids.slang)) *already* specify **HEALPix** as the target spherical grid.

### 1.2 Dual Purpose: Driving `RhiResources` & `RenderGraph` (`Loom`)
Rather than designing `modules/RhiResources` and `modules/RenderGraph` (`loom`) against an abstract ideal, PlanetGen v2 + `OceanFft` serve as real, demanding workloads:
- **Software sparse tile paging** exercises compute-driven buffer pools, placed transient aliasing (`rhi::placement`), and async tile streaming *without* legacy `VK_QUEUE_SPARSE_BINDING` (preserving 100% WebGPU portability).
- **Conditional collision passes + multi-rate climate/erosion passes** exercise `loom::RunPolicy::OnTrigger`, indirect dispatch barriers, and future `Transfer`/`Compute` queue handoffs.

---

## 2. Unified Spherical Grid: Native Nested HEALPix (`Healpix.slang`)

### 2.1 Geometry & Resolution Ladder
HEALPix (Górski et al. 2005) partitions the unit sphere into **12 equal-area curvilinear diamond base faces** (faces `0–3` North polar cap, `4–7` Equatorial belt, `8–11` South polar cap). Each face is an $N_{\text{side}} \times N_{\text{side}}$ grid where $N_{\text{side}} = 2^{\text{order}}$, indexed in **Nested (2D Morton / Z-order)** layout:

$$\text{ipix} = (\text{face} \ll (2 \cdot \text{order})) \;\big|\; \text{SpreadBits}(ix) \;\big|\; (\text{SpreadBits}(iy) \ll 1)$$

| Order | $N_{\text{side}}$ | Total Cells ($12 \cdot 4^{\text{order}}$) | Mean Cell Spacing (Earth $R = 6370\text{ km}$) | `8×8` Tiles / Sphere | Role in PlanetGen v2 |
| :---: | :---: | ---: | :---: | ---: | :--- |
| 4 | 16 | 3,072 | 408 km | 48 | Interactive scrub macro-climate & SH ($l \le 12$) |
| 5 | 32 | 12,288 | 204 km | 192 | Fast unit-test / mobile preview / coarse multigrid |
| 6 | 64 | 49,152 | 102 km | 768 | Idle climate (`Texture2DArray`) / low-end WebGPU |
| **7** | **128** | **196,608** | **51.0 km** | **3,072** | **Default real-time tectonic profile** |
| **8** | **256** | **786,432** | **25.5 km** | **12,288** | **High-detail desktop profile (fits L2 with paging!)** |
| 9 | 512 | 3,145,728 | 12.7 km | 49,152 | Ultra / localized AMR refinement (hotspots, arcs) |
| 10 | 1,024 | 12,582,912 | 6.4 km | 196,608 | Regional erosion / GeoDS orographic downscaling |

**32-Bit WebGPU / WGSL Safety:** `12 × 4^order` fits in an unsigned 32-bit `uint` all the way to **`order = 13` (`805,306,368` cells, ~0.8 km spacing)**. Unlike `Fibonacci.slang` (which needed emulated 64-bit `UMul32`), **zero 64-bit integers are needed anywhere in Slang or WGSL**.

### 2.2 Machine-Verified Core Math (`Healpix.slang`)

All equations, constants, and tables below were exhaustively verified in Python (`order = 1..6` full bijection = 0 errors; `order = 7..10` pure `float32` polar-cap round-trip = 0 errors; `order = 1..5` neighbor symmetry = 0 asymmetries).

#### Bit Interleaving (`uint32_t`, up to `order = 13`)
```hlsl
internal uint SpreadBits16(uint v)
{
    v &= 0x0000FFFFu;
    v = (v | (v << 8u)) & 0x00FF00FFu;
    v = (v | (v << 4u)) & 0x0F0F0F0Fu;
    v = (v | (v << 2u)) & 0x33333333u;
    v = (v | (v << 1u)) & 0x55555555u;
    return v;
}

internal uint CompactBits16(uint v)
{
    v &= 0x55555555u;
    v = (v | (v >> 1u)) & 0x33333333u;
    v = (v | (v >> 2u)) & 0x0F0F0F0Fu;
    v = (v | (v >> 4u)) & 0x00FF00FFu;
    v = (v | (v >> 8u)) & 0x0000FFFFu;
    return v;
}
```

#### Forward Map: `HealpixPixToVec(uint ipix, uint order)` (Replaces `LatticePoint`)
```hlsl
static const int k_Jrll[12] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };
static const int k_Jpll[12] = { 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7 };

internal float3 HealpixPixToVec(uint ipix, uint order)
{
    const uint nside = 1u << order;
    const uint npface = nside * nside;
    const uint face = ipix >> (2u * order);
    const uint inFace = ipix & (npface - 1u);
    const int ix = int(CompactBits16(inFace));
    const int iy = int(CompactBits16(inFace >> 1u));

    const int jr = (k_Jrll[face] << order) - ix - iy - 1;
    const int ns = int(nside);
    const float fact1 = 1.0f / (3.0f * float(npface));
    const float fact2 = 2.0f / (3.0f * float(nside));

    int nr;
    int kshift;
    float z;
    if (jr < ns)
    {
        nr = jr;
        z = 1.0f - float(nr * nr) * fact1;
        kshift = 0;
    }
    else if (jr > 3 * ns)
    {
        nr = 4 * ns - jr;
        z = float(nr * nr) * fact1 - 1.0f;
        kshift = 0;
    }
    else
    {
        nr = ns;
        z = float(2 * ns - jr) * fact2;
        kshift = (jr - ns) & 1;
    }

    int jp = (k_Jpll[face] * nr + ix - iy + 1 + kshift) >> 1;
    if (jp > 4 * nr) jp -= 4 * nr;
    if (jp < 1)      jp += 4 * nr;

    const float phi = (float(jp) - 0.5f * float(kshift + 1)) * (1.57079632679f / float(nr));
    const float sinTheta = sqrt(max(0.0f, 1.0f - z * z));
    float s, c;
    sincos(phi, s, c);
    return float3(sinTheta * c, sinTheta * s, z);
}
```

#### Inverse Map: `HealpixVecToPix(float3 dir, uint order)` (Replaces `FibonacciNearest`)
> **Float32 Pole-Stability Note:** Near the poles ($|z| \to 1$), computing `1.0f - abs(z)` suffers catastrophic cancellation in `float32`. Use the exact algebraic identity $1 - |z| = \frac{x^2 + y^2}{1 + |z|}$ for unit vectors to preserve full 24-bit mantissa accuracy all the way to `order = 10+`.

```hlsl
internal uint HealpixVecToPix(float3 dir, uint order)
{
    const uint nside = 1u << order;
    const int last = int(nside) - 1;
    const float absZ = abs(dir.z);

    float phi = atan2(dir.y, dir.x);
    if (phi < 0.0f) phi += 6.28318530718f;
    // Quarter-turns in [0, 4)
    const float tt = min(phi * 0.6366197723675814f, 3.9999995f);

    uint face;
    uint ix;
    uint iy;

    if (absZ <= 0.6666666667f)
    {
        const float along = float(nside) * (0.5f + tt);
        const float across = float(nside) * dir.z * 0.75f;
        const int jp = int(along - across);
        const int jm = int(along + across);
        const int ifp = jp >> int(order);
        const int ifm = jm >> int(order);

        if (ifp == ifm)      face = uint(ifp | 4);
        else if (ifp < ifm)  face = uint(ifp);
        else                 face = uint(ifm + 8);

        ix = uint(jm & last);
        iy = uint(int(nside) - (jp & last) - 1);
    }
    else
    {
        const int ntt = min(int(tt), 3);
        const float tp = tt - float(ntt);
        // Numerically stable 1 - |z| for float32 near poles
        const float oneMinusAbsZ = (dir.x * dir.x + dir.y * dir.y) / (1.0f + absZ);
        const float tmp = float(nside) * sqrt(max(0.0f, 3.0f * oneMinusAbsZ));
        const int jp = min(int(tp * tmp), last);
        const int jm = min(int((1.0f - tp) * tmp), last);

        if (dir.z >= 0.0f)
        {
            face = uint(ntt);
            ix = uint(int(nside) - jm - 1);
            iy = uint(int(nside) - jp - 1);
        }
        else
        {
            face = uint(ntt + 8);
            ix = uint(jp);
            iy = uint(jm);
        }
    }

    return (face << (2u * order)) | SpreadBits16(ix) | (SpreadBits16(iy) << 1u);
}
```

#### Exact Analytic 8-Neighbor Lookup (`HealpixNeighbor`, Zero Neighbor Buffer!)
In Nested HEALPix, stepping by `(dx, dy)` in `{-1, 0, +1}^2` computes `nbnum = 4 + signX + 3 * signY` in `[0, 8]`. When crossing a face seam (`nbnum != 4`), `k_NbFace[nbnum][face]` yields the destination face (`-1` for the **24 pixels** touching the 8 degree-3 rhombic-dodecahedron vertices, which have 7 neighbors instead of 8), and `k_NbSwap[nbnum][face >> 2]` encodes the coordinate transform.

> **Critical Transform Order (Verified):** Apply bit-flips **before** coordinate swap:  
> `if (bits & 1) x = nside - 1 - x;` → `if (bits & 2) y = nside - 1 - y;` → `if (bits & 4) swap(x, y);`

```hlsl
// Indexed [nbnum][face], where nbnum = 4 + signX + 3 * signY
static const int k_NbFace[9][12] = {
    {  8,  9, 10, 11, -1, -1, -1, -1, 10, 11,  8,  9 }, // 0: S  (x<0, y<0)
    {  5,  6,  7,  4,  8,  9, 10, 11,  9, 10, 11,  8 }, // 1: SE (y<0)
    { -1, -1, -1, -1,  5,  6,  7,  4, -1, -1, -1, -1 }, // 2: E  (x>=ns, y<0)
    {  4,  5,  6,  7, 11,  8,  9, 10, 11,  8,  9, 10 }, // 3: SW (x<0)
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11 }, // 4: Center
    {  1,  2,  3,  0,  0,  1,  2,  3,  5,  6,  7,  4 }, // 5: NE (x>=ns)
    { -1, -1, -1, -1,  7,  4,  5,  6, -1, -1, -1, -1 }, // 6: W  (x<0, y>=ns)
    {  3,  0,  1,  2,  3,  0,  1,  2,  4,  5,  6,  7 }, // 7: NW (y>=ns)
    {  2,  3,  0,  1, -1, -1, -1, -1,  0,  1,  2,  3 }, // 8: N  (x>=ns, y>=ns)
};

// Indexed [nbnum][face >> 2]: bit0 = flipX, bit1 = flipY, bit2 = swapXY (apply flips BEFORE swap)
static const uint k_NbSwap[9][3] = {
    { 0u, 0u, 3u }, // 0
    { 0u, 0u, 6u }, // 1
    { 0u, 0u, 0u }, // 2
    { 0u, 0u, 5u }, // 3
    { 0u, 0u, 0u }, // 4
    { 5u, 0u, 0u }, // 5
    { 0u, 0u, 0u }, // 6
    { 6u, 0u, 0u }, // 7
    { 3u, 0u, 0u }, // 8
};

internal int HealpixNeighbor(uint face, int ix, int iy, int dx, int dy, uint order)
{
    const int ns = int(1u << order);
    int x = ix + dx;
    int y = iy + dy;
    int nbnum = 4;

    if (x < 0)        { x += ns; nbnum -= 1; }
    else if (x >= ns) { x -= ns; nbnum += 1; }
    if (y < 0)        { y += ns; nbnum -= 3; }
    else if (y >= ns) { y -= ns; nbnum += 3; }

    const int dstFace = k_NbFace[nbnum][face];
    if (dstFace < 0) return -1;

    const uint bits = k_NbSwap[nbnum][face >> 2u];
    if ((bits & 1u) != 0u) x = ns - 1 - x;
    if ((bits & 2u) != 0u) y = ns - 1 - y;
    if ((bits & 4u) != 0u) { int t = x; x = y; y = t; }

    return int((uint(dstFace) << (2u * order)) | SpreadBits16(uint(x)) | (SpreadBits16(uint(y)) << 1u));
}
```

### 2.3 Migration of Passes 1–6 & Verification Strategy
- **Uniforms (`OwnersParams`):** Add `uint HealpixOrder` (with `PointCount = 12u << (2u * HealpixOrder)`).
- **Direct swaps in existing shaders:**
  - `Owners.slang`, `Crust.slang`, `FrontPoints.slang`, `LiveCells.slang`, `TorqueSums.slang`, `RenderMap.slang`: replace `LatticePoint(i, n)` → `HealpixPixToVec(i, Params.HealpixOrder)` and `FibonacciNearest(dir, n)` → `HealpixVecToPix(dir, Params.HealpixOrder)`.
  - **`RenderMapCS` bonus:** Because each face is a regular 2D grid, we can immediately upgrade `RenderMapCS` from nearest-neighbor Voronoi sampling to **2D bilinear interpolation** inside each HEALPix face (using `HealpixNeighbor` across borders) + domain-warped fBm detail!
- **Host side (`applications/PlanetGen/gpu/Healpix.hpp/.cpp`):** Already implements `HealpixNestedIndex` (`ang2pix_nest`)! Extend it with `HealpixPixToVec` and `HealpixNeighbors8` in C++, plus a procedural/Voronoi plate initializer (`InitPlatesHealpix`) so v2 does not depend on Fibonacci reference files.
- **Verification (`PlanetGenCheck`):**
  1. Keep v1 Fibonacci checks runnable behind `--legacy-fibonacci` (or a separate target) until v2 is proven, or archive v1 on a git tag.
  2. Add **HEALPix math invariants** (exhaustive bijection `VecToPix(PixToVec(i)) == i`, 0 neighbor asymmetries, exactly 24 7-neighbor pixels, equal-area distribution check matching [check_healpix.py](../PlanetLab/python/check_healpix.py)).
  3. Add **Physics & Conservation invariants** (zero NaNs over 500 ticks, plate rotation orthogonality $\|\mathbf{R}^T\mathbf{R} - \mathbf{I}\| < 10^{-5}$, continental crust area conservation through collisions, exact CCL component count matching CPU BFS on a test continent mask).

---

## 3. Pure-GPU Continental Collision & Terrane Transfer (`Collide.slang`)

In [tectonics.py](../PlanetLab/planetlab/tectonics.py#L269-L379), continental collision (`Collide` → `Terrane` → `TransferTerrane`) runs right after `RemoveSubductedCS` whenever two continental crusts overlap by `TERRANE_PENETRATION = 300 km`. Staying **100% on the GPU with zero CPU readbacks** requires four pieces:

### 3.1 Zero-Cost Gating (`CollideDispatch`)
Continental transfers occur on only **~1–5% of ticks**.
1. **`DetectCollisionsCS` (`N` threads):** For each world point where `Owners` has two continental plates `(top, under)`:
   - Atomically increment `CollisionCount[pair]`.
   - Accumulate `CollisionSpeedSum[pair]` (fixed-point `×1024`).
   - Measure penetration depth along the convergence direction `dir = normalize(cross(spin, worldPt))` by atomically tracking `minProj` and `maxProj` (`OrderedFloatToUint(dot(worldPt, dir))` via `InterlockedMin`/`InterlockedMax`), giving `depthKm = (maxProj - minProj) * PlanetRadiusKm`. *(Optional Phase 2 refinement: 8-iteration 8-neighbor erosion on the collision mask for exact `2 * max_dist_to_outside - spacing` parity.)*
2. **`SelectCollisionPairCS` (`[numthreads(32, 1, 1)]`, 1 workgroup):**
   - Scans the `P × P` pair table (400 entries for 20 plates), selects one colliding pair `(top, under)` with `count >= 3 && depthKm >= 300.0f`.
   - If **no pair qualifies**: writes `{0, 1, 1}` to `CollideDispatch` (`VkDispatchIndirectCommand`). All downstream collision dispatches become 0-workgroup no-ops (**~5–10 µs total tick overhead**).
   - If **a pair qualifies**: writes `CollisionJob { TopPlate, UnderPlate, PairSpeed, ... }` and populates indirect group counts for the BKE CCL and transfer passes.

### 3.2 Allegretti `2×2` Block Equivalence (BKE) on 12 HEALPix Faces
On a Fibonacci graph, CCL is forced into 1×1 Oliveira-Lotufo node-level Union-Find. On **Nested HEALPix**, every pixel is part of a 2D Morton-ordered face, which unlocks **Allegretti et al. (IEEE TPDS 2020) Block-Based Equivalence (BKE)** with two massive structural wins:

1. **Nested Order = Free `2×2` Block Addressing:**
   - Every `2×2` block of pixels is **4 contiguous indices**: `blockId = pixelId >> 2`.
   - Under 8-connectivity, **all continental pixels inside a `2×2` block are guaranteed to belong to the same connected component**!
   - Therefore, the Union-Find parent forest shrinks by **4×**: at `order = 8` (`786,432` cells), there are only **`196,608` blocks per plate** (`786 KB` for `uint32_t BkeParent` per plate — **1.57 MB total for both colliding plates**, fitting 40× inside the 4080's 64 MB L2!).
2. **`16×16` Tile Shared-Memory Pre-Merge (`groupshared`):**
   - Every `16×16` pixel tile is `256` contiguous Morton indices = **`64` contiguous `2×2` blocks** (`8×8` blocks = 1 workgroup of `[numthreads(8, 8, 1)]`!).
   - Each thread reads 1 coalesced `uint` from `CrustFlagsPackedIn` (`4` contiguous bytes = its exact `2×2` block!), computes its 4-bit continental occupancy mask `m = (b0_cont) | (b1_cont<<1) | (b2_cont<<2) | (b3_cont<<3)` (where `m == 0` is ocean), and stores it in `groupshared uint s_Mask[64]` and `groupshared uint s_Parent[64]`.
   - Inside `groupshared`, the 64 threads run local `InterlockedMin` Union-Find over internal 8-connected block edges (zero global atomics!). This collapses **~95% of all continental pixels** into ~1–4 local roots per `16×16` tile before touching global memory!

#### Complete 4-Stage BKE Pipeline (Both `top` and `under` Plates in Parallel)

```
Stage 1: BkeTileInitAndLocalUnionCS   (Indirect: 2 * NumTiles_16x16 groups of 8x8 threads)
  - Each thread owns one 2x2 block inside a 16x16 HEALPix tile.
  - Loads 4 contiguous bytes (exact 2x2 Morton block!) -> 4-bit continental mask.
  - Runs tile-local groupshared Union-Find across internal 2x2 block neighbors:
      Block (bx, by) connects to:
        West  (bx-1, by):   if (mask & 0b0101) && (westMask & 0b1010)
        South (bx, by-1):   if (mask & 0b0011) && (southMask & 0b1100)
        SW    (bx-1, by-1): if (mask & 0b0001) && (swMask & 0b1000)
        NW    (bx-1, by+1): if (mask & 0b0100) && (nwMask & 0b0010)
  - Flattens s_Parent[local] -> writes global BkeParent[slot * N/4 + globalBlock]
    and BkeMask[slot * N/4 + globalBlock] (1 byte or 4-bit nibble).

Stage 2: BkeStitchSeamsCS             (Indirect: ONLY border blocks of 16x16 tiles!)
  - Only ~25% of blocks sit on a 16x16 tile border (bx == 0 || by == 0 || bx == 7 || by == 7).
  - Interior tile borders: direct global Lock-Free InterlockedMin Union with West/South/SW/NW neighbor block.
  - Base-face seam borders (ix == 0 || iy == 0 || ix == Nside-2 || iy == Nside-2):
      Use HealpixNeighbor() on the 2 border pixels of the 2x2 block -> find neighbor blockId = nbPix >> 2 -> Union!
      (Naturally handles all 12-face rotations, polar seams, and 24 7-neighbor corners with zero special tables.)

Stage 3: BkeFlattenCS                 (Indirect: 2 * (N/4) threads)
  - Path-compresses BkeParent[b] = FindRoot(b) so every continental 2x2 block points to its canonical root.

Stage 4: MarkSeeds + CountTerrane + DecideTransfer
  - MarkTerraneSeedsCS (N threads): collision front points set SeedFlag[BkeParent[slotBase + (cell >> 2)]] = 1.
  - CountTerraneCS (2 * (N/4) threads): if SeedFlag[root] == 1, add countbits(blockMask) via WaveActiveSum -> TerraneSize[slot].
  - DecideTransferCS (1 thread): smaller terrane = SourcePlate, larger = TargetPlate; compute SurgeRadiusKm and SurgeAmpKm.
```

**Why BKE Beats 1×1 Union-Find at `order = 8` (786k):**
- `1×1` Oliveira-Lotufo: `1.57M` nodes, `12+ MB` neighbor table, millions of global atomics.
- **HEALPix `2×2` BKE:** `393k` block nodes (`1.57 MB`), **0 MB neighbor table**, ~95% of merges resolved in `groupshared`, total CCL time **~25–40 µs** on RTX 4080!

### 3.3 Terrane Transfer, 1-Dispatch Tiled Suture Distance & Himalayan Surge
1. **`TransferTerraneDirectCS` (`N` threads over `TargetPlate`):**
   - Map target pixel `c` → world (`HealpixPixToVec` × `Rot[Target]`) → source pixel (`HealpixVecToPix` × `Rot[Source]^T`).
   - Check `inTerrane = (srcMask & (1 << (srcCell & 3))) && SeedFlag[BkeParent[srcSlot + (srcCell >> 2)]]`.
   - If `Target` cell `c` is already continental → **suture anchor**: compact `float4(platePos.xyz, 0)` into `SuturePoints[]` (`InterlockedAdd(SutureCount, 1)`), update `SutureCapCenter` / `SutureCapSpread`.
   - Else → copy `CrustFlags` & `CrustState` from `(Source, srcCell)` to `(Target, c)`, `InterlockedAdd(ReceivedCount, 1)`.
2. **`ClearSourceTerraneCS` (`N` threads over `SourcePlate`):** Clear `alive` & `continental` flags on transferred source terrane cells.
3. **`NearestSuturePointCS` (1 Single Dispatch — Replaces `cKDTree` & Eliminates Iterative Graph Relaxation):**
   - Uses our proven `NearestFrontPointCS` `groupshared` 64-point tiled dot-product kernel over `SuturePoints[]`!
   - Cells outside `SutureCapRadius + SurgeRadiusKm` early-out in 1 dot product (~85% of the globe skipped).
   - Writes exact geodesic distance `SutureDist[c]` (km) in **1 dispatch (~20–40 µs at 786k)** with bit-exact parity to Python's `cKDTree`.
4. **`EscapeHistogramCS` (`N` threads + 1 workgroup prefix sum):**
   - Computes `deficit = round(COLLISION_AREA_KEPT * (TerraneCount - ReceivedCount))`.
   - Bins free `TargetPlate` cells within `SurgeRadiusKm` into a 64-bin distance histogram (`WaveMatch`/`WaveActiveSum` aggregated) → 1-workgroup scan finds exact `EscapeRadiusKm` where cumulative free cells $\ge \text{deficit}$.
5. **`ApplyEscapeAndSurgeCS` (`N` threads over `TargetPlate`):**
   - Free cells with `SutureDist[c] <= EscapeRadiusKm` become new continental crust (`relief = 0.0f`).
   - Continental cells with `d = SutureDist[c] < SurgeRadiusKm` receive the Cortial 2019 surge:
     $$\text{surge} = \min(C_{\text{coll}} \cdot A, \; 4.0\text{ km}) \cdot \left(1 - \left(\frac{d}{R_{\text{surge}}}\right)^2\right)^2$$
     and when `surge > 0.1f`, tag `orogenyType = 2 (OROGENY_HIMALAYAN)`, `orogenyTime = Time`.

---

## 4. Software Sparse Virtual Plate Paging (Zero `VK_QUEUE_SPARSE_BINDING`)

### 4.1 Why Software Compute Paging Instead of Vulkan Sparse Binding?
Vulkan's legacy `VK_QUEUE_SPARSE_BINDING` requires:
- 64 KB hardware page granularity (far too coarse for small plates/island chains),
- CPU-side `vkQueueBindSparse` synchronization stalls between frames, and
- **Does not exist in WebGPU** (nor cleanly across mobile GPUs).

**Software Virtual Tile Paging** lives 100% in standard storage buffers (`StructuredBuffer` / `RWStructuredBuffer`), runs completely GPU-driven inside compute shaders, works identically on Vulkan + WebGPU, and uses fine-grained **`8×8` HEALPix Nested tiles (`64` contiguous pixels = 1 tile)**.

### 4.2 Memory Math: Slashing VRAM by ~10× (Keeping 786k Inside L2!)
At **`order = 8` (`786,432` cells/sphere, `P = 20` plates)**:
- Each `8×8` tile holds **64 cells** (`tileId = cell >> 6`, `localIdx = cell & 63`).
- `NumTilesPerPlate = 786,432 / 64 = 12,288`.
- **Dense v1 layout (`20 × 786,432`):**
  - `CrustFlags` (1 B): `15.7 MB`
  - `CrustState` (16 B): `251.7 MB`
  - `CellFronts` (8 B): `125.8 MB`
  - **Total = `393.2 MB`** (6.1× larger than the RTX 4080's 64 MB L2!).
- **Sparse Virtual Paged layout (`PoolCapacity = 24,576` physical tiles ≈ `2.0×` sphere coverage, generous slack for overlap/collision):**
  - `PageTable[20 × 12,288]` (`uint32_t`): **`0.98 MB`** (`0xFFFFFFFF` = unmapped/dead)
  - `TileLiveMask[24,576]` (`uint2` = 64-bit occupancy mask): **`0.20 MB`**
  - `TileFlagsPool[24,576 × 64]` (1 B/cell): **`1.57 MB`**
  - `TileCrustStatePool[24,576 × 64]` (16 B/cell): **`25.17 MB`**
  - `TileCellFrontsPool[24,576 × 64]` (8 B/cell): **`12.58 MB`**
  - **Total = `40.5 MB`** — **nearly 10× smaller**, and **100% L2-resident on an RTX 4080 at 786,432 cells!**

### 4.3 Lock-Free GPU Tile Allocator & Lifecycle

```hlsl
static const uint k_TileUnmapped = 0xFFFFFFFFu;
static const uint k_TilePending  = 0xFFFFFFFEu;

// Per-plate page table: [plate * NumTiles + tileId] -> physical tile index in [0, PoolCapacity)
Counters PageTable;
// Ring/stack of free physical tile IDs + atomic cursor
Words FreeTileStack;
Counters FreeTileTop; // initialized to PoolCapacity
// Reverse metadata for each physical tile (enables direct active-tile dispatch!)
// .x = plate, .y = logicalTileId, .z = liveMaskLo, .w = liveMaskHi
IntPoints PhysicalTileHeader;
```

1. **Read Path (`OwnersCS`, `RemoveSubductedCS`, `RenderMapCS`):**
   - `uint phys = PageTable.Load(plate * NumTiles + (cell >> 6u));`
   - `if (phys == k_TileUnmapped) return /* dead ocean! */;` (1 coalesced 4-byte load skips 64 dead cells!)
   - `uint flat = (phys << 6u) | (cell & 63u);` → index directly into `TileFlagsPool` / `TileCrustStatePool`.
   - **Bonus:** In `OwnersCS`, the 32 lanes of a HEALPix wave usually hit only **1–2 `8×8` tiles** per plate, so `PageTable` stays ~100% L1-cached!
2. **On-Demand Allocation (`FillGapsCS` & `TransferTerraneDirectCS`):**
   - When a thread needs to write a new cell in an unmapped tile `(plate, tileId)`:
     - Because multiple threads in the same dispatch might hit the same newly-opened ridge tile simultaneously, use a **2-phase allocation** (zero spinlocks, deadlock-free on every GPU/WebGPU!):
       - **Phase A (`RequestNewTilesCS` or inside `FillGapsCS`):** `InterlockedCompareExchange(PageTable[entry], k_TileUnmapped, k_TilePending, prev)`. Only the **single winning thread** (`prev == k_TileUnmapped`) pops `uint slot = InterlockedAdd(FreeTileTop, -1) - 1; uint phys = FreeTileStack[slot];` and appends `(plate, tileId, phys)` to a tiny `NewTilesList[]`.
       - **Phase B (`InitNewTilesCS`, indirect over `NewTileCount`):** 1 workgroup of 64 threads per new tile zeroes its 64 cells in `TileFlagsPool`/`TileCrustStatePool`, writes `PhysicalTileHeader[phys]`, and publishes `PageTable[entry] = phys` — then `FillGapsCS` writes the newly minted ridge crust!
   - *(Zero spinning across warps — critical for Forward-Progress-free GPU execution models like WebGPU/Metal!)*
3. **Zero-Waste Active-Tile Iteration (`LiveCells` Upgrade!):**
   - Instead of scanning `20 × 786,432 = 15.7M` cells in `CountLiveCellsCS`, scan only `ActiveTiles` (or `PoolCapacity = 24,576` physical tiles where `PhysicalTileHeader[phys].x >= 0`)!
   - Each 64-thread workgroup handles 1 physical tile (`64` cells): computes its 64-bit `liveMask`, updates `PlateCounters`, and **if `liveMask == 0` (entire tile subducted/transferred away!), immediately reaps the tile** (`PageTable[entry] = k_TileUnmapped`, pushes `phys` back onto `FreeTileStack`, sets `PhysicalTileHeader[phys].x = -1`)!
   - `CountLiveCellsCS` work drops by **~15×** at `order = 8`!

### 4.4 Future Hook: Regional Adaptive Resolution (AMR)
Once plates are paged by `8×8` tiles, supporting **multi-resolution regions** (e.g., `order = 8` quiet ocean + `order = 9/10` around active mantle hotspots, volcanic arcs, or collision sutures) fits cleanly into the exact same page table:
- Reserve the top 2 bits of `PageTable[entry]` as `subdivLevel ∈ {0, 1, 2}`:
  - `0` → leaf physical tile at base `order`.
  - `1` → pointer to a 4-child sub-table (`order + 1`), allocated from a small child-table pool.
- Only active plume swells (`ArnouldColtice2020`, $r_p \approx 80\text{ km}$) or high-curvature mountain belts trigger subdivision; quiet abyssal plains stay at base order.

### 4.5 Concrete Design Feedback for `RhiResources` & `RenderGraph` (`Loom`)
Implementing PlanetGen v2 directly informs four real engine requirements we inspected in [RhiResources/README.md](../../modules/RhiResources/README.md), [RhiResources/todo.md](../../modules/RhiResources/todo.md), and [RenderGraph/include/GraphBuilder.hpp](../../modules/RenderGraph/include/GraphBuilder.hpp):

1. **Transient Scratch Aliasing via `rhi::placement`:**
   - Collision buffers (`BkeParent`, `BkeMask`, `SutureDist`, `SuturePoints`, ~12–20 MB at `order = 8`) are purely transient and never live during `OwnersCS` or `RenderMapCS`.
   - `Loom` can allocate them as **placed buffers** (`rhi::placement::CreateMemoryHeap` + `CreatePlacedBuffer`) aliased against `RenderMapCS` / future erosion scratch heaps — testing `HeapInUse`, `Discard`, and non-overlapping lifetime packing on real workloads. (On WebGPU, `Loom` falls back to whole-buffer pool reuse as noted in `RhiResources/README.md:271`.)
2. **GPU-Driven Conditional Subgraphs in `Loom`:**
   - `loom::RunPolicy::OnTrigger` currently models CPU-triggered passes (`graph.Trigger(key)`). PlanetGen v2's `Collide` and `InitNewTilesCS` need **GPU-indirect subgraphs** (always recorded, gated by `VkDispatchIndirectCommand`, with `Loom` inserting proper `SHADER_WRITE -> INDIRECT_COMMAND_READ` barriers automatically).
3. **Poolable Command Pools & Sub-Range Copies in `TransferSystem` (`RhiResources/todo.md`):**
   - Streaming high-resolution HEALPix climate/erosion/DEM tiles (or reading back only dirty physical tiles for disk caching / `AgentTools` inspection) requires batched sub-buffer/sub-image region copies and reused `VkCommandPool`s in `TransferSystem` instead of single-use allocations.
4. **Queue Affinity Expansion (`QueueAffinity::Compute` / `Transfer`):**
   - [Declarations.hpp](../../modules/RenderGraph/include/Declarations.hpp#L39-L43) currently stubs `QueueAffinity` to `Graphics` only. Running background climate/erosion refinement (`Phase 2: Idle Refinement` in `Procedural_Planet_Generation_Reference.md`) concurrently with viewport rendering directly motivates async `Compute` + `Transfer` timeline handoffs (`rhi::TransferWait` + `RegisterConsumerTimeline`).

---

## 5. Unifying Across Future Simulation Systems (`DocParse/results` & Repo Cross-Check)

Switching PlanetGen's tectonic core to **Nested HEALPix** unifies every planned planetary subsystem onto **one single spherical discretization** — zero reprojection seams, zero area-distortion correction factors, and bit-shift parent/child alignment (`fine >> 2Δo`) across every scale:

| Future Subsystem | Reference Document | Grid / Solver Requirement | How Native Nested HEALPix Fits Seamlessly |
| :--- | :--- | :--- | :--- |
| **Macro-Climate & Wind ($\psi, \phi$)** | [Procedural_Planet_Generation_Reference.md](../../../DocParse/results/Procedural_Planet_Generation_Reference.md#L75-L117) (§3, §6.1, §10.1) | **Explicitly mandates HEALPix** $N_{\text{side}} = 16 \to 64$ (`order 4 → 6`) + Spherical Harmonics ($l \le 12$) over HEALPix iso-latitude rings (reusing `OceanFFT` subgroup Stockham 1D ring FFTs!) + 1D upwind IIR rain shadows; stored in a 12-layer `Texture2DArray`. | Tectonics (`order 7–8`) downsamples to climate (`order 4–6`) via exact `2×2` Morton box-averaging (`idx >> 2Δo`). Zero interpolation mismatch! |
| **Orographic Precipitation Downscaling** | [Brenner2026_GeoDS_OrographicDownscaling.md](../../../DocParse/results/Brenner2026_GeoDS_OrographicDownscaling.md#L70-L76) (Eq. 5) & [HergartenRobl2022_LFPM_OrographicPrecip.md](../../../DocParse/results/HergartenRobl2022_LFPM_OrographicPrecip.md) | GeoDS enforces exact mass conservation over each coarse cell $C$: $P_{\text{HR,final}} = P_{\text{HR}} \cdot \frac{P_{\text{CR}}(C) A_C}{\sum_{M \in C} P_{\text{HR}}(M) A_M}$. LFPM solves real-space advection-dispersion on 2D grids without FFT wrap-around. | Because HEALPix is **strictly equal-area** ($A_C = 4^{\Delta o} A_M$ identically everywhere), Eq. 5 reduces to a pure wave/workgroup mean over consecutive Morton indices — **zero area weights needed**. |
| **Global Hydrology & Endorheic Lakes** | [Gauvain2026_GlobalPlanetaryHydrology.md](../../../DocParse/results/Gauvain2026_GlobalPlanetaryHydrology.md) | Depression trees & precomputed hypsometric curves $(V, A, Z)$ over global DEM; watershed area $A_i^w$ and closed volatile GEL budget. | Equal-area cells mean catchment area $A_i^w$ is simply `cellCount * cellAreaKm2`, and Priority-Flood / basin-tree construction runs directly on the analytic 8-neighbor HEALPix graph (plus our BKE CCL reuses the exact same machinery for lake/basin labeling!). |
| **Analytical Multigrid Erosion** | [Tzathas2024_Analytical_Erosion.md](../../../DocParse/results/Tzathas2024_Analytical_Erosion.md#L94-L101) (§4.2) | 4–5 level **multigrid V-cycle** with $2\times 2$ box downsampling & bilinear upsampling for Stream Power Law + hillslope diffusion + thermal talus. | HEALPix Nested IS a native $2\times 2$ hierarchy (`order 8 → 7 → 6 → 5 → 4`) on 12 regular 2D faces — V-cycle restrict/prolongate are pure 4-element Morton reductions and intra-face bilinear ops! |
| **Mantle Plumes & Hotspots** | [ArnouldColtice2020_MantlePlumeTectonics.md](../../../DocParse/results/ArnouldColtice2020_MantlePlumeTectonics.md) | 15–35 active mobile plumes ($r_p \approx 80\text{ km}$, dynamic swell $W_s \approx 1000\text{ km}$), connected-component thermal anomaly tracking, plume-ridge interactions. | Runs on coarse HEALPix (`order 5–6`) using the same BKE CCL for plume tracking, injects dynamic swell uplift into tectonic plates, and acts as the natural refinement trigger for future regional AMR tiles. |
| **Coupled Ecology & Seasonal Texturing** | [Acevedo2023_AdaScape_TectonicsClimateBiodiversity.md](../../../DocParse/results/Acevedo2023_AdaScape_TectonicsClimateBiodiversity.md) & [Kanai2024_SeasonalTerrainKoppenConditioning.md](../../../DocParse/results/Kanai2024_SeasonalTerrainKoppenConditioning.md) | Continuous trait-space competition over spherical neighborhoods; Köppen periodic conditioning over overlapping 2D tiles. | Replaces AdaScape's planar Cartesian limitation (noted in §4.3) with equal-area spherical neighborhoods; HEALPix 12 faces act as 2D tileable canvases with `k_NbFace`-resolved border aprons. |
| **NoiseGraph Rasterizer & Diffusion** | [NoiseGrids.slang](../../assets/shaders/compute/Noise/NoiseGrids.slang), [Noise/README.md](../../assets/shaders/compute/Noise/README.md#L286), [Borg2026_TerrestrialPlanetsDiffusion.md](../../../DocParse/results/Borg2026_TerrestrialPlanetsDiffusion.md), [Sun2025_LDDEM_PlanetaryLatentDiffusion.md](../../../DocParse/results/Sun2025_LDDEM_PlanetaryLatentDiffusion.md) | `NoiseGrids.slang` explicitly plans a HEALPix grid backend alongside cube-face/lat-lon; diffusion models operate on equal-size 2D spherical patches with sine blending windows (`Borg2026` Eq. 7). | Sharing one `HealpixGrid` struct between `PlanetLab` and `NoiseGraph` lets `IField3` / `IOctaveRule` nodes (`Pseudokarst`, `AeolianYardang`, `StrataStepped`) evaluate directly on tectonic plates with zero coordinate glue. |

---

## 6. Aside: Path to 3D Meshing, Slang Autodiff & Progressive Detail

When we eventually move from 2D equirectangular `RenderMapCS` output to an interactive 3D planet viewer, **Native Nested HEALPix gives us a singularity-free 12-root quadtree mesh out of the box** — no polar pinching, no cube-corner stretching, and zero data reprojection. For an initial 3D orbit viewer, each of the 12 HEALPix base faces simply emits a regular displaced quad grid (or a quick icosphere/cubemap sampling `HealpixVecToPix`), and as camera distance drops, a GPU compute pass traverses the 12 quadtrees using **CDLOD / Mesh Shaders** (emitting `16×16` vertex diamond patches per visible quadtree leaf, with continuous vertex morphing or skirt stitching along adjacent-LOD borders to eliminate T-junctions).

Where **Slang `[Differentiable]` / `fwd_diff`** shines in this stack is bridging the coarse tectonic simulation (`order 7–8`, ~25–50 km) to fine multi-scale geometry without exploding VRAM:
1. **Coarse-to-Mid Meshing (Error-Driven Subdivision):** Forward-mode autodiff over our continuous elevation + erosion + `IOctaveRule` fields yields exact analytical surface normals $\mathbf{n}$ and mean curvature $H_c = \operatorname{div}_t(\mathbf{g})$ (or single-fetch `QuadReadAcross*` derivatives on materialized raster tiles per `Procedural_Planet_Generation_Reference.md` §10.2). High curvature along Himalayan sutures, trenches, and carved canyons drives quadtree subdivision density automatically while flat abyssal plains stay coarse.
2. **Progressive Detail Layering:** Coarse tectonic elevation (`order 8`) seeds **mid-scale analytical erosion** (`Tzathas2024` multigrid + `Cortial2020_PlanetHyperAmplification` valley carving down to ~500 m–50 m on visible leaf tiles), which in turn feeds **fine micro-detail** from fused `NoiseGraph` fractals (`IOctaveRule`) or patch-based latent diffusion (`Borg2026` / `Sun2025`) — while `fwd_diff` normals keep grazing-angle orbital lighting and close-up terrain shading crisp at every LOD.

---

## 7. Phased Implementation Roadmap & Coding Kickoff

### Phase 1 — Uniform Native Nested HEALPix (`order 7` 196k & `order 8` 786k)
*Goal: Complete, verified swap of the lattice math and benchmark `OwnersCS` + step speedup before adding new physics.*
1. **Host & Unit Tests (`applications/PlanetGen/gpu/Healpix.hpp/.cpp`):**
   - Add `HealpixPixToVec(uint32_t ipix, uint32_t order)` and `HealpixNeighbor(...)` alongside existing `HealpixNestedIndex`.
   - Add a fast C++ unit check in `PlanetGenCheck` (`--checks=healpix`): exhaustive bijection (`order 1..6`), float32 polar round-trip (`order 7..10`), and exhaustive neighbor symmetry (`0` asymmetries, `24` seven-neighbor corners).
2. **Shader Core (`assets/shaders/compute/Planet/Healpix.slang`):**
   - Implement `SpreadBits16`, `CompactBits16`, `HealpixPixToVec`, `HealpixVecToPix`, and `HealpixNeighbor` (exact code in §2.2).
   - Add `uint HealpixOrder` to `OwnersParams`.
3. **Port Passes 1–6 + Live Loop:**
   - Replace `LatticePoint` / `FibonacciNearest` across `Owners.slang`, `Crust.slang`, `LiveCells.slang`, `FrontPoints.slang`, `TorqueSums.slang`, and `RenderMap.slang` (adding intra-face bilinear filtering in `RenderMapCS`).
   - Add a procedural Voronoi HEALPix initial-state generator in `StepState.cpp` (20 plates with realistic continents, Euler poles, and initial bounding caps) so `PlanetGenBench` and `PlanetGenTimelapse` run natively at `order 6/7/8` with zero external reference files.
   - **Gate:** `PlanetGenCheck --checks=healpix` PASS + Full Vulkan validation clean + measure `OwnersCS` plates/wave & step time in `PlanetGenBench` (`order 7` 196k vs Fibonacci 100k, `order 8` 786k vs Fibonacci 500k).

### Phase 2 — Pure-GPU Continental Collision via Allegretti `2×2` BKE CCL (`Collide.slang`)
*Goal: Colliding continents and island rosettes dock, conserve area, and raise Himalayan mountain chains 100% on GPU.*
1. Implement `DetectCollisionsCS` + `SelectCollisionPairCS` + `CollideDispatch` indirect gating.
2. Implement 4-stage `2×2` BKE (`BkeTileInitAndLocalUnionCS`, `BkeStitchSeamsCS`, `BkeFlattenCS`, `MarkTerraneSeedsCS` / `CountTerraneCS` / `DecideTransferCS`).
   - **Unit Test Gate:** Compare GPU BKE connected components on synthetic multi-face continents (including polar-cap & 7-neighbor corner crossings!) against a 20-line CPU BFS reference in `PlanetGenCheck` — **must match 100% exact**.
3. Implement `TransferTerraneDirectCS`, `ClearSourceTerraneCS`, tiled `NearestSuturePointCS`, `EscapeHistogramCS`, and `ApplyEscapeAndSurgeCS`.
4. **Visual Gate:** Run a 300-frame `order 7/8` timelapse in `PlanetGenTimelapse` and verify the colliding island rosette & continents dock cleanly and build Himalayan chains (`OrogenyHimalayan = 2`).

### Phase 3 — Software Sparse Virtual Plate Paging & `RhiResources`/`Loom` Integration
*Goal: Break the `P × N` memory wall so `order 8` (786k) and `order 9` (3.1M) run inside L2 cache, and drive real `Loom` / `RhiResources` features.*
1. Introduce `PageTable`, `PhysicalTileHeader`, `FreeTileStack`, and `Tile*Pool` buffers (`8×8` tile = 64 cells) with 2-phase deadlock-free allocation in `FillGapsCS` / `TransferTerraneDirectCS` and automatic reaping in `CountLiveCellsCS`.
2. Benchmark `order 8` (786k) dense vs. paged (`UpliftCS` and `LiveCells` expected 5×–10× faster, total VRAM ~40 MB vs ~393 MB).
3. Port `PlanetStep` + `Collide` + `RenderMapCS` onto `modules/RenderGraph` (`loom::GraphBuilder`), aliasing transient collision/render scratch buffers via `rhi::placement` and surfacing indirect-subgraph & transfer-pool requirements back into `Loom` / `RhiResources`.

---

### Ready to Code Tonight (Phase 1, Step 1 & 2)
When we pick back up, the cleanest first coding session is:
1. Type `assets/shaders/compute/Planet/Healpix.slang` (from §2.2 — ~120 lines of pure, self-contained 32-bit Slang).
2. Extend `applications/PlanetGen/gpu/Healpix.hpp/.cpp` with `HealpixPixToVec` + `HealpixNeighbor` and run the exhaustive bijection/symmetry test in `PlanetGenCheck`.
3. Wire `Healpix.slang` into `Owners.slang` and benchmark our first native Nested HEALPix `order 7` (196k) step!
