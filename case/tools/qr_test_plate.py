"""Small test plate with the same QR engraving as the lid (30 mm, 1.2 mm modules, 0.6 mm deep).

    python case/tools/qr_test_plate.py   -> esp32case_qr_testplate.stl
"""
from pathlib import Path

import trimesh

from lid_qr import DEPTH, SIZE, qr_matrix

HERE = Path(__file__).resolve().parent.parent
EDGE, THICK = 38.0, 2.0   # 4 mm border, same plate thickness as the lid


def main() -> None:
    plate = trimesh.creation.box(bounds=[[0, 0, 0], [EDGE, EDGE, THICK]])
    mat = qr_matrix()
    n = mat.shape[0]
    mod = SIZE / n
    x0 = y0 = (EDGE - SIZE) / 2
    y0 += SIZE
    cuts = [trimesh.creation.box(bounds=[
        [x0 + c * mod - 1e-3, y0 - (r + 1) * mod - 1e-3, THICK - DEPTH],
        [x0 + (c + 1) * mod + 1e-3, y0 - r * mod + 1e-3, THICK + 1]])
        for r in range(n) for c in range(n) if mat[r, c]]
    out = trimesh.boolean.difference([plate, trimesh.boolean.union(cuts, engine="manifold")], engine="manifold")
    assert out.is_watertight
    out.export(HERE / "esp32case_qr_testplate.stl")
    print(f"{n}x{n} modules of {mod:.2f} mm, plate {EDGE:.0f}x{EDGE:.0f}x{THICK:.0f} mm")


if __name__ == "__main__":
    main()
