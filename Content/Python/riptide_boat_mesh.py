"""Builds the patrol skiff model in code and writes it as OBJ files that Unreal imports.

No third-party model: the hull is lofted from cross-sections, the fittings are simple solids. Pure Python (no
Unreal), so it runs anywhere. Coordinates are Unreal's: X forward, Y right (starboard), Z up, centimetres, with
the origin at the centre of the boat's physics box (HullExtent 300 x 110 x 35 in ARiptideBoat), so the waterline
sits at Z = -15.

    build_skiff()     -> Mesh   hull, collar, deck, console, leaning post, T-top, bow rail
    build_outboard()  -> Mesh   the outboard, pivot at its steering axis (the model origin)
    Mesh.write_obj(path)
"""

import math

# --- Shape ------------------------------------------------------------------------------------------------------

LENGTH = 600.0           # stern at X = -300, stem at X = +300
HALF_BEAM = 110.0
STERN_X = -300.0
STATIONS = 48            # lengthwise resolution of the hull


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def station(t):
    """Half cross-section at t (0 = transom, 1 = stem): dict of the key heights and half-beams, in cm."""
    # Planform: full beam over the aft third, then narrowing steadily to a pointed stem (not a blunt pram bow).
    if t < 0.35:
        sheer_b = HALF_BEAM
    else:
        u = (t - 0.35) / 0.65
        sheer_b = HALF_BEAM * max(0.0, math.cos(u * math.pi / 2)) ** 0.75
    sheer_z = 35.0 + 50.0 * t ** 2.2                       # sheer sweeps up to a tall, proud bow
    keel_z = -42.0 + 30.0 * smoothstep(0.62, 1.0, t) ** 1.3 + 50.0 * smoothstep(0.93, 1.0, t)
    chine_b = sheer_b * (0.88 - 0.1 * smoothstep(0.6, 1.0, t))
    chine_z = -24.0 + 34.0 * smoothstep(0.55, 1.0, t)       # chine sweeps up into the bow
    chine_z = min(chine_z, sheer_z - 6.0)
    deck_z = 5.0 + 14.0 * smoothstep(0.7, 1.0, t)
    return {"sheer_b": sheer_b, "sheer_z": sheer_z, "keel_z": keel_z, "chine_b": chine_b, "chine_z": chine_z,
            "deck_z": deck_z, "x": STERN_X + LENGTH * t}


# --- Mesh building ----------------------------------------------------------------------------------------------

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(v):
    l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    return (v[0] / l, v[1] / l, v[2] / l) if l > 1e-9 else (0.0, 0.0, 1.0)


