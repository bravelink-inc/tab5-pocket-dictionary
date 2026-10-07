#!/usr/bin/env python3
"""tray.py - parametric desk cradle for M5Stack Tab5 + Tab5 Keyboard (3D print).

The Tab5 lies screen-up with its NP-F550 battery sitting in a closed pocket in the floor,
and the Tab5 Keyboard (docked to the Tab5's bottom edge) rests on a riser so the whole
unit is level. The underside is one flat plate with recesses for four rubber feet, so the
off-centre battery cannot make the unit rock on the desk. The perimeter lip is kept low so it never covers the side ports
(USB-C, USB-A, microSD, power button), whose exact positions do not matter to this design.

Requires: pip install manifold3d numpy matplotlib   (works on macOS, Windows and Linux)
Usage:    python tray.py            -> out/tray.3mf, out/tray.stl, out/tray_preview.png
          python tray.py --no-battery   (Tab5 without the battery kit)

All sizes are in millimetres. Measure your own unit and adjust PARAMS before printing.
"""
import argparse
import os
import struct
import zipfile

import numpy as np
from manifold3d import Manifold

# [book:16-params]
PARAMS = dict(
    # --- the device (M5Stack docs: Tab5 128 x 80 x 12, Keyboard 128 x 59.4 x 13.1) ---
    tab5_w=128.0,        # X: long edge
    tab5_d=80.0,         # Y: short edge
    tab5_t=12.0,         # body thickness
    kb_d=43.0,           # Y: how far the docked keyboard sticks out from the Tab5's bottom edge
                         #    (M5Stack's STL: 43.0; the shop page says 59.4 overall - measure!)
    kb_t=13.1,           # keyboard thickness
    kb_drop=0.0,         # how far the keyboard's bottom sits below the Tab5's bottom (measure!)
    bat_protrude=14.7,   # battery below the body: 26.7 (kit) - 12.0 (body)
    bat_w=42.0,          # battery pocket, X (NP-F550 is 38 wide here; its 70 mm side runs along Y)
    bat_d=71.5,          # battery pocket, Y (the battery slides out towards the Tab5's top edge)
    bat_cx=36.3,         # pocket centre offset from the Tab5 centre, X (+ = right, screen up,
                         #    keyboard towards you). From the battery holder in M5Stack's STL
    bat_cy=4.7,          # pocket centre offset from the Tab5 centre, Y (+ = away from the keyboard)
    bat_floor=True,      # close the pocket underneath (flat bottom); False = through-hole
    # --- the tray ---
    clearance=0.4,       # gap around the device (per side); 0.3-0.5 for a P2S with PLA
    wall=2.4,            # wall thickness
    floor=2.0,           # floor thickness under the keyboard riser
    lip=3.0,             # how far the lip rises above the device's bottom face
    corner=10.0,         # length of the taller corner posts that hold the device
    corner_h=7.0,        # corner post height above the device's bottom face
    finger=24.0,         # width of the finger notches at the middle of each long side
    feet_d=10.5,         # recesses for stick-on rubber feet (10 mm), 0 = none
    feet_h=1.0,          # recess depth
    feet_in=9.0,         # recess centre, inset from each outer corner
)
# [/book:16-params]


def box(x0, y0, z0, x1, y1, z1):
    """Axis-aligned box from two corners."""
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


# [book:16-build]
def build(p, battery=True):
    c, w = p["clearance"], p["wall"]
    W = p["tab5_w"] + 2 * c                  # inner width
    D = p["tab5_d"] + p["kb_d"] + 2 * c      # inner depth (Tab5 + keyboard, docked)
    closed = p["bat_floor"] or not battery
    base = p["floor"] + (p["bat_protrude"] if battery else 0.0)   # z of the Tab5's bottom face
    if battery and not closed:
        base = p["bat_protrude"]
    kb_floor = base - p["kb_drop"]                          # z of the keyboard's bottom face

    # outer block up to the lip, then hollow out the device's footprint above the floors
    top = base + p["lip"]
    tray = box(-w, -w, 0, W + w, D + w, top)
    y_kb = p["kb_d"] + c                                    # keyboard occupies y in [0, y_kb)
    tray -= box(0, 0, kb_floor, W, y_kb, top + 1)           # keyboard pocket (riser below it)
    tray -= box(0, y_kb, base, W, D, top + 1)               # Tab5 pocket
    # The riser under the keyboard is left solid on purpose: the slicer fills it with sparse
    # infill (15 % is enough). Hollowing it here would leave a wide unsupported ceiling.

    # taller corner posts hold the device without covering the ports on the sides
    post_top = base + p["corner_h"]
    k = p["corner"]
    for x0, x1 in ((-w, k), (W - k, W + w)):
        for y0, y1 in ((-w, k), (D - k, D + w)):
            post = box(x0, y0, 0, x1, y1, post_top)
            post -= box(max(x0, 0), max(y0, 0), base if y0 > 0 else kb_floor,
                        min(x1, W), min(y1, D), post_top + 1)
            tray += post

    if battery:
        # pocket for the battery (cut after the posts so no post reaches into it):
        # the Tab5 rests on the floor around it and the battery hangs into it
        cx = W / 2 + p["bat_cx"]
        cy = y_kb + p["tab5_d"] / 2 + p["bat_cy"]
        tray -= box(cx - p["bat_w"] / 2, cy - p["bat_d"] / 2, p["floor"] if closed else -1,
                    cx + p["bat_w"] / 2, cy + p["bat_d"] / 2, base + 1)

    # finger notches on the long sides so the unit can be lifted out
    f = p["finger"]
    for y0 in (-w - 1, D - 1):
        tray -= box(W / 2 - f / 2, y0, base - 1, W / 2 + f / 2, y0 + w + 2, top + 1)

    # recesses for rubber feet under the four corners
    if p["feet_d"] > 0:
        r, k = p["feet_d"] / 2, p["feet_in"]
        for x in (-w + k, W + w - k):
            for y in (-w + k, D + w - k):
                tray -= Manifold.cylinder(p["feet_h"] + 1, r, r, 48).translate([x, y, -1])
    return tray
