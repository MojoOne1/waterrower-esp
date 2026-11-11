"""Lid variant with a QR code to the repository, engraved like the WR mark.

    python case/tools/lid_qr.py      (needs trimesh, manifold3d, qrcode)

Reads esp32case_lid.stl, closes the two vent slots nearest the free area to
make room, engraves the code 0.6 mm deep into the top face and writes
esp32case_lid_qr.stl. The code is upper case on purpose: the alphanumeric
QR mode fits the URL into a 25x25 version 2 symbol, which keeps the modules
0.88 mm wide - printable with a 0.4 mm nozzle. GitHub resolves owner and
repository names without regard to case.
"""
from pathlib import Path

import numpy as np
import qrcode
import trimesh

HERE = Path(__file__).resolve().parent.parent
URL = "HTTPS://GITHUB.COM/MOJOONE1/WATERROWER-ESP"
TOP = 7.61            # outer face of the lid (it prints face down)
DEPTH = 0.6           # same as the WR mark: three 0.2 mm layers
SIZE = 22.0           # mm, edge of the symbol without quiet zone
CENTER = (35.25, 14.0)
SLOTS = [(37.5, 40.5), (43.5, 46.5)]   # vent slots closed for the code
PLATE = (5.61, TOP)                    # z range of the lid plate


def qr_matrix() -> np.ndarray:
    q = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_L, border=0)
    q.add_data(URL)
    q.make(fit=True)
    return np.array(q.get_matrix(), dtype=bool)


def main() -> None:
    lid = trimesh.load(HERE / "esp32case_lid.stl")
    fills = [trimesh.creation.box(bounds=[[x0, 1.5, PLATE[0]], [x1, 26.5, PLATE[1]]])
             for x0, x1 in SLOTS]
    lid = trimesh.boolean.union([lid, *fills], engine="manifold")

    mat = qr_matrix()
    n = mat.shape[0]
    mod = SIZE / n
    x0, y0 = CENTER[0] - SIZE / 2, CENTER[1] + SIZE / 2
    cuts = []
    for r in range(n):
        for c in range(n):
            if mat[r, c]:
                # Viewed from above (+z) with row 0 at the top: not mirrored.
                x, y = x0 + c * mod, y0 - (r + 1) * mod
                cuts.append(trimesh.creation.box(
                    bounds=[[x - 1e-3, y - 1e-3, TOP - DEPTH], [x + mod + 1e-3, y + mod + 1e-3, TOP + 1]]))
    cutter = trimesh.boolean.union(cuts, engine="manifold")
    out = trimesh.boolean.difference([lid, cutter], engine="manifold")
    assert out.is_watertight
    out.export(HERE / "esp32case_lid_qr.stl")
    print(f"QR version {(n - 17) // 4}, {n}x{n} modules of {mod:.2f} mm -> esp32case_lid_qr.stl")


if __name__ == "__main__":
    main()