class Mesh:
    """Triangles grouped by material. Each part added is smooth-shaded on its own; parts meet at hard edges."""

    def __init__(self):
        self.verts = []      # (pos, normal, uv)
        self.faces = {}      # material -> [(i, j, k)]

    def _part(self, points, tris, material, uv_scale=100.0, outward_hint=None):
        """Adds one smooth part. tris index into points. outward_hint(p) gives a point the normal should face away
        from; triangles facing toward it are flipped."""
        acc = [(0.0, 0.0, 0.0)] * len(points)
        fixed = []
        for i, j, k in tris:
            n = cross(sub(points[j], points[i]), sub(points[k], points[i]))
            if outward_hint is not None:
                c = tuple((points[i][a] + points[j][a] + points[k][a]) / 3.0 for a in range(3))
                inside = outward_hint(c)
                if sum(n[a] * (c[a] - inside[a]) for a in range(3)) < 0:
                    j, k = k, j
                    n = (-n[0], -n[1], -n[2])
            fixed.append((i, j, k))
            for v in (i, j, k):
                acc[v] = (acc[v][0] + n[0], acc[v][1] + n[1], acc[v][2] + n[2])
        base = len(self.verts)
        for p, n in zip(points, acc):
            n = norm(n)
            # Box-projected UVs so textures read the same on every face.
            ax = max(range(3), key=lambda a: abs(n[a]))
            u, v = [(p[1], p[2]), (p[0], p[2]), (p[0], p[1])][ax]
            self.verts.append((p, n, (u / uv_scale, v / uv_scale)))
        self.faces.setdefault(material, []).extend((base + i, base + j, base + k) for i, j, k in fixed)

    def grid(self, rows, material, outward_hint=None, close_rows=False):
        """Surface through rows of points (rows = list of equal-length point lists), quads between neighbours."""
        pts = [p for row in rows for p in row]
        w = len(rows[0])
        tris = []
        for r in range(len(rows) - 1):
            for c in range(w - 1 if not close_rows else w):
                c2 = (c + 1) % w
                a, b, cc, d = r * w + c, r * w + c2, (r + 1) * w + c2, (r + 1) * w + c
                tris += [(a, b, cc), (a, cc, d)]
        self._part(pts, tris, material, outward_hint=outward_hint)

    def fan(self, outline, material, outward_hint=None):
        """Flat-ish cap over a closed outline, fanned from its centroid."""
        c = tuple(sum(p[a] for p in outline) / len(outline) for a in range(3))
        pts = [c] + list(outline)
        tris = [(0, i + 1, (i + 1) % len(outline) + 1) for i in range(len(outline))]
        self._part(pts, tris, material, outward_hint=outward_hint)

    def box(self, lo, hi, material):
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        faces = [
            [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)],
            [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
            [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
            [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],
            [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
            [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
        ]
        for f in faces:
            self._part(f, [(0, 1, 2), (0, 2, 3)], material, outward_hint=lambda p: c)

    def prism(self, profile_xz, y0, y1, material):
        """Side profile (convex polygon in X/Z) extruded across Y from y0 to y1, capped."""
        c = (sum(p[0] for p in profile_xz) / len(profile_xz), (y0 + y1) / 2, sum(p[1] for p in profile_xz) / len(profile_xz))
        hint = lambda p: c
        n = len(profile_xz)
        for i in range(n):
            a, b = profile_xz[i], profile_xz[(i + 1) % n]
            quad = [(a[0], y0, a[1]), (b[0], y0, b[1]), (b[0], y1, b[1]), (a[0], y1, a[1])]
            self._part(quad, [(0, 1, 2), (0, 2, 3)], material, outward_hint=hint)
        self.fan([(p[0], y0, p[1]) for p in profile_xz], material, outward_hint=hint)
        self.fan([(p[0], y1, p[1]) for p in profile_xz], material, outward_hint=hint)

    def tube(self, path, radius, material, sides=10):
        """Round tube along a polyline, open ends (they meet other parts)."""
        rows = []
        for i, p in enumerate(path):
            d = norm(sub(path[min(i + 1, len(path) - 1)], path[max(i - 1, 0)]))
            ref = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
            u = norm(cross(d, ref))
            v = cross(d, u)
            rows.append([(p[0] + radius * (math.cos(a) * u[0] + math.sin(a) * v[0]),
                          p[1] + radius * (math.cos(a) * u[1] + math.sin(a) * v[1]),
                          p[2] + radius * (math.cos(a) * u[2] + math.sin(a) * v[2]))
                         for a in (2 * math.pi * s / sides for s in range(sides))])
        pts_center = path

        def hint(p):
            best = min(pts_center, key=lambda q: (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2)
            return best
        self.grid(rows, material, outward_hint=hint, close_rows=True)

    def write_obj(self, path):
        """OBJ for Unreal's importer, which keeps X and Z and flips Y (right- to left-handed), so positions and
        normals are written as (X, -Y, Z) to arrive as Unreal's X forward, Y right, Z up."""
        def out(v):
            return (v[0], -v[1], v[2])
        with open(path, "w") as f:
            f.write("# Riptide patrol skiff, generated by riptide_boat_mesh.py\n")
            for p, n, uv in self.verts:
                f.write("v %.3f %.3f %.3f\n" % out(p))
            for p, n, uv in self.verts:
                f.write("vn %.4f %.4f %.4f\n" % out(n))
            for p, n, uv in self.verts:
                f.write("vt %.4f %.4f\n" % uv)
            for material, faces in self.faces.items():
                # Material sections only, no groups: Unreal would import each group as a separate mesh.
                f.write("usemtl %s\n" % material)
                for i, j, k in faces:
                    # Unreal's triangles face the other way to OBJ's after the handedness flip.
                    a, b, c = i + 1, k + 1, j + 1
                    f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a, a, a, b, b, b, c, c, c))


# --- The boat ---------------------------------------------------------------------------------------------------

def _hull(m):
    ts = [i / STATIONS for i in range(STATIONS + 1)]
    secs = [station(t) for t in ts]
    centre_line = lambda p: (p[0], 0.0, p[2] + 30.0)   # normals point away from inside the hull

    for side in (1.0, -1.0):
        # Bottom: keel out to the chine (a deep V), in a few steps so it curves slightly.
        bottom = []
        for s in secs:
            row = []
            for k in range(5):
                f = k / 4.0
                y = s["chine_b"] * f
                z = s["keel_z"] + (s["chine_z"] - s["keel_z"]) * (f ** 0.9)
                row.append((s["x"], side * y, z))
            bottom.append(row)
        m.grid(bottom, "Aluminium", outward_hint=lambda p: (p[0], 0.0, 40.0))
        # Reverse chine: a small flat shelf, then the topsides flaring out to the sheer.
        chine = [[(s["x"], side * s["chine_b"], s["chine_z"]),
                  (s["x"], side * (s["chine_b"] + (s["sheer_b"] - s["chine_b"]) * 0.35), s["chine_z"] + 1.0)] for s in secs]
        m.grid(chine, "Aluminium", outward_hint=lambda p: (p[0], 0.0, p[2] + 50.0))
        sides = [[(s["x"], side * (s["chine_b"] + (s["sheer_b"] - s["chine_b"]) * 0.35), s["chine_z"] + 1.0),
                  (s["x"], side * s["sheer_b"] * 0.99, (s["chine_z"] + s["sheer_z"]) / 2),
                  (s["x"], side * s["sheer_b"], s["sheer_z"])] for s in secs]
        m.grid(sides, "Aluminium", outward_hint=lambda p: (p[0], 0.0, p[2]))

        # Inside: bulwark wall from the gunwale down to the deck, 6 cm in from the sheer.
        wall = [[(s["x"], side * max(0.0, s["sheer_b"] - 6.0), s["sheer_z"]),
                 (s["x"], side * max(0.0, s["sheer_b"] - 6.0), s["deck_z"])] for s in secs[:-2]]
        m.grid(wall, "HullInside", outward_hint=lambda p: (p[0], side * 500.0, p[2]))
        # Gunwale cap closing the top of the shell.
        cap = [[(s["x"], side * s["sheer_b"], s["sheer_z"]), (s["x"], side * max(0.0, s["sheer_b"] - 6.0), s["sheer_z"])]
               for s in secs[:-2]]
        m.grid(cap, "Aluminium", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))

        # Fender collar: a thick black rubber band around the gunwale (like a patrol boat's foam collar).
        collar = []
        for s in secs[:-1]:
            b, z = s["sheer_b"], s["sheer_z"]
            collar.append([(s["x"], side * (b - 1.0), z + 4.0), (s["x"], side * (b + 7.0), z + 1.0),
                           (s["x"], side * (b + 9.0), z - 6.0), (s["x"], side * (b + 7.0), z - 13.0),
                           (s["x"], side * (b - 1.0), z - 15.0)])
        m.grid(collar, "Collar", outward_hint=lambda p: (p[0], side * 1.0, p[2] - 5.0))

    # Deck: flat between the bulwark walls, rising into the bow.
    deck = [[(s["x"], -max(0.0, s["sheer_b"] - 6.0), s["deck_z"]), (s["x"], max(0.0, s["sheer_b"] - 6.0), s["deck_z"])]
            for s in secs[:-2]]
    m.grid(deck, "Deck", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))

    # Transom: the flat stern panel, and the deck's end wall at the bow.
    s0 = secs[0]

    def transom_side(side):
        """Transom edge from the keel up to the sheer on one side."""
        pts = [(STERN_X, side * s0["chine_b"] * f, s0["keel_z"] + (s0["chine_z"] - s0["keel_z"]) * f ** 0.9)
               for f in (0.25, 0.5, 0.75, 1.0)]
        pts.append((STERN_X, side * (s0["chine_b"] + (s0["sheer_b"] - s0["chine_b"]) * 0.35), s0["chine_z"] + 1.0))
        pts.append((STERN_X, side * s0["sheer_b"], s0["sheer_z"]))
        return pts
    loop = [(STERN_X, 0.0, s0["keel_z"])] + transom_side(1.0) + list(reversed(transom_side(-1.0)))
    m.fan(loop, "Aluminium", outward_hint=lambda p: (p[0] + 50.0, 0.0, p[2]))

    se = secs[-3]
    m.fan([(se["x"], -max(0.0, se["sheer_b"] - 6.0), se["deck_z"]), (se["x"], max(0.0, se["sheer_b"] - 6.0), se["deck_z"]),
           (se["x"], max(0.0, se["sheer_b"] - 6.0), se["sheer_z"]), (se["x"], -max(0.0, se["sheer_b"] - 6.0), se["sheer_z"])],
          "HullInside", outward_hint=lambda p: (p[0] + 50.0, 0.0, p[2]))