# [/book:16-build]


def mesh_arrays(manifold):
    """Vertices and triangles of the closed mesh. Keep every triangle: a zero-area one still
    closes a seam, and dropping it would open a hole."""
    mesh = manifold.to_mesh()
    return np.asarray(mesh.vert_properties)[:, :3].astype(np.float64), np.asarray(mesh.tri_verts)


def write_3mf(manifold, path):
    """3MF (the native format of Bambu Studio / PrusaSlicer): an indexed mesh in millimetres."""
    verts, tris = mesh_arrays(manifold)
    vx = "".join(f'<vertex x="{x:.4f}" y="{y:.4f}" z="{z:.4f}"/>' for x, y, z in verts)
    tx = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             f'<resources><object id="1" type="model"><mesh><vertices>{vx}</vertices>'
             f'<triangles>{tx}</triangles></mesh></object></resources>'
             '<build><item objectid="1"/></build></model>')
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
             '</Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
    return len(tris)


def write_stl(manifold, path):
    verts, tris = mesh_arrays(manifold)
    with open(path, "wb") as f:
        f.write(b"tab5 tray".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            va, vb, vc = verts[a], verts[b], verts[c]
            n = np.cross(vb - va, vc - va)
            n = n / (np.linalg.norm(n) or 1.0)
            f.write(struct.pack("<12fH", *n, *va, *vb, *vc, 0))
    return len(tris)


def preview(manifold, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    mesh = manifold.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    fig = plt.figure(figsize=(8, 6), dpi=120)
    ax = fig.add_subplot(111, projection="3d")
    tri = v[t]
    normals = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-9
    light = np.array([-0.4, -0.6, 0.7]); light /= np.linalg.norm(light)
    shade = 0.45 + 0.55 * np.clip(normals @ light, 0, 1)
    base_rgb = np.array([0x9F, 0xB3, 0xC8]) / 255
    # back-face culling: matplotlib has no depth buffer, so drop faces turned away from the camera
    elev, azim = np.radians(35), np.radians(-60)
    eye = np.array([np.cos(elev) * np.cos(azim), np.cos(elev) * np.sin(azim), np.sin(elev)])
    front = normals @ eye > 1e-6
    poly = Poly3DCollection(tri[front], facecolors=np.outer(shade[front], base_rgb), edgecolor="none")
    ax.add_collection3d(poly)
    mn, mx = v.min(axis=0), v.max(axis=0)
    span = (mx - mn).max()
    ctr = (mx + mn) / 2
    for setlim, i in ((ax.set_xlim, 0), (ax.set_ylim, 1), (ax.set_zlim, 2)):
        setlim(ctr[i] - span / 2, ctr[i] + span / 2)
    ax.view_init(elev=35, azim=-60)
    ax.set_axis_off()
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-battery", action="store_true", help="Tab5 without the battery kit")
    ap.add_argument("-o", "--outdir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "out"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    tray = build(PARAMS, battery=not a.no_battery)
    name = "tray_nobattery" if a.no_battery else "tray"
    stl = os.path.join(a.outdir, name + ".stl")
    n = write_stl(tray, stl)
    write_3mf(tray, os.path.join(a.outdir, name + ".3mf"))
    preview(tray, os.path.join(a.outdir, name + "_preview.png"))
    bb = tray.bounding_box()
    size = [bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]]
    print(f"{stl}: {n} triangles, {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} mm, "
          f"volume {tray.volume() / 1000:.1f} cm3")


if __name__ == "__main__":
    main()
