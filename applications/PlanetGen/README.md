# PlanetGen

The C++ and Lodestone port of the PlanetLab plate model. PlanetLab (`applications/PlanetLab`) is the Python
prototype, and git ignores it. Its file `gpu_port/README.md` is the pass specification, and its script
`write_gpu_reference.py` writes the reference files that the checks here read.

## Parts

| Path | Holds |
|---|---|
| `assets/shaders/compute/Planet/` | The Slang module `PlanetLab` (passes 0 to 3). Lodestone cooks it to `ShaderLibrary.ldmanifest` as a build step (target `planet_gen_shaders`). `Buffers.slang` declares the buffer shims and the crust flag axis. |
| `gpu/` | `planet_gpu`, a static library. `PlanetShaders` selects a variant for the device, makes one program for each entry point, and writes the descriptor sets by the names that the cook reflected (`WriteManifestDescriptors`). It also writes uniform buffers from the member offsets in the manifest. `GpuBuffers` holds buffers by their shader names, and reads them back. |
| `check/` | `PlanetGenCheck`, a headless executable. It runs each pass on the reference inputs and compares the result with the reference outputs. |

## Sources

- The inverse map (`Fibonacci.slang`) is the algorithm of Keinert, Innmann, Saenger and Stamminger, "Spherical
  Fibonacci Mapping" (SIGGRAPH Asia 2015). It is not ours. Our changes: a 64-bit integer azimuth, a closed form for
  the grid basis, and an integer candidate index. Without them the map fails in float32 from 100 000 points.
- The plate model follows Cortial, Peytavie, Galin and Guérin, "Procedural Tectonic Planets" (2019). The
  PlanetLab README lists the other sources and which rules are ours.

## The check

Make the reference files first, from `applications/PlanetLab`:

```bat
C:\Python313\python.exe write_gpu_reference.py 10000 40
C:\Python313\python.exe write_gpu_reference.py 100000 40
```

Then run the check for each size, from its output folder:

```bat
PlanetGenCheck --reference=C:\SoftwareDev\DiamondDogs\applications\PlanetLab\gpu_port\reference_100000
```

`--checks=map,owners,front,crust` selects checks; the default is all of them. Each check runs once for each crust flag
storage (bytes, then words). A storage that the device cannot run prints `SKIP`. The program exits 0 only if each check
passes. Validation is `Full`, so a pass also needs output with no `Validation Error` and no `VUID`. If the reference
folder is missing, the check fails; it does not skip.

To test a device without 8-bit storage, decline it with `--config=<file>`:

```json
{ "Device": { "Declined": [ "StorageBuffer8BitAccess" ] } }
```

The device then prefers words, and the byte runs print `SKIP`.

The tolerances are the ones of the pass specification:

| Check | Pass | Tolerance |
|---|---|---|
| `map` | 0, the inverse map | Equal, or a near tie (5 % of the point spacing); fewer than 10 ties |
| `owners` | 1, owners | Fewer than 1 point in 10 000 differs |
| `front` | 2, one plate on top | Exact |
| `crust` | 3, new crust and removal | Exact, and a relative 1e-4 for the front values |

## Buffers

Each storage buffer is a Lodestone shim (`IReadBuffer`, `IReadWriteBuffer`, `IAtomicBuffer`). The `spirv` cook binds
each one as a descriptor. A `spirv-modern` cook would store a buffer device address instead, with no change to the
Slang; the host side here writes descriptors only.

### Storage of the crust flags

All passes read and write one set of crust arrays: `CrustAlive`, `CrustContinental` and `CrustBirth`. The Capability
axis `CRUST_FLAG_BYTES` selects the storage of the two flags:

| Value | Buffers | Cost | Device needs |
|---|---|---|---|
| `true` | `CrustAliveBytes`, `CrustContinentalBytes`: one byte for each cell | 1 byte | `StorageBuffer8BitAccess`, `UniformAndStorageBuffer8BitAccess`, `Int8` (what the cook recorded) |
| `false` | `CrustAliveWords`, `CrustContinentalWords`: one word for each cell | 4 bytes | nothing more |

A byte store changes only its own byte, so the threads of pass 3 need no atomic in either form. The 8-bit needs are
`Wanted`, not `Required`, because only one value of a Capability axis needs them. `PlanetShaders::Create` with
`CrustFlagChoice::BestForDevice` uses `PreferCapable`, so it takes bytes when the device was created with 8-bit
storage. `PlanetShaders::CrustFlags()` tells the host which buffers to create; `CrustFlagBufferNames` gives their
names. Each variant binds only the buffers of its own storage.

### Votes

Pass 2 counts votes with `IAtomicBuffer<uint>.Add`, one atomic for each vote. `AtomicAddWaveAggregated` does not fit:
it needs one index on every lane, and the pair index changes from lane to lane. It would also save little. In the
100 000-point capture, 808 points vote (0.8 %), a wave of 32 that has a voter has 1.1 voters on average, and a perfect
aggregation would remove 18 of the 808 atomics.