def _console(m):
    deck = 5.0
    # Console: a box with a raked front, and a windscreen on top.
    profile = [(-45.0, deck), (35.0, deck), (35.0, deck + 70.0), (5.0, deck + 112.0), (-45.0, deck + 112.0)]
    m.prism(profile, -45.0, 45.0, "Console")
    glass = [(5.0, deck + 112.0), (0.0, deck + 112.0), (-18.0, deck + 150.0), (-13.0, deck + 150.0)]
    m.prism(glass, -42.0, 42.0, "Glass")
    # Wheel and a dash panel.
    m.box((-49.0, -30.0, deck + 95.0), (-45.0, 30.0, deck + 110.0), "Trim")
    m.tube([(-52.0, 16.0 * math.cos(a), deck + 92.0 + 16.0 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(17)]],
           1.6, "Trim", sides=8)
    m.tube([(-45.0, 0.0, deck + 92.0), (-52.0, 0.0, deck + 92.0)], 2.0, "Trim", sides=8)

    # Leaning post behind the helm, with a padded bolster.
    m.box((-160.0, -40.0, deck), (-130.0, 40.0, deck + 70.0), "Console")
    m.box((-162.0, -42.0, deck + 70.0), (-128.0, 42.0, deck + 92.0), "Cushion")


def _t_top(m):
    deck = 5.0
    top = 225.0
    legs = [(-150.0, 52.0), (-150.0, -52.0), (20.0, 52.0), (20.0, -52.0)]
    for x, y in legs:
        m.tube([(x, y, deck), (x, y * 1.05, deck + 110.0), (x + (8.0 if x > 0 else -8.0), y * 1.15, top)], 3.5, "Frame")
    # Canopy frame and roof.
    ring = [(-158.0, 60.0, top), (28.0, 60.0, top), (28.0, -60.0, top), (-158.0, -60.0, top), (-158.0, 60.0, top)]
    m.tube(ring, 3.5, "Frame")
    m.box((-165.0, -70.0, top + 2.0), (35.0, 70.0, top + 8.0), "Canopy")
    # Light bar and a radar dome up top.
    m.box((-20.0, -40.0, top + 8.0), (0.0, 40.0, top + 16.0), "Trim")
    dome = []
    for r in range(7):
        a = (math.pi / 2) * r / 6
        dome.append([(-90.0 + 22.0 * math.cos(a) * math.cos(b), 22.0 * math.cos(a) * math.sin(b), top + 8.0 + 14.0 * math.sin(a))
                     for b in [2 * math.pi * i / 16 for i in range(16)]])
    m.grid(dome, "Canopy", outward_hint=lambda p: (-90.0, 0.0, top + 8.0), close_rows=True)


