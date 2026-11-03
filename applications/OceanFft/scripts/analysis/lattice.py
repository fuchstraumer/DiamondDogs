"""Measure exact tiling in a top-down OceanFft capture.

Computes the 2D autocorrelation of the image and compares its value at each cascade's lattice offsets, (L, 0) and
(0, L), with the mean and spread at the same radius. A cascade that repeats visibly gives a z-score well above 2.
The dominant wavelength also makes peaks, but at any angle and not at the lattice offsets.

Usage (Python 3.13, which has Pillow and numpy):
    C:\\Python313\\python.exe lattice.py <capture.png> <camera distance m> <vertical fov deg> <L1> <L2> ...
The camera must look straight down (`camera 0 89.4 <distance>`). ArcballCamera's FovY is 0.9 rad = 51.57 deg.
"""

import sys

import numpy as np
from PIL import Image

path, distance, fov = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
scales = [float(s) for s in sys.argv[4:]]
img = np.asarray(Image.open(path).convert("L"), dtype=np.float64)
h, w = img.shape
side = min(h, w)
img = img[(h - side) // 2:(h + side) // 2, (w - side) // 2:(w + side) // 2]
img -= img.mean()

spectrum = np.fft.fft2(img)
ac = np.fft.fftshift(np.real(np.fft.ifft2(spectrum * np.conj(spectrum))))
ac /= ac.max()
c = side // 2
metres_per_pixel = 2.0 * distance * np.tan(np.radians(fov) / 2.0) / h
yy, xx = np.mgrid[0:side, 0:side]
r = np.hypot(yy - c, xx - c)

for L in scales:
    px = L / metres_per_pixel
    if px < 6 or px > side * 0.45:
        print(f"L {L:7.1f} m = {px:6.1f} px: out of range for this distance")
        continue

    ring = ac[np.abs(r - px) < 1.5]
    hits = []
    for dx, dy in ((px, 0), (-px, 0), (0, px), (0, -px)):
        x, y = int(round(c + dx)), int(round(c + dy))
        hits.append(ac[y - 1:y + 2, x - 1:x + 2].max())

    z = (np.mean(hits) - ring.mean()) / (ring.std() + 1e-12)
    print(f"L {L:7.1f} m = {px:6.1f} px: axis peaks {np.round(hits, 3)}, ring mean {ring.mean():.3f} "
          f"sd {ring.std():.3f}, z = {z:+.1f}")
