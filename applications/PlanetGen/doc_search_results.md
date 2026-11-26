Here is the focused **$\pm 2$ forward/backward citation triage** for **[planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md)**, cross-checked against all **95** already-converted documents in [`C:\SoftwareDev\DocParse\results\`](file:///C:/SoftwareDev/DocParse/results) (plus the `ocean/` and `our_machinery/` bundles) and the **115** PDFs in [`C:\SoftwareDev\DocParse\inputs\`](file:///C:/SoftwareDev/DocParse/inputs).

---

### 0. Quick Scan of Local Anthology PDFs (`GPU Pro 5`, `GPU Pro 6`, `GPU Pro 7`)

The repository houses **[GPU Pro 5 (2014)](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel%20-%20GPU%20Pro%205_%20Advanced%20Rendering%20Techniques%20(2014,%20A%20K%20Peters_CRC%20Press).pdf)**, **[GPU Pro 6 (2015)](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel%20-%20GPU%20Pro%206_%20Advanced%20Rendering%20Techniques%20(2015,%20CRC%20Press).pdf)**, and **[GPU Pro 7 (2016)](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel-GPU%20Pro%207_%20Advanced%20Rendering%20Techniques-A%20K%20Peters_CRC%20Press%20(2016).pdf)** (tracked as `DOC-26` in [CHECKPOINT.md](file:///C:/SoftwareDev/DocParse/CHECKPOINT.md#L45)). Scanning their full tables of contents surfaced **three must-extract chapters** (bundled as **Rank #7** in Tier 1 below) and **two secondary chapters**:

| Volume & Chapter | Author(s) | Book Pages (0-Indexed PDF Pages) | Relevance to `PlanetGen v2` |
| :--- | :--- | :---: | :--- |
| **`GPU Pro 7` Ch. VI.2** — *Interactive Sparse Eulerian Fluid* | Alex Dunn | pp. 275–298 (`286–309`) | **Tier 1 (`DOC-26a`):** §2.6 (*Latency Resistant Sparse Fluid Simulation*) implements a 100% compute-driven **software page table + free-brick stack + active-brick `DispatchIndirect` pipeline** with zero CPU readback and zero hardware sparse binding. |
| **`GPU Pro 7` Ch. III.1** — *Adaptive Virtual Textures* | Ka Chen (Ubisoft) | pp. 131–144 (`142–155`) | **Tier 1 (`DOC-26b`):** Software virtual-texture page-table indirection, physical tile pool caching, **1–2 texel border apron padding** for bilinear filtering across packed physical tiles, and coarse-parent fallback for regional AMR ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L405) §4.4). |
| **`GPU Pro 5` Ch. VI.3** — *Quadtrees on the GPU* | Jonathan Dupuy, J.-C. Iehl, P. Poulin | pp. 439–450 (`452–463`) | **Tier 1 (`DOC-26c`):** Direct `-1 hop` ancestor to Dupuy (2020) CBT; packs a linear Morton quadtree node (level + Z-index) into a single 32-bit `uint` and updates split/merge LOD in 1 ping-pong compute pass + `DrawIndirect`. |
| **`GPU Pro 6` Ch. VII.1** — *Block-Wise Linear Binary Grids for Fast Ray-Casting Operations* | Holger Gruen | pp. 489–503 (`502–516`) | **Tier 2 Note:** Stores 64-cell blocks as a 64-bit occupancy bitmask (`uint2`, identical to our `8×8` `TileLiveMask` in [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L367) §4.2) in block-linear memory layout for single-load L1 occupancy checks. |
| **`GPU Pro 6` Ch. I.1 & II.5** — *Dynamic GPU Terrain* / *Sparse Procedural Volume Rendering* | David Pangerl / Doug McNabb | pp. 3–17 / pp. 167–180 | **Tier 3 (Skip):** Rely on hardware DX11 tessellation factors and CPU-managed frustum ring buffers; superseded by compute CBTs / Mesh Shaders. |

---

### 1. Top-10 Ranked Triage Table ( Plus Tier 3 Rejections )

| Rank | Tier | Paper / Source | Citation Hop | Core Target in `PlanetGen v2` | Open-Access / Local Status |
| :---: | :---: | :--- | :--- | :--- | :--- |
| **#1** | **Tier 1** *(Must-Convert)* | **Karlbauer et al. (2024)** — *Advancing Parsimonious Deep Learning Weather Prediction using the HEALPix Mesh* (*JAMES* 16) | `+2` from Górski (2005); `+1` from Calabretta (2007) | **`HEALPixPad` 12-Face GPU Halo Exchange:** Cross-seam orientation flips/rotations & 24 degree-3 corner interpolation bridge ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L192) §2.2, §2.3, §5) | Open Access (`arXiv:2311.06253` / AGU Gold OA) |
| **#2** | **Tier 1** *(Must-Convert)* | **Bolelli, Allegretti, Lumetti & Grana (2024)** + **Allegretti, Bolelli & Grana (2020)** — *Connected Components Labeling on GPUs* (*IEEE TPDS* 35 & 31) | `Seed 2` (`0 hop`) + `+1` from Allegretti (2020) | **`Collide.slang` BKE + Subgroup CCL:** `2×2` block masks, 4-neighbor block sufficiency, **Inline Compression (`IC`)**, and **Warp-Level Merge (`WLP`)** ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L277) §3.2, §5) | Open Access PDFs via UNIMORE IRIS (`11380/1344445` & `11380/1179616`) |
| **#3** | **Tier 1** *(Must-Convert)* | **Benyoub & Dupuy (2024)** + **Dupuy (2020)** — *Concurrent Binary Trees for Large-Scale Game Components* (*PACMCGIT / HPG* 2024 & 2020) | `+1` from Dupuy (2020) (`Seed 1c`); `+2` from `GPU Pro 5` Ch. VI.3 | **Planetary 12-Face CBT & GPU Bisector Pool Manager:** Extends CBT from flat squares to arbitrary base meshes + lock-free GPU bitfield memory pool allocator ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L443) §6, §4.3) | Open Access (`arXiv:2407.02215` + Author PDF) |
| **#4** | **Tier 1** *(Must-Convert)* | **Westerteiger, Gerndt & Hamann (2012 / VLUDS 2011)** — *Spherical Terrain Rendering using the hierarchical HEALPix grid* (*OASIcs* 27) | `Seed 1a` (`0 hop`, not yet in `results/`) | **12-Root HEALPix Quadtree Terrain LOD & Seam Stitching:** Spherical geometric error metric, diamond patch skirts/stitching, and DEM+imagery fusion ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L443) §6, §2.3) | Open Access (Dagstuhl DROPS CC-BY) |
| **#5** | **Tier 1** *(Must-Convert)* | **Cheng, Subramaniam, Wu & Brenowitz (2025)** — *cuHPX: GPU-Accelerated Differentiable Spherical Harmonic Transforms on HEALPix Grids* (NVIDIA Research) | `+2` from Górski (2005); `+1` from Karlbauer (2024) | **GPU `NESTED` $\leftrightarrow$ `xyf` $\leftrightarrow$ `RING` Shuffling, Bilinear Regridding & Differentiable Ring SHTs:** Bridges `order 7–8` Nested tectonics to `order 4–6` ring SH macro-climate ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L433) §2.2, §5, §6) | Open Access (`arXiv:2510.01785`) |
| **#6** | **Tier 1** *(Must-Convert)* | **Calabretta & Roukema (2007)** — *Mapping on the HEALPix grid* (*MNRAS* 381) | `-1` from Westerteiger (2011) & Fernique (2015) | **Closed-Form Hybrid Collignon–Cylindrical Jacobian $J(\theta,\phi)$:** Continuous sub-pixel $(x_f,y_f)$ coordinates & metric distortion tensor across $\|z\|=2/3$ ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L81) §2.2, §2.3, §5, §6) | Open Access (`arXiv:astro-ph/0412607`) |
| **#7** | **Tier 1** *(Must-Convert)* | **Local `GPU Pro` Bundle (`DOC-26`):** **Dunn (2016)** `GPU Pro 7` VI.2 + **Chen (2016)** `GPU Pro 7` III.1 + **Dupuy et al. (2014)** `GPU Pro 5` VI.3 | `Seed 3` (`0 hop`) & `-1` from Dupuy (2020) | **Software Compute Tile Pool, Page-Table Indirection, 2-Phase Eviction & 32-Bit Linear GPU Quadtrees** ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L345) §4.2–§4.4, §6) | **Already in [`inputs/`](file:///C:/SoftwareDev/DocParse/inputs)** (extract via `--page_range`) |
| **#8** | **Tier 2** *(1-Technique Ref)* | **Fernique et al. (2015)** *HiPS* (*A&A* 578, `arXiv:1505.02291`) + **Baumann et al. (2022)** *Aladin Lite v3* (*ADASS XXX*) | `Seed 1b` (`0 hop`) + `+1` forward | **Fixed-Shift Tile Indexing ($\text{ipix} = \text{tile}\cdot 4^{\Delta k} + \text{sub}$) & Polar-Boundary Sub-Mesh UV Splitting:** Subdivide coarse tile meshes $2\text{–}3\times$ across $\|z\|=2/3$ in vertex/mesh shaders to prevent nonlinear Collignon UV swimming. | No full conversion needed (1 rule for §4.1 & §6) |
| **#9** | **Tier 2** *(1-Technique Ref)* | **Playne & Hawick (2018)** — *A New Algorithm for Parallel Connected-Component Labelling on GPUs* (*IEEE TPDS* 29) | `-1` from Allegretti et al. (2020) (`Seed 2`) | **Single-Read Pointer Jumping (`parent[b] = parent[parent[b]]`) in `BkeFlattenCS`:** Resolves $>98\%$ of tile-stitched trees without a `while` loop when inter-tile component diameter is small. | No full conversion needed (1-line optimization for §3.2 Stage 3) |
| **#10** | **Tier 2** *(1-Technique Ref)* | **Museth (2021)** *NanoVDB* (*SIGGRAPH 2021*) + **Williams et al. (2024)** *fVDB* (*ACM TOG* 43, `arXiv:2407.01781`) | `Seed 3` (`0/+1 hop`) | **Per-Thread Register Read-Aside Accessor Cache (`cachedTileId`, `cachedPhysSlot`):** Cache the last `8×8` tile lookup in thread registers during `OwnersCS` and upwind IIR walks so consecutive hits inside the same `8×8` tile skip `PageTable.Load()`. | No full conversion needed (register pattern for §4.3) |

#### Tier 3 (Skip / Redundant — Checked & Rejected to Prevent Re-Triaging)
- **Jain, Kerbl, Gain, Finley & Cordonnier (2024)**, *"FastFlow: GPU Acceleration of Flow and Depression Routing for Landscape Simulation"* (`+1` from `Tzathas2024` / `Barnes2019`): **Already converted** in [Jain2024_FastFlow_GPUFlowRouting.md](file:///C:/SoftwareDev/DocParse/results/Jain2024_FastFlow_GPUFlowRouting.md) (`DOC-41`).
- **Schott et al. (2023, 2024)**, **Cordonnier et al. (2019)**, & **Gailleton et al. (2024 `GraphFlood` / `CHONK`)**: **Already converted** in `results/` (`DOC-42`, `DOC-46`, `DOC-47`, `DOC-51`, `DOC-52`).
- **Youngren, Keen &זור (2017)**, *"A multi-resolution HEALPix data structure for spherically mapped point data"* (`+1` from Westerteiger 2011 & Fernique 2015): **Excluded per prompt** (already in `inputs/`; CPU point-cloud binary-search MLQ).
- **Krachmalnicoff & Tomasi (2019)**, *"Convolutional neural networks on the HEALPix sphere"* (*A&A* 628, `arXiv:1902.04083`, `-1` from Karlbauer 2024): **Skip** — allocates an explicit $9 \times N_{\text{pix}}$ neighbor index gather buffer in global memory (36 B/cell overhead) for CMB 1D strided convolutions; strictly inferior to our zero-buffer analytic `HealpixNeighbor` ([planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L192) §2.2) + Karlbauer (2024) tile-apron padding.
- **Perraudin et al. (2019)**, *"DeepSphere: Efficient spherical CNN with HEALPix sampling"* (*Astron. Comput.* 27, `arXiv:1810.12186`): **Excluded** — treats HEALPix as an unstructured sparse graph Laplacian $L = D - W$ for CMB power spectra, discarding 2D Morton quadtree regularity.
- **Górski et al. (2005)**, *"HEALPix: A Framework for High-Resolution Discretization..."* (*ApJ* 622, `-1` from Westerteiger 2011 & Fernique 2015): **Redundant** — discrete indexing and ring/nested math are already synthesized in [Procedural_Planet_Generation_Reference.md](file:///C:/SoftwareDev/DocParse/results/Procedural_Planet_Generation_Reference.md) and machine-verified in [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L49) §2.2; **Calabretta & Roukema (2007)** provides the missing continuous Jacobian math.
- **Kazhdan & Hoppe (2019)**, *"An Adaptive Multigrid Solver for Applications in Computer Graphics"* (*CGF* 38(1)): **Skip** — CPU B-spline finite-element Galerkin octree solver for 3D point-cloud surface reconstruction; poor fit for 32-bit WebGPU compute shaders compared to matrix-free $2\times 2$ Morton V-cycles ([Tzathas2024_Analytical_Erosion.md](file:///C:/SoftwareDev/DocParse/results/Tzathas2024_Analytical_Erosion.md) + Karlbauer 2024 face aprons).
- **Hu et al. (2019)**, *"Taichi: A Language for High-Performance Computation on Spatially Sparse Data Structures"* (*ACM TOG* 38(6)): **Skip full conversion** — focuses on LLVM/CUDA compiler IR (`SNode` code generation) rather than portable Vulkan/WGSL compute shaders (covered directly by `DOC-26` Dunn 2016 + Chen 2016).

---

### 2. Detailed Breakdown of Tier 1 Papers (Must-Convert via DocParse)

#### #1. Karlbauer et al. (2024) — Cross-Face GPU Halo Padding (`HEALPixPad`)
- **Citation & Year:** Matthias Karlbauer, Nathaniel Cresswell-Clay, Dale R. Durran, Raul A. Moreno, Thorsten Kurth, Boris Bonev, Noah Brenowitz, & Martin V. Butz (2024), *"Advancing Parsimonious Deep Learning Weather Prediction using the HEALPix Mesh"*, *Journal of Advances in Modeling Earth Systems (JAMES)*, Vol. 16, No. 8, e2023MS004027.
- **DOI / arXiv / PDF URL:** `doi:10.1029/2023MS004027` | `arXiv:2311.06253` | PDF: https://arxiv.org/pdf/2311.06253.pdf
- **Citation Hop:** `+2 from Górski et al. (2005)` (via `+1` Krachmalnicoff & Tomasi 2019); `+1 from Calabretta & Roukema (2007)`.
- **Exact Technique / Algorithm:**
  - **`HEALPixPad` Custom GPU Halo-Exchange Kernel (§3.1 & Appendix A):** Models the sphere as 12 regular 2D face buffers `[12, N_side, N_side]` (or $8\times 8$ / $16\times 16$ tiles with a $k$-pixel halo apron) and specifies the exact 9-direction (`N, S, E, W, NE, NW, SE, SW`) cross-face copy and coordinate rotation/transposition rules when pulling apron texels from adjacent base faces.
  - **24 Seven-Neighbor Corner Interpolation Bridge:** At the 8 degree-3 vertices of the HEALPix rhombic dodecahedron (the 4 North polar-cap corners and 4 South polar-cap corners, affecting 24 pixels total), one diagonal neighbor (`N`/`S` or `E`/`W` in local face axes) does not exist (`k_NbFace == -1` in [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L200)). Karlbauer et al. define a symmetric 2-neighbor average across the two flanking cardinal edge pixels to populate the missing corner slot of a $3\times 3$ stencil so standard 2D $3\times 3$ convolutions, bilinear/bicubic filters, and finite-difference Laplacians execute uniformly across all 12 faces with **zero branch divergence inside the inner stencil kernel**.
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§2.2 (`Healpix.slang`):** Adds a branch-free `HealpixStencil3x3` / `HealpixPadTileApron` helper for the 24 7-neighbor corner pixels.
  - **§2.3 (`RenderMapCS`):** Eliminates seam artifacts when upgrading `RenderMapCS` to cross-face 2D bilinear/bicubic interpolation.
  - **§5 (`Analytical Multigrid Erosion`, `Orographic Downscaling`, `Coupled Ecology`):** Enables shared-memory apron loading (`18×18` apron for `16×16` tiles, or `10×10` apron for `8×8` paged tiles) across all 12 face boundaries for `Tzathas2024` multigrid V-cycles and `LFPM`/`GeoDS` moisture advection.

---

#### #2. Bolelli, Allegretti, Lumetti & Grana (2024) + Allegretti, Bolelli & Grana (2020) — GPU `2×2` BKE & Subgroup CCL
- **Citation & Year:**
  1. *(Primary Review + Modern Kernels)* Federico Bolelli, Stefano Allegretti, Luca Lumetti, & Costantino Grana (2024), *"A State-of-the-Art Review With Code About Connected Components Labeling on GPUs"*, *IEEE Transactions on Parallel and Distributed Systems (TPDS)*, Vol. 35, No. 4, pp. 885–904.
  2. *(Foundational BKE/BUF Paper)* Stefano Allegretti, Federico Bolelli, & Costantino Grana (2020), *"Optimized Block-Based Algorithms to Label Connected Components on GPUs"*, *IEEE Transactions on Parallel and Distributed Systems (TPDS)*, Vol. 31, No. 2, pp. 423–438.
- **DOI / arXiv / PDF URL:**
  - 2024 Paper: `doi:10.1109/TPDS.2024.3434357` | Open-Access PDF (UNIMORE IRIS): https://hdl.handle.net/11380/1344445
  - 2020 Paper: `doi:10.1109/TPDS.2019.2917711` | Open-Access PDF (UNIMORE IRIS): https://iris.unimore.it/retrieve/handle/11380/1179616/225393/2018_TPDS_Optimized_Block_Based_Algorithms_to_Label_Connected_Components_on_GPUs.pdf
- **Citation Hop:** `Seed 2` (`0 hop`, Allegretti 2020) and `+1 from Allegretti (2020)` (Bolelli 2024).
- **Exact Technique / Algorithm:**
  - **Bitmask Komura Equivalence (`BKE`) & 4-Block Half-Neighborhood Sufficiency:** Encodes each $2\times 2$ block's connectivity to its 4 causal $2\times 2$ neighbor blocks (`P, Q, R, S` = `NW, North, NE, West`) into an 8-bit state word (4 bits foreground occupancy + 4 bits neighbor-merge flags), proving that internal $2\times 2$ pixels never require separate union nodes under 8-connectivity.
  - **Inline Compression (`IC`) & Warp/Subgroup-Level Merge (`WLP`):** Before issuing shared-memory or global `InterlockedMin` union operations, threads within a 32-lane warp/subgroup compress parent chains inline (`FindAndCompress`) and aggregate identical root merge targets across lanes using subgroup intrinsics (`WaveActiveBitOr` / `WaveActiveMin`), cutting atomic contention on dense continental interiors by $4\times\text{–}8\times$.
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§3.2 (`Collide.slang` — `BkeTileInitAndLocalUnionCS`, `BkeStitchSeamsCS`, `BkeFlattenCS`):** Upgrades the 4-stage BKE pipeline with the exact 8-bit `BKE` merge bitmask (storing both pixel occupancy and causal block connectivity in 1 byte per $2\times 2$ block) plus subgroup-deduplicated seam merges.
  - **§5 (`Global Hydrology & Endorheic Lakes`, `Mantle Plumes & Hotspots`):** Reuses the exact same BKE kernel to label closed endorheic depression basins (`Gauvain2026`) and mobile thermal plume anomalies (`ArnouldColtice2020`).

---

#### #3. Benyoub & Dupuy (2024) + Dupuy (2020) — Planetary 12-Face CBT & GPU Bisector Pool Manager
- **Citation & Year:**
  1. Anis Benyoub & Jonathan Dupuy (2024), *"Concurrent Binary Trees for Large-Scale Game Components"*, *Proceedings of the ACM on Computer Graphics and Interactive Techniques (PACMCGIT / HPG 2024)*, Vol. 7, No. 3, Article 41.
  2. Jonathan Dupuy (2020), *"Concurrent Binary Trees (with application to longest edge bisection)"*, *PACMCGIT (HPG 2020)*, Vol. 3, No. 2, Article 21.
- **DOI / arXiv / PDF URL:**
  - 2024 Paper: `doi:10.1145/3675389` | `arXiv:2407.02215` | PDF: https://arxiv.org/pdf/2407.02215.pdf
  - 2020 Paper: `doi:10.1145/3406186`
- **Citation Hop:** `+1 from Dupuy (2020)` (`Seed 1c`); `+2 from Dupuy, Iehl & Poulin (2014)` (Local `GPU Pro 5` Ch. VI.3).
- **Exact Technique / Algorithm:**
  - **Halfedge Bisector CBT on Arbitrary Base Polygon Meshes:** Eliminates the flat-square-domain limitation of Dupuy (2020) by pairing a **Concurrent Binary Tree (CBT)** with a compact GPU halfedge "bisector" table over any coarse base mesh—allowing our **12 HEALPix base diamond faces** (24 base triangles) to subdivide and decimate adaptively from orbital view down to sub-meter ground level in a single unified representation with **zero T-junction cracks** (twin halfedge pointer updates enforce conforming edges across all 12 face seams automatically).
  - **CBT as a Lock-Free GPU Memory Pool Manager:** Instead of indexing an exponential virtual binary heap (which overflows 32-bit integers past depth 31), Benyoub & Dupuy use the CBT bitfield sum-reduction tree as a **constant-size GPU slab allocator** (`0` = allocated slot, `1` = free slot), enabling thousands of compute threads to allocate and free subdivision nodes (or physical tiles!) in parallel in $< 0.2\text{ ms}$ with zero spinlocks and zero atomic contention hotspots.
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§6 (`Path to 3D Meshing, Slang Autodiff & Progressive Detail`):** Provides the exact crack-free 3D spherical mesh-shader subdivision pipeline over the 12 HEALPix base faces, driven by Slang `fwd_diff` mean curvature $H_c$.
  - **§4.3 & §4.4 (`Lock-Free GPU Tile Allocator` & `Regional Adaptive Resolution (AMR)`):** Offers a contention-free bitfield sum-reduction tree alternative to the single-counter `FreeTileTop` atomic stack when scaling to high-frequency AMR tile allocation/deallocation.

---

#### #4. Westerteiger, Gerndt & Hamann (2012 / VLUDS 2011) — 12-Root HEALPix Quadtree Terrain Rendering
- **Citation & Year:** Rolf Westerteiger, Andreas Gerndt, & Bernd Hamann (2012), *"Spherical Terrain Rendering using the hierarchical HEALPix grid"*, *Proceedings of IRTG 1131 Workshop (VLUDS 2011), OpenAccess Series in Informatics (OASIcs)*, Vol. 27, pp. 13–23.
- **DOI / arXiv / PDF URL:** `doi:10.4230/OASIcs.VLUDS.2011.13` | PDF: https://drops.dagstuhl.de/storage/01oasics/oasics-vol027-vluds2012-irtg1131/OASIcs.VLUDS.2011.13/OASIcs.VLUDS.2011.13.pdf
- **Citation Hop:** `Seed 1a` (`0 hop`, not yet in `DocParse/inputs/` or `results/`).
- **Exact Technique / Algorithm:**
  - **12-Forest HEALPix Spherical Quadtree & Screen-Space Geometric Error Metric:** Formulates a 12-root quadtree over the base HEALPix diamonds where each quadtree node represents an $M \times M$ elevation + color tile; derives the view-dependent bounding-sphere/horizon-occlusion test and screen-space chord error metric exploiting HEALPix's uniform solid angle $\Omega_{\text{pix}} = \frac{\pi}{3 N_{\text{side}}^2}$.
  - **Multi-Resolution DEM + Texture Fusion & Cross-Seam Skirt/Edge Stitching:** Handles decoupled DEM resolution vs. texture resolution (sampling coarse ancestor DEM tiles with scale/bias UV transforms when fine elevation tiles are unmapped) and specifies vertex alignment across the polar-to-equatorial transition ($|z| = 2/3$) and the 8 degree-3 corners.
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§6 (`Path to 3D Meshing, Slang Autodiff & Progressive Detail`):** Direct architectural blueprint for the 12-root HEALPix quadtree CDLOD / Mesh Shader (`16×16` diamond patch) renderer, horizon culling, and coarse-to-fine DEM tile fallback.
  - **§2.3 (`RenderMapCS`) & §4.4 (`Regional AMR`):** Supplies the exact scale-and-bias UV transform `uv_parent = 0.5 * uv_child + 0.5 * float2(childMorton & 1, (childMorton >> 1) & 1)` for sampling coarser parent tiles when an AMR leaf tile is unmapped.

---

#### #5. Cheng, Subramaniam, Wu & Brenowitz (2025) — `cuHPX`: GPU HEALPix Layouts, Bilinear Regridding & Differentiable Ring SHTs
- **Citation & Year:** Xiaopo Cheng, Akshay Subramaniam, Shixun Wu, & Noah Brenowitz (2025), *"cuHPX: GPU-Accelerated Differentiable Spherical Harmonic Transforms on HEALPix Grids"*, NVIDIA Research Preprint (`NVlabs/cuHPX`).
- **DOI / arXiv / PDF URL:** `doi:10.48550/arXiv.2510.01785` | `arXiv:2510.01785` | PDF: https://arxiv.org/pdf/2510.01785.pdf
- **Citation Hop:** `+2 from Górski et al. (2005)`; `+1 from Karlbauer et al. (2024)`.
- **Exact Technique / Algorithm:**
  - **Warp-Coalesced GPU `NESTED` $\leftrightarrow$ `xyf` $\leftrightarrow$ `RING` Remapping Kernels:** Uses intermediate packed `(ix, iy, face)` (`xyf`) register coordinates and shared-memory staging to convert between 2D Morton `NESTED` layout, 12-face flat 2D arrays, and iso-latitude `RING` layout at full HBM bandwidth without uncoalesced random global scatters.
  - **GPU Differentiable Spherical Harmonic Transforms (SHT) & Bilinear Spherical Regridding:** Couples batched 1D ring FFTs over the $4 N_{\text{side}} - 1$ iso-latitude HEALPix rings with recurrence-stabilized Associated Legendre Polynomials $P_l^m(\cos\theta)$ and iterative ring quadrature weights, plus adjoint/forward-differentiable bilinear interpolation weights across arbitrary spherical query directions.
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§5 (`Macro-Climate & Wind`):** Directly implements the GPU bridge between our `order 7–8` `NESTED` tectonic grid and the `order 4–6` iso-latitude `RING` Spherical Harmonics ($l \le 12$) + `OceanFft` subgroup Stockham 1D ring FFTs mandated by [Procedural_Planet_Generation_Reference.md](file:///C:/SoftwareDev/DocParse/results/Procedural_Planet_Generation_Reference.md#L75-L117).
  - **§2.2 & §6 (`Healpix.slang` & `Slang Autodiff`):** Provides differentiable forward/adjoint HEALPix bilinear interpolation and spectral operators compatible with Slang `[Differentiable]`.

---

#### #6. Calabretta & Roukema (2007) — Exact Continuous HEALPix Projection & Metric Jacobian
- **Citation & Year:** Mark R. Calabretta & Boudewijn F. Roukema (2007), *"Mapping on the HEALPix grid"*, *Monthly Notices of the Royal Astronomical Society (MNRAS)*, Vol. 381, No. 2, pp. 865–872.
- **DOI / arXiv / PDF URL:** `doi:10.1111/j.1365-2966.2007.12297.x` | `arXiv:astro-ph/0412607` | PDF: https://arxiv.org/pdf/astro-ph/0412607.pdf
- **Citation Hop:** `-1 from Westerteiger et al. (2011)` (`Seed 1a`) and `-1 from Fernique et al. (2015)` (`Seed 1b`).
- **Exact Technique / Algorithm:**
  - **Continuous Hybrid Collignon–Cylindrical Equal-Area Map & Metric Jacobian $J(\theta, \phi)$:** Whereas Górski et al. (2005) define only discrete integer pixel centers (`jr`, `jp`), Calabretta & Roukema derive the **exact continuous forward and inverse analytical equations** mapping any unit vector $(\theta, \phi)$ to continuous fractional intra-face coordinates $(u, v) \in [0, N_{\text{side}})^2$ within its diamond face, together with the exact $2\times 2$ Jacobian matrix $J = \frac{\partial (u, v)}{\partial (\theta, \phi)}$ (whose determinant is constant everywhere by equal-area conservation, $\det J = \text{const}$, while its shear/aspect eigenvalues quantify the local anisotropy of finite-difference stencils near the poles).
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§2.2 & §2.3 (`Healpix.slang` & `RenderMapCS`):** Modifies `HealpixVecToPix` (`HealpixVecToFaceUV`) to output the exact sub-pixel fractional offset `frac2 = float2(frac(u), frac(v))` at **zero extra trigonometric cost** (directly from the intermediate `along - across` and `tp * tmp` floats already computed on lines `150–172` of [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md#L150-L172)!), unlocking exact 4-tap 2D bilinear interpolation in `RenderMapCS` and `TransferTerraneDirectCS`.
  - **§5 & §6 (`Analytical Multigrid Erosion` & `Slang Autodiff`):** Provides the exact metric tensor $g^{ij} = (J J^T)_{ij}$ so finite-difference gradients $\nabla_S h$ and Laplacians $\Delta_S h$ in `Tzathas2024` multigrid erosion remain distortion-free across the polar-cap boundary $|z| = 2/3$.

---

#### #7. Local `GPU Pro` Anthology Bundle (`DOC-26`: Dunn 2016, Chen 2016, & Dupuy et al. 2014)
- **Citations, Local Paths & Exact Page Ranges to Extract:**
  1. **Alex Dunn (2016)**, *"Interactive Sparse Eulerian Fluid"*, in [Wolfgang Engel-GPU Pro 7_ Advanced Rendering Techniques-A K Peters_CRC Press (2016).pdf](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel-GPU%20Pro%207_%20Advanced%20Rendering%20Techniques-A%20K%20Peters_CRC%20Press%20(2016).pdf), Book pp. 275–298 (**0-indexed PDF pages `286–309`**).
  2. **Ka Chen (2016)**, *"Adaptive Virtual Textures"*, in [Wolfgang Engel-GPU Pro 7_ Advanced Rendering Techniques-A K Peters_CRC Press (2016).pdf](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel-GPU%20Pro%207_%20Advanced%20Rendering%20Techniques-A%20K%20Peters_CRC%20Press%20(2016).pdf), Book pp. 131–144 (**0-indexed PDF pages `142–155`**).
  3. **Jonathan Dupuy, Jean-Claude Iehl, & Pierre Poulin (2014)**, *"Quadtrees on the GPU"*, in [Wolfgang Engel - GPU Pro 5_ Advanced Rendering Techniques (2014, A K Peters_CRC Press).pdf](file:///C:/SoftwareDev/DocParse/inputs/Wolfgang%20Engel%20-%20GPU%20Pro%205_%20Advanced%20Rendering%20Techniques%20(2014,%20A%20K%20Peters_CRC%20Press).pdf), Book pp. 439–450 (**0-indexed PDF pages `452–463`**).
- **Citation Hop:** Local Repository (`0 hop` for Seed 3; `-1 from Dupuy 2020` for Seed 1c).
- **Exact Technique / Algorithm:**
  - **Dunn (2016) Latency-Resistant Compute Tile Paging:** Implements an end-to-end GPU-resident sparse brick allocator (`PageTable` + `FreeBrickList` + `ActiveBrickList` + `DispatchIndirect`) where simulation passes dispatch exclusively over `ActiveBrickCount` workgroups, halo transfers resolve via `PageTable` neighbor lookups, and empty tiles self-reap back to `FreeBrickList` with zero CPU readback.
  - **Chen (2016) 2-Level Adaptive Virtual Page Table & Physical Tile Border Aprons:** Encodes physical pool slot + mip/subdivision level + ancestor fallback in a single `uint32_t` page-table entry and pads physical tiles with a 1-texel border apron so hardware/compute bilinear interpolation never bleeds across adjacent physical slots in the tile pool.
  - **Dupuy et al. (2014) 32-Bit Linear Morton GPU Quadtree:** Packs quadtree depth $l \in [0, 14]$ in bits `[0..3]` and the $2l$-bit Morton code in bits `[4..31]` of a single `uint32_t` (extendable to our 12 HEALPix roots by placing `face ∈ [0, 11]` in the top 4 bits, supporting `order = 0..12` in a single 32-bit `uint`!).
- **Refines [planetgenv2.md](file:///C:/SoftwareDev/DiamondDogs/applications/PlanetGen/planetgenv2.md) Sections:**
  - **§4.2, §4.3, & §4.4 (`Software Sparse Virtual Plate Paging` & `Regional AMR`):** Validates our 2-phase lock-free tile allocator (`k_TileUnmapped` $\to$ `k_TilePending` $\to$ `phys`), active-tile `DispatchIndirect` compaction, and 2-level AMR page table with coarse-parent fallback.
  - **§6 (`Path to 3D Meshing`):** Provides a lightweight 32-bit `uint` linear HEALPix quadtree representation (`[face:4][morton:24][level:4]`) for frustum/horizon-culled CDLOD patch emission before full halfedge CBT integration.

---

### Recommended Next Step
If you want to ingest the **7 Tier-1 items** (#1–#6 external open-access PDFs + #7 local `DOC-26` `GPU Pro 5/7` chapter extractions) into [`C:\SoftwareDev\DocParse\results\`](file:///C:/SoftwareDev/DocParse/results) using the zero-API `pypdfium2` + parallel `Model: "flash"` ASD-STE100 pipeline, say the word and I will fetch the open-access PDFs, extract the `GPU Pro` page ranges, and generate the verified `.md` references.