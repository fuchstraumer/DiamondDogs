"""Measures the value range of the noise bases in assets/shaders/compute/Noise, to set their normalization constants.

The script mirrors the Slang code: the pcg4d hash (Jarzynski and Olano, JCGT 2020), Perlin's 12 edge gradients picked by a
multiply-shift reduction, gradient noise with the quintic fade, and simplex noise with a kernel radius of 0.5. It reports
the largest |n| that it finds with the scale constant at 1.0. The scale constant is the reciprocal of that maximum.

Random samples find a lower bound only. The script then climbs from the best samples with a small local search, which
moves the estimate close to the true maximum. The README of the Noise folder records the numbers.

Run: python assets/scripts/measure_noise_ranges.py
"""

import numpy as np

EDGE_GRADIENTS = np.array(
    [
        [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
        [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
        [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
    ],
    dtype=np.float64,
)


def pcg4d(v):
    v = v * np.uint32(1664525) + np.uint32(1013904223)
    v[0] += v[1] * v[3]
    v[1] += v[2] * v[0]
    v[2] += v[0] * v[1]
    v[3] += v[1] * v[2]
    v ^= v >> np.uint32(16)
    v[0] += v[1] * v[3]
    v[1] += v[2] * v[0]
    v[2] += v[0] * v[1]
    v[3] += v[1] * v[2]
    return v


def hash_cell(cell, seed):
    lanes = np.stack([cell[..., 0], cell[..., 1], cell[..., 2], np.full(cell.shape[:-1], seed)]).astype(np.int64)
    lanes = (lanes & 0xFFFFFFFF).astype(np.uint32)
    return pcg4d(lanes)


def edge_gradient(cell, seed):
    h = hash_cell(cell, seed)[0]
    index = ((h >> np.uint32(16)).astype(np.uint64) * 12) >> 16
    return EDGE_GRADIENTS[index.astype(np.int64)]


def fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlin3(p, seed):
    cell = np.floor(p).astype(np.int64)
    frac = p - cell
    u = fade(frac)
    result = np.zeros(p.shape[0])
    for corner in np.ndindex(2, 2, 2):
        offset = np.array(corner)
        weight = np.prod(np.where(offset == 1, u, 1.0 - u), axis=1)
        result += weight * np.sum(edge_gradient(cell + offset, seed) * (frac - offset), axis=1)
    return result


def value3(p, seed):
    cell = np.floor(p).astype(np.int64)
    frac = p - cell
    u = fade(frac)
    result = np.zeros(p.shape[0])
    for corner in np.ndindex(2, 2, 2):
        offset = np.array(corner)
        weight = np.prod(np.where(offset == 1, u, 1.0 - u), axis=1)
        h = hash_cell(cell + offset, seed)[1]
        result += weight * (h.astype(np.float64) * (2.0 / 4294967295.0) - 1.0)
    return result


def simplex3(p, seed):
    F3, G3 = 1.0 / 3.0, 1.0 / 6.0
    skew = np.floor(p + np.sum(p, axis=1, keepdims=True) * F3)
    x0 = p - skew + np.sum(skew, axis=1, keepdims=True) * G3
    rank = np.zeros_like(x0, dtype=np.int64)
    for a in range(3):
        for b in range(3):
            if a != b:
                rank[:, a] += (x0[:, a] > x0[:, b]) | ((x0[:, a] == x0[:, b]) & (a < b))
    i1 = (rank >= 2).astype(np.int64)
    i2 = (rank >= 1).astype(np.int64)
    corners = [np.zeros_like(i1), i1, i2, np.ones_like(i1)]
    result = np.zeros(p.shape[0])
    for k, offset in enumerate(corners):
        x = x0 - offset + k * G3
        t = np.maximum(0.5 - np.sum(x * x, axis=1), 0.0)
        g = edge_gradient(skew.astype(np.int64) + offset, seed)
        result += t ** 4 * np.sum(g * x, axis=1)
    return result


def measure(name, basis):
    rng = np.random.default_rng(7)
    best = 0.0
    seeds = (1, 2, 3)
    for seed in seeds:
        points = rng.uniform(-64.0, 64.0, size=(400_000, 3))
        values = np.abs(basis(points, seed))
        top = points[np.argsort(values)[-64:]]
        best = max(best, values.max())
        step = 0.05
        for _ in range(60):
            candidates = top[:, None, :] + rng.normal(0.0, step, size=(top.shape[0], 32, 3))
            flat = candidates.reshape(-1, 3)
            values = np.abs(basis(flat, seed)).reshape(top.shape[0], 32)
            pick = np.argmax(values, axis=1)
            top = candidates[np.arange(top.shape[0]), pick]
            best = max(best, values.max())
            step *= 0.93
    print(f"{name}: max |n| = {best:.6f}, scale = {1.0 / best:.6f}")


if __name__ == "__main__":
    measure("Perlin3", perlin3)
    measure("Simplex3 (r^2 = 0.5)", simplex3)
    measure("Value3", value3)
