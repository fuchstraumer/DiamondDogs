"""Packs a foam material into two KTX1 textures with full mip chains and a JSON descriptor.

This script wraps texture_pack.py in monochrome mode for backwards compatibility.

Usage:
    python foam_texture_pack.py <material folder> <material name> <output folder> <size>
    python foam_texture_pack.py assets/textures/Foam002 Foam002 build/foam 1024
"""

import sys
from pathlib import Path

# Add script directory to sys.path to ensure texture_pack can be imported
sys.path.insert(0, str(Path(__file__).resolve().parent))
import texture_pack


def main() -> None:
    if len(sys.argv) < 5:
        print("Usage: python foam_texture_pack.py <folder> <name> <output> <size>", file=sys.stderr)
        sys.exit(1)

    folder = Path(sys.argv[1])
    name = sys.argv[2]
    output = Path(sys.argv[3])
    size = int(sys.argv[4])

    texture_pack.pack_monochrome(folder, name, output, size)


if __name__ == "__main__":
    main()