def _bow_rail(m):
    secs = [station(t) for t in [0.55 + 0.45 * i / 20 for i in range(21)]]
    for side in (1.0, -1.0):
        rail = [(s["x"], side * max(0.0, s["sheer_b"] - 10.0), s["sheer_z"] + 30.0) for s in secs[:-2]]
        tip = secs[-2]
        rail.append((tip["x"] + 5.0, 0.0, tip["sheer_z"] + 30.0))
        m.tube(rail, 2.2, "Frame", sides=8)
        for s in secs[:-2:4]:
            b = max(0.0, s["sheer_b"] - 10.0)
            m.tube([(s["x"], side * b, s["sheer_z"]), (s["x"], side * b, s["sheer_z"] + 30.0)], 1.8, "Frame", sides=8)


def build_skiff():
    m = Mesh()
    _hull(m)
    _console(m)
    _t_top(m)
    _bow_rail(m)
    return m


def build_outboard():
    """A big outboard. Origin = its steering pivot, on the transom centreline; the leg hangs below it and the
    prop sits near ARiptideBoat's Propeller point (15 cm aft of the transom, 25 cm below the hull box)."""
    m = Mesh()
    # Cowling: rounded box, sloping back.
    rows = []
    for zi, (z, sx, sy, xoff) in enumerate([(30.0, 30.0, 26.0, 0.0), (45.0, 34.0, 30.0, -2.0), (75.0, 34.0, 30.0, -4.0),
                                             (95.0, 30.0, 26.0, -6.0), (104.0, 22.0, 18.0, -8.0), (108.0, 10.0, 8.0, -8.0)]):
        row = []
        for i in range(20):
            a = 2 * math.pi * i / 20
            row.append((-30.0 + xoff + sx * math.cos(a), sy * 0.95 * math.sin(a), z))
        rows.append(row)
    m.grid(rows, "Cowling", outward_hint=lambda p: (-32.0, 0.0, p[2]), close_rows=True)
    m.fan([p for p in rows[0]], "Cowling", outward_hint=lambda p: (-32.0, 0.0, 60.0))
    m.fan([p for p in rows[-1]], "Cowling", outward_hint=lambda p: (-32.0, 0.0, 60.0))
    # Clamp bracket onto the transom.
    m.box((-8.0, -14.0, 10.0), (0.0, 14.0, 40.0), "Trim")
    # Midsection leg down to the gearcase.
    m.prism([(-14.0, 32.0), (-36.0, 32.0), (-34.0, -60.0), (-16.0, -60.0)], -8.0, 8.0, "Cowling")
    # Gearcase torpedo, skeg, and anti-ventilation plate.
    torpedo = []
    for xi in range(9):
        x = -2.0 - 44.0 * xi / 8
        r = 8.0 * math.sin(math.pi * (0.15 + 0.85 * xi / 8)) + 1.0
        torpedo.append([(x, r * math.cos(a), -75.0 + r * math.sin(a)) for a in [2 * math.pi * i / 12 for i in range(12)]])
    m.grid(torpedo, "Trim", outward_hint=lambda p: (p[0], 0.0, -75.0), close_rows=True)
    m.prism([(-30.0, -80.0), (-12.0, -80.0), (-18.0, -98.0), (-26.0, -98.0)], -1.5, 1.5, "Trim")
    m.box((-44.0, -16.0, -58.0), (-8.0, 16.0, -55.0), "Cowling")
    # Propeller: three blades on a hub.
    m.tube([(-46.0, 0.0, -75.0), (-54.0, 0.0, -75.0)], 4.0, "Prop", sides=10)
    for k in range(3):
        a = 2 * math.pi * k / 3
        c, s = math.cos(a), math.sin(a)
        blade = [(-48.0, 4.0 * c, -75.0 + 4.0 * s), (-52.0, 4.0 * c, -75.0 + 4.0 * s),
                 (-54.0, 17.0 * math.cos(a + 0.25), -75.0 + 17.0 * math.sin(a + 0.25)),
                 (-47.0, 17.0 * math.cos(a - 0.1), -75.0 + 17.0 * math.sin(a - 0.1))]
        m._part(blade, [(0, 1, 2), (0, 2, 3)], "Prop")
        m._part(list(reversed(blade)), [(0, 1, 2), (0, 2, 3)], "Prop")
    return m


if __name__ == "__main__":
    import os
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, mesh in (("skiff", build_skiff()), ("outboard", build_outboard())):
        mesh.write_obj(os.path.join(out, name + ".obj"))
        print(name, len(mesh.verts), "verts", sum(len(f) for f in mesh.faces.values()), "tris")
