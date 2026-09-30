"""Builds the patrol skiff model in code and writes it as OBJ files that Unreal imports.

No third-party model: the hull is lofted from cross-sections, the fittings are simple solids. Pure Python (no
Unreal), so it runs anywhere. Coordinates are Unreal's: X forward, Y right (starboard), Z up, centimetres, with
the origin at the centre of the boat's physics box (HullExtent 395 x 130 x 35 in ARiptideBoat), so the waterline
sits at Z = -15. Full size for a 26 ft patrol boat: 7.9 m long, 2.6 m beam, with room to walk around the console.

    build_skiff()     -> Mesh   hull, collar, deck, console, leaning post, T-top, bow rail
    build_outboard()  -> Mesh   one outboard, pivot at its steering axis (the model origin); the boat carries two
    Mesh.write_obj(path)
"""

import math

# --- Shape ------------------------------------------------------------------------------------------------------

LENGTH = 790.0           # stern at X = -395, stem at X = +395
HALF_BEAM = 130.0
STERN_X = -395.0
STATIONS = 60
DECK_Z = 20.0            # the cockpit deck, 35 cm above the waterline (self-bailing, and clear of the swell)

# The stern: a bulkhead across the boat in front of the motors, gunwale-high, closing the cockpit off from the sea.
# Behind it the transom is notched down to where the motors clamp on, with a splashwell between the two (its floor
# well above the waterline) to catch spray coming over the notch.
BULKHEAD_X = -340.0      # aft face of the cockpit
WELL_HALF = 72.0         # half the width of the motor notch and splashwell
SILL_Z = 45.0            # top of the transom in the notch, where the motor brackets hook on
WELL_FLOOR_Z = 38.0
TRANSOM_THICK = 5.0

# The outboards (ARiptideBoat's OutboardPivot): each tilts on the tube at the top of its clamp bracket, which hooks
# over the transom sill. In the outboard's own model, the transom's outer face is at X = +5.
OUTBOARD_PIVOT_Z = SILL_Z - 5.0

# The throttle: a twin-lever binnacle on the console's back, right of the wheel (see ARiptideBoat's ThrottlePivot).
BINNACLE = (-49.0, 32.0, DECK_Z + 92.0)            # lengthwise resolution of the hull


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
    sheer_z = DECK_Z + 55.0 + 45.0 * t ** 2.2              # knee-high bulwarks aft, sweeping up to a proud bow
    keel_z = -42.0 + 30.0 * smoothstep(0.62, 1.0, t) ** 1.3 + 50.0 * smoothstep(0.93, 1.0, t)
    chine_b = sheer_b * (0.88 - 0.1 * smoothstep(0.6, 1.0, t))
    chine_z = -24.0 + 34.0 * smoothstep(0.55, 1.0, t)       # chine sweeps up into the bow
    chine_z = min(chine_z, sheer_z - 6.0)
    deck_z = DECK_Z + 14.0 * smoothstep(0.7, 1.0, t)
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
        # Gunwale cap closing the top of the shell, right to the stem, where the two sides meet (and the bow platform
        # fills between their inner edges).
        cap = [[(s["x"], side * s["sheer_b"], s["sheer_z"]), (s["x"], side * max(0.0, s["sheer_b"] - 6.0), s["sheer_z"])]
               for s in secs]
        m.grid(cap, "Aluminium", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))

        # Fender collar: a thick black rubber band around the gunwale (like a patrol boat's foam collar).
        collar = []
        for s in secs:
            b, z = s["sheer_b"], s["sheer_z"]
            collar.append([(s["x"], side * (b - 1.0), z + 4.0), (s["x"], side * (b + 7.0), z + 1.0),
                           (s["x"], side * (b + 9.0), z - 6.0), (s["x"], side * (b + 7.0), z - 13.0),
                           (s["x"], side * (b - 1.0), z - 15.0)])
        m.grid(collar, "Collar", outward_hint=lambda p: (p[0], side * 1.0, p[2] - 5.0))
        # Close its end at the transom, so it isn't a hollow tube seen from astern.
        m.fan(collar[0], "Collar", outward_hint=lambda p: (p[0] + 50.0, p[1], p[2]))

    # Deck: flat between the bulwark walls, rising into the bow.
    deck = [[(s["x"], -max(0.0, s["sheer_b"] - 6.0), s["deck_z"]), (s["x"], max(0.0, s["sheer_b"] - 6.0), s["deck_z"])]
            for s in secs[:-2]]
    m.grid(deck, "Deck", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))

    # Transom: the flat stern panel, notched down in the middle for the motors.
    s0 = secs[0]

    def transom_side(side):
        """Transom edge from the keel up to the sheer on one side."""
        pts = [(STERN_X, side * s0["chine_b"] * f, s0["keel_z"] + (s0["chine_z"] - s0["keel_z"]) * f ** 0.9)
               for f in (0.25, 0.5, 0.75, 1.0)]
        pts.append((STERN_X, side * (s0["chine_b"] + (s0["sheer_b"] - s0["chine_b"]) * 0.35), s0["chine_z"] + 1.0))
        # Up the topsides' edge through the same middle point they have, so the transom meets them with no slit.
        pts.append((STERN_X, side * s0["sheer_b"] * 0.99, (s0["chine_z"] + s0["sheer_z"]) / 2))
        return pts
    # Where the topsides' edge crosses the sill's height (between that middle point and the sheer).
    mid = transom_side(1.0)[-1]
    f = (SILL_Z - mid[2]) / (s0["sheer_z"] - mid[2])
    sill_b = mid[1] + (s0["sheer_b"] - mid[1]) * f
    lower = ([(STERN_X, 0.0, s0["keel_z"])] + transom_side(1.0) + [(STERN_X, sill_b, SILL_Z), (STERN_X, -sill_b, SILL_Z)]
             + list(reversed(transom_side(-1.0))))
    aft = lambda p: (p[0] + 50.0, 0.0, p[2])
    m.fan(lower, "Aluminium", outward_hint=aft)
    for side in (1.0, -1.0):
        m._part([(STERN_X, side * sill_b, SILL_Z), (STERN_X, side * s0["sheer_b"], s0["sheer_z"]),
                 (STERN_X, side * WELL_HALF, s0["sheer_z"]), (STERN_X, side * WELL_HALF, SILL_Z)],
                [(0, 1, 2), (0, 2, 3)], "Aluminium", outward_hint=aft)
    _stern_box(m, s0)

    se = secs[-3]
    m.fan([(se["x"], -max(0.0, se["sheer_b"] - 6.0), se["deck_z"]), (se["x"], max(0.0, se["sheer_b"] - 6.0), se["deck_z"]),
           (se["x"], max(0.0, se["sheer_b"] - 6.0), se["sheer_z"]), (se["x"], -max(0.0, se["sheer_b"] - 6.0), se["sheer_z"])],
          "HullInside", outward_hint=lambda p: (p[0] + 50.0, 0.0, p[2]))

    # Bow platform (the anchor locker's lid) closing the stem over the end of the deck at gunwale height, so the bow
    # reads as solid from inside the boat rather than showing the backs of the hull's outer faces.
    tip = secs[-1]
    rim = [(s["x"], max(0.0, s["sheer_b"] - 6.0), s["sheer_z"]) for s in secs[-3:-1]]
    outline = [(se["x"], -rim[0][1], rim[0][2]), (rim[1][0], -rim[1][1], rim[1][2]), (tip["x"], 0.0, tip["sheer_z"]),
               (rim[1][0], rim[1][1], rim[1][2]), (se["x"], rim[0][1], rim[0][2])]
    m.fan(outline, "Deck", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))
    # Its edges down to the hull sides, so there's no gap under the lid where the walls stop.
    for side in (1.0, -1.0):
        edge = [[(x, side * y, z), (x, side * y, z - 30.0)] for x, y, z in
                [(se["x"], rim[0][1], rim[0][2]), (rim[1][0], rim[1][1], rim[1][2]), (tip["x"], 0.0, tip["sheer_z"])]]
        m.grid(edge, "HullInside", outward_hint=lambda p: (p[0], side * 500.0, p[2]))


RADAR = (-70.0, 0.0, DECK_Z + 220.0 + 8.0 + 30.0)   # spinning array's hub (ARiptideBoat's RadarHub)


def _ball(m, c, r, material, rows=8, cols=16):
    pts = []
    for i in range(rows + 1):
        a = -math.pi / 2 + math.pi * i / rows
        pts.append([(c[0] + r * math.cos(a) * math.cos(b), c[1] + r * math.cos(a) * math.sin(b), c[2] + r * math.sin(a))
                    for b in [2 * math.pi * j / cols for j in range(cols)]])
    m.grid(pts, material, outward_hint=lambda p: c, close_rows=True)


def _dome(m, c, r, h, material):
    """A radome: a squashed half-sphere of radius r and height h, on a flat base at c."""
    pts = []
    for i in range(7):
        a = (math.pi / 2) * i / 6
        pts.append([(c[0] + r * math.cos(a) * math.cos(b), c[1] + r * math.cos(a) * math.sin(b), c[2] + h * math.sin(a))
                    for b in [2 * math.pi * j / 16 for j in range(16)]])
    m.grid(pts, material, outward_hint=lambda p: c, close_rows=True)


def _cone(m, a, b, r0, r1, material, sides=12):
    """A flared horn from a (radius r0) to b (radius r1)."""
    d = norm(sub(b, a))
    ref = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = norm(cross(d, ref))
    v = cross(d, u)
    rows = []
    for p, r in ((a, r0), (b, r1)):
        rows.append([(p[0] + r * (math.cos(t) * u[0] + math.sin(t) * v[0]), p[1] + r * (math.cos(t) * u[1] + math.sin(t) * v[1]),
                      p[2] + r * (math.cos(t) * u[2] + math.sin(t) * v[2])) for t in [2 * math.pi * i / sides for i in range(sides)]])
    axis_pts = [a, b]
    m.grid(rows, material, outward_hint=lambda p: min(axis_pts, key=lambda q: sum((q[k] - p[k]) ** 2 for k in range(3))),
           close_rows=True)


def _ring(m, c, r, tube_r, plane, material):
    """A small eye or ring: plane "xz" (faces sideways) or "yz" (faces fore and aft)."""
    pts = []
    for i in range(17):
        a = 2 * math.pi * i / 16
        if plane == "xz":
            pts.append((c[0] + r * math.cos(a), c[1], c[2] + r * math.sin(a)))
        else:
            pts.append((c[0], c[1] + r * math.cos(a), c[2] + r * math.sin(a)))
    m.tube(pts, tube_r, material, sides=6)


def _deck_z_at(x):
    return station((x - STERN_X) / LENGTH)["deck_z"]


def _frame(origin, forward, up_hint=(0.0, 0.0, 1.0)):
    """A local frame at origin: s along `forward`, r across, h up (square to forward). Returns a function mapping a
    local (s, r, h) point into the boat."""
    f = norm(forward)
    r = norm(cross(up_hint, f))
    u = cross(f, r)
    return lambda p: tuple(origin[a] + p[0] * f[a] + p[1] * r[a] + p[2] * u[a] for a in range(3))


def _obox(m, T, lo, hi, material):
    """A box given in a local frame T (see _frame)."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    c = T(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    for f in ([(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
              [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],
              [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]):
        m._part([T(p) for p in f], [(0, 1, 2), (0, 2, 3)], material, outward_hint=lambda p: c)


def _cleat(m, T):
    """A 25 cm mooring cleat in the local frame T (horns along s): a tie-off point. An oval base, two raked legs,
    and horns that thin toward their tips and turn up a little, so a line can't slip off."""
    _obox(m, T, (-7.0, -2.4, 0.0), (7.0, 2.4, 1.0), "Frame")
    for s in (-4.0, 4.0):
        m.tube([T((s, 0.0, 0.8)), T((s * 1.15, 0.0, 4.4))], 1.35, "Frame", sides=8)
    horn = [T((s, 0.0, 5.2 + 0.012 * s * s)) for s in (-12.5, -10.0, -6.0, 0.0, 6.0, 10.0, 12.5)]
    m.tube(horn, 1.25, "Frame", sides=8)
    for end in (horn[0], horn[-1]):
        _ball(m, end, 1.25, "Frame", rows=4, cols=8)


def _gunwale_frame(t, side, inset):
    """The local frame on the gunwale cap at station t, `inset` cm in from the sheer: along the gunwale as it curves
    in toward the bow and sweeps up, square to the cap (which runs straight across the boat at each station)."""
    a, b = station(max(0.0, t - 0.004)), station(min(1.0, t + 0.004))
    st = station(t)
    forward = (b["x"] - a["x"], side * (b["sheer_b"] - a["sheer_b"]), b["sheer_z"] - a["sheer_z"])
    up = norm(cross(forward, (0.0, 1.0, 0.0)))
    if up[2] < 0:
        up = (-up[0], -up[1], -up[2])
    return _frame((st["x"], side * (st["sheer_b"] - inset), st["sheer_z"]), forward, up)


def _deck_hatch(m, x0, x1, hy, handle=True):
    """A flush deck hatch (a storage locker's lid): level with the deck so nothing catches a foot, set in a black
    gasket, with flush pull latches."""
    def flat(x, y, lift):
        return (x, y, _deck_z_at(x) + lift)
    # Gasket: a black border 2 cm wide around the lid.
    g = 2.0
    _quad(m, flat(x0 - g, -hy - g, 0.1), flat(x1 + g, -hy - g, 0.1), flat(x1 + g, hy + g, 0.1), flat(x0 - g, hy + g, 0.1),
          "Trim", (0.5 * (x0 + x1), 0.0, 300.0))
    _quad(m, flat(x0, -hy, 0.2), flat(x1, -hy, 0.2), flat(x1, hy, 0.2), flat(x0, hy, 0.2), "Deck", (0.5 * (x0 + x1), 0.0, 300.0))
    if handle:
        for y in (-hy * 0.5, hy * 0.5):
            xh = x0 + 7.0
            _quad(m, flat(xh - 3.0, y - 5.0, 0.3), flat(xh + 3.0, y - 5.0, 0.3), flat(xh + 3.0, y + 5.0, 0.3),
                  flat(xh - 3.0, y + 5.0, 0.3), "Frame", (xh, y, 300.0))


def _fittings(m):
    """Deck gear, tie-off points and hull fittings, each where it goes on a patrol boat of this kind."""
    # Floor storage lockers in front of the console, and the bilge access hatch in the aft cockpit.
    _deck_hatch(m, 55.0, 150.0, 40.0)
    _deck_hatch(m, 170.0, 240.0, 28.0)
    _deck_hatch(m, -300.0, -245.0, 26.0)
    # Anchor locker lid on the bow platform.
    tip = station(1.0)
    bp = station(0.975)
    m.box((bp["x"] - 6.0, -5.0, bp["sheer_z"]), (bp["x"] + 6.0, 5.0, bp["sheer_z"] + 0.6), "Trim")

    # Cleats: bow pair, midship (spring) pair on the gunwale caps, lying along the gunwale as it curves and rises,
    # and the stern pair on the stern box's top, outboard of its hatches.
    for t in (0.87, 0.49):
        for side in (1.0, -1.0):
            _cleat(m, _gunwale_frame(t, side, 3.6))
    s0 = station(0.0)
    for side in (1.0, -1.0):
        _cleat(m, _frame((STERN_X + 33.0, side * (s0["sheer_b"] - 13.0), s0["sheer_z"]), (1.0, 0.0, 0.0)))
    # Tow post (samson post) on the foredeck, just aft of the anchor locker.
    tx = 345.0
    tz = _deck_z_at(tx)
    m.box((tx - 6.0, -6.0, tz), (tx + 6.0, 6.0, tz + 1.5), "Frame")
    m.tube([(tx, 0.0, tz + 1.5), (tx, 0.0, tz + 32.0)], 3.0, "Frame", sides=12)
    m.tube([(tx, -9.0, tz + 24.0), (tx, 9.0, tz + 24.0)], 1.5, "Frame", sides=8)
    # Bow eye on the stem, above the waterline, and towing / lifting eyes on the transom corners.
    _ring(m, (tip["x"] - 12.0, 0.0, 22.0), 4.0, 1.2, "xz", "Frame")
    for side in (1.0, -1.0):
        _ring(m, (STERN_X - 1.5, side * 108.0, 55.0), 3.5, 1.0, "yz", "Frame")

    # Combination bow light at the stem: red to port, green to starboard.
    lz = tip["sheer_z"] + 2.0
    m.box((tip["x"] - 16.0, -3.0, lz), (tip["x"] - 8.0, 0.0, lz + 4.0), "NavRed")
    m.box((tip["x"] - 16.0, 0.0, lz), (tip["x"] - 8.0, 3.0, lz + 4.0), "NavGreen")
    m.box((tip["x"] - 17.0, -4.0, lz - 1.0), (tip["x"] - 7.0, 4.0, lz), "Trim")

    # Weapon mount sockets: one on the foredeck behind the tow post, one at each aft corner of the cockpit.
    for x, y, z in ((315.0, 0.0, _deck_z_at(315.0)),
                    (-315.0, station(0.1)["sheer_b"] - 3.8, station(0.1)["sheer_z"]),
                    (-315.0, -(station(0.1)["sheer_b"] - 3.8), station(0.1)["sheer_z"])):
        hw = 6.0 if y == 0.0 else 3.8          # the gunwale's sockets sit on its 6 cm cap
        m.box((x - 6.0, y - hw, z), (x + 6.0, y + hw, z + 1.2), "Frame")
        m.tube([(x, y, z + 1.2), (x, y, z + 14.0)], 3.2, "Frame", sides=12)

    # The life ring in its holder on the console's front face, where it's to hand from the foredeck and the helm.
    ring_c = (40.5, 0.0, DECK_Z + 36.0)
    m.tube([(ring_c[0], 30.0 * math.cos(a), ring_c[2] + 30.0 * math.sin(a)) for a in [2 * math.pi * i / 24 for i in range(25)]],
           5.0, "Safety", sides=10)
    for dy in (-18.0, 18.0):
        m.tube([(35.0, dy, ring_c[2] + 26.0), (41.0, dy, ring_c[2] + 26.0), (41.0, dy, ring_c[2] + 32.0)], 0.9, "Frame", sides=6)

    # Console: grab rails down both sides, the fire extinguisher on its bracket to port, and the compass up top.
    for side in (1.0, -1.0):
        y = side * 49.0
        m.tube([(-38.0, side * 45.0, DECK_Z + 70.0), (-38.0, y, DECK_Z + 70.0), (22.0, y, DECK_Z + 70.0),
                (22.0, side * 45.0, DECK_Z + 70.0)], 1.3, "Frame", sides=8)
    m.tube([(-10.0, -51.0, DECK_Z + 14.0), (-10.0, -51.0, DECK_Z + 50.0)], 5.5, "Red", sides=12)
    _dome(m, (-10.0, -51.0, DECK_Z + 50.0), 5.5, 4.0, "Red")
    m.tube([(-10.0, -51.0, DECK_Z + 54.0), (-13.0, -51.0, DECK_Z + 58.0)], 1.0, "Trim", sides=6)
    m.box((-8.0, -46.0, DECK_Z + 20.0), (-5.0, -45.0, DECK_Z + 46.0), "Frame")
    _dome(m, (-8.0, 0.0, DECK_Z + 112.0), 6.0, 5.0, "Glass")
    m.tube([(-8.0, 0.0, DECK_Z + 112.0), (-8.0, 0.0, DECK_Z + 113.5)], 6.5, "Trim", sides=12)

    # Leaning post: backrest bolster and a grab rail across its back.
    m.box((-177.0, -38.0, DECK_Z + 92.0), (-171.0, 38.0, DECK_Z + 120.0), "Cushion")
    m.tube([(-178.0, -36.0, DECK_Z + 80.0), (-182.0, -36.0, DECK_Z + 124.0), (-182.0, 36.0, DECK_Z + 124.0),
            (-178.0, 36.0, DECK_Z + 80.0)], 1.3, "Frame", sides=8)

    # Cockpit drains (scuppers) at the foot of the stern bulkhead, and the fuel filler on the starboard gunwale.
    for side in (1.0, -1.0):
        m.box((BULKHEAD_X - 0.5, side * 60.0 - 8.0, DECK_Z), (BULKHEAD_X + 0.3, side * 60.0 + 8.0, DECK_Z + 6.0), "Trim")
    fs = station(0.2)
    m.tube([(fs["x"], fs["sheer_b"] - 3.6, fs["sheer_z"]), (fs["x"], fs["sheer_b"] - 3.6, fs["sheer_z"] + 0.8)], 2.4, "Frame", sides=12)

    # Transom: hydraulic trim tabs hinged along the bottom edge between the motors and the corners, following the
    # V of the bottom and set down a few degrees, each worked by a ram from a mount higher up the transom. Clear of
    # the motors' lower units at full lock and trim, and of the ladder.
    s0 = station(0.0)
    for side in (1.0, -1.0):
        _trim_tab(m, s0, side, 60.0, 88.0)
    # The boarding ladder reaches into the water, so a swimmer can climb out (ARiptideBoat's ladder foot): two
    # stainless rails on standoffs off the transom's port corner, flat treads, and a grab handle over the top.
    lx = STERN_X - 4.0
    for y in (-114.0, -94.0):
        m.tube([(lx, y, -45.0), (lx, y, 72.0), (STERN_X + 2.0, y, 80.0), (STERN_X + 14.0, y, 80.0)], 1.2, "Frame", sides=8)
        for z in (8.0, 58.0):
            m.box((lx, y - 1.0, z - 2.0), (STERN_X, y + 1.0, z + 2.0), "Frame")
    for z in (-38.0, -22.0, -6.0, 10.0, 26.0, 42.0, 58.0):
        m.box((lx - 2.2, -113.0, z - 0.7), (lx + 2.2, -95.0, z + 0.7), "Frame")
    # Bilge pump outlets on the hull sides, aft.
    for x in (-300.0, -200.0):
        st = station((x - STERN_X) / LENGTH)
        for side in (1.0, -1.0):
            _ring(m, (x, side * (st["sheer_b"] + 0.5), st["sheer_z"] - 28.0), 2.0, 0.6, "xz", "Frame")

    # Running strakes along the bottom: they throw the spray out sideways and give the hull its grip.
    for f in (0.38, 0.68):
        for side in (1.0, -1.0):
            path = []
            for t in [0.08 + 0.8 * i / 24 for i in range(25)]:
                st = station(t)
                y = st["chine_b"] * f
                z = st["keel_z"] + (st["chine_z"] - st["keel_z"]) * (f ** 0.9) - 1.0
                path.append((st["x"], side * y, z))
            m.tube(path, 1.6, "Aluminium", sides=5)


def _bottom_z(s, y):
    """Height of the hull's bottom at half-width y in section s (the V from the keel out to the chine)."""
    f = min(1.0, abs(y) / s["chine_b"])
    return s["keel_z"] + (s["chine_z"] - s["keel_z"]) * f ** 0.9


def _trim_tab(m, s0, side, y0, y1, length=23.0, drop_deg=6.0, thick=0.8):
    """One trim tab on the transom's bottom edge from half-width y0 to y1."""
    drop = length * math.tan(math.radians(drop_deg))
    ys = [side * (y0 + (y1 - y0) * i / 4.0) for i in range(5)]
    hinge = [(STERN_X, y, _bottom_z(s0, y)) for y in ys]
    trail = [(STERN_X - length, y, _bottom_z(s0, y) - drop) for y in ys]
    top = [hinge, trail]
    bottom = [[(p[0], p[1], p[2] - thick) for p in row] for row in top]
    m.grid(top, "Frame", outward_hint=lambda p: (p[0], p[1], p[2] - 20.0))
    m.grid(bottom, "Frame", outward_hint=lambda p: (p[0], p[1], p[2] + 20.0))
    for row_a, row_b in ((trail, [(p[0], p[1], p[2] - thick) for p in trail]),):
        m.grid([row_a, row_b], "Frame", outward_hint=lambda p: (p[0] + 20.0, p[1], p[2]))
    for i in (0, -1):
        _quad(m, hinge[i], trail[i], (trail[i][0], trail[i][1], trail[i][2] - thick), (hinge[i][0], hinge[i][1], hinge[i][2] - thick),
              "Frame", (STERN_X - length / 2, 0.0, hinge[i][2]))
    m.tube([(STERN_X - 0.8, p[1], p[2] - 0.3) for p in hinge], 0.9, "Frame", sides=6)
    # The ram: its cylinder on a bracket up the transom, its rod down to a clevis on the tab's top.
    ym = side * 0.5 * (y0 + y1)
    zm = _bottom_z(s0, ym)
    upper = (STERN_X - 3.5, ym, zm + 30.0)
    lower = (STERN_X - length * 0.6, ym, zm - drop * 0.6 + 1.2)
    mid = tuple(upper[a] + (lower[a] - upper[a]) * 0.55 for a in range(3))
    m.box((STERN_X - 2.0, ym - 3.0, zm + 27.0), (STERN_X, ym + 3.0, zm + 33.0), "Trim")
    m.tube([upper, mid], 2.2, "Trim", sides=10)
    m.tube([mid, lower], 0.8, "Frame", sides=6)
    m.box((lower[0] - 2.0, ym - 1.5, lower[2] - 1.2), (lower[0] + 2.0, ym + 1.5, lower[2] + 0.6), "Frame")


def _quad(m, a, b, c, d, material, facing):
    """A flat four-cornered panel whose face points toward the point `facing`."""
    m._part([a, b, c, d], [(0, 1, 2), (0, 2, 3)], material,
            outward_hint=lambda p: (2 * p[0] - facing[0], 2 * p[1] - facing[1], 2 * p[2] - facing[2]))


def _stern_box(m, s0):
    """The bulkhead across the stern, its flat top with hatches, and the splashwell behind it in front of the motors."""
    top = s0["sheer_z"]
    inner = s0["sheer_b"] - 6.0
    x0, x1 = STERN_X + TRANSOM_THICK, BULKHEAD_X        # inside of the transom, front of the box
    cockpit = (0.0, 0.0, DECK_Z + 60.0)
    # Bulkhead facing the cockpit, deck to gunwale, the full width between the bulwarks.
    _quad(m, (x1, -inner, DECK_Z), (x1, inner, DECK_Z), (x1, inner, top), (x1, -inner, top), "HullInside", cockpit)
    # Box top either side of the well, flush with the gunwale caps, each with a hatch lid.
    for side in (1.0, -1.0):
        _quad(m, (x0 - TRANSOM_THICK, side * WELL_HALF, top), (x1, side * WELL_HALF, top), (x1, side * inner, top),
              (x0 - TRANSOM_THICK, side * inner, top), "Deck", (-360.0, side * 90.0, top + 100.0))
        hy0, hy1 = side * (WELL_HALF + 8.0), side * (inner - 16.0)      # outboard of it, the stern cleat
        m.box((x0 + 6.0, min(hy0, hy1), top), (x1 - 8.0, max(hy0, hy1), top + 0.8), "Trim")
    # Splashwell: floor, sides, the bulkhead's back, and the inside of the transom up to the sill.
    well_centre = ((x0 + x1) / 2, 0.0, (WELL_FLOOR_Z + top) / 2)
    _quad(m, (x0, -WELL_HALF, WELL_FLOOR_Z), (x1, -WELL_HALF, WELL_FLOOR_Z), (x1, WELL_HALF, WELL_FLOOR_Z),
          (x0, WELL_HALF, WELL_FLOOR_Z), "Deck", (well_centre[0], 0.0, top + 100.0))
    for side in (1.0, -1.0):
        _quad(m, (STERN_X, side * WELL_HALF, WELL_FLOOR_Z), (x1, side * WELL_HALF, WELL_FLOOR_Z),
              (x1, side * WELL_HALF, top), (STERN_X, side * WELL_HALF, top), "HullInside", well_centre)
    _quad(m, (x1, -WELL_HALF, WELL_FLOOR_Z), (x1, WELL_HALF, WELL_FLOOR_Z), (x1, WELL_HALF, top), (x1, -WELL_HALF, top),
          "HullInside", well_centre)
    _quad(m, (x0, -WELL_HALF, WELL_FLOOR_Z), (x0, WELL_HALF, WELL_FLOOR_Z), (x0, WELL_HALF, SILL_Z), (x0, -WELL_HALF, SILL_Z),
          "HullInside", well_centre)
    _quad(m, (STERN_X, -WELL_HALF, SILL_Z), (x0, -WELL_HALF, SILL_Z), (x0, WELL_HALF, SILL_Z), (STERN_X, WELL_HALF, SILL_Z),
          "Aluminium", (well_centre[0], 0.0, top + 100.0))
    # Control cables and fuel hoses from the bulkhead, down through the well and over the sill to each motor.
    for my in (-38.0, 38.0):
        for dy, r in ((-4.0, 1.6), (3.0, 1.2)):
            y = my + dy
            m.tube([(x1 - 0.5, y * 0.8, top - 12.0), (x1 - 12.0, y * 0.85, WELL_FLOOR_Z + 4.0),
                    (x0 + 8.0, y, WELL_FLOOR_Z + 3.0), (x0 + 1.0, y, SILL_Z + 3.0), (STERN_X - 6.0, y, SILL_Z + 2.0)],
                   r, "Trim", sides=6)


def _console(m):
    deck = DECK_Z
    top = deck + 112.0
    # Console: raked front, flat top behind the windscreen, and a dash sloping down toward the helm.
    profile = [(-45.0, deck), (35.0, deck), (35.0, deck + 70.0), (5.0, top), (-20.0, top), (-45.0, deck + 95.0)]
    m.prism(profile, -45.0, 45.0, "Console")
    _windscreen(m, top)
    _dash_and_wheel(m, deck)
    # Throttle binnacle on the console's back, right of the wheel: the twin levers (a separate model) pivot on top.
    bx, by, bz = BINNACLE
    m.box((bx - 4.0, by - 9.0, bz - 14.0), (-45.0, by + 9.0, bz), "Trim")
    m.tube([(bx, by - 8.0, bz), (bx, by + 8.0, bz)], 2.2, "Frame", sides=10)

    # Leaning post behind the helm, with a padded bolster. It leaves about 75 cm to stand in behind the wheel, room
    # to stay clear of it with the boat pitching.
    m.box((-175.0, -40.0, deck), (-145.0, 40.0, deck + 70.0), "Console")
    m.box((-177.0, -42.0, deck + 70.0), (-143.0, 42.0, deck + 92.0), "Cushion")


def _windscreen(m, base_z):
    """Wraparound windscreen: a front pane that curves round into side wings, raked back, in an aluminium frame with a
    grab rail along the top. Low enough to look over standing at the helm, like a centre console's."""
    height = 38.0
    front_lean, wing_lean = height * math.tan(math.radians(25.0)), height * math.tan(math.radians(8.0))
    # Base outline on the console top (plan view), from the port wing round the front to the starboard wing.
    corner_r, front_x, half = 13.0, 5.0, 45.0
    cx, cy = front_x - corner_r, half - corner_r
    port = [(-20.0, -half), (cx, -half)]
    port += [(cx + corner_r * math.sin(a), -cy - corner_r * math.cos(a)) for a in [math.radians(15 * i) for i in range(1, 6)]]
    port += [(front_x, -cy)]
    base_xy = port + [(x, -y) for x, y in reversed(port)]

    def inward(i):
        # Horizontal normal pointing into the console at point i, from the outline's direction there.
        a, b = base_xy[max(i - 1, 0)], base_xy[min(i + 1, len(base_xy) - 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        l = math.hypot(tx, ty)
        return (-ty / l, tx / l)   # the outline runs port -> front -> starboard, so this turns toward the centreline

    base, top_row = [], []
    for i, (x, y) in enumerate(base_xy):
        nx, ny = inward(i)
        lean = front_lean * nx * nx + wing_lean * ny * ny   # the front rakes well back; the wings stand nearly upright
        base.append((x, y, base_z))
        top_row.append((x + nx * lean, y + ny * lean, base_z + height))
    m.grid([base, top_row], "Glass", outward_hint=lambda p: (-60.0, 0.0, p[2]))
    # Frame: a grab rail along the top, posts at the wing ends and the front corners, and a trim strip along the base.
    m.tube(top_row, 1.8, "Frame", sides=8)
    for i in (0, 2 + 5 - 1, len(base) - 1 - (2 + 5 - 1), len(base) - 1):
        m.tube([base[i], top_row[i]], 1.4, "Frame", sides=8)
    m.tube(base, 1.2, "Trim", sides=6)


def _dash_and_wheel(m, deck):
    """Instrument panel on the dash slope, facing the helmsman, and the wheel on a tilted shaft below it."""
    # The dash slope runs from (-45, deck + 95) up to (-20, deck + 112) in X/Z.
    lo, hi = (-45.0, deck + 95.0), (-20.0, deck + 112.0)
    sx, sz = hi[0] - lo[0], hi[1] - lo[1]
    sl = math.hypot(sx, sz)
    ux, uz = sx / sl, sz / sl          # up the slope
    nx, nz = -uz, ux                   # out of the slope, toward the helm (back and up)

    def on_slope(f, y, out=0.6):
        return (lo[0] + sx * f + nx * out, y, lo[1] + sz * f + nz * out)
    # Black panel across the dash, with a chart display in the middle and a gauge either side.
    panel = [on_slope(0.05, -40.0), on_slope(0.05, 40.0), on_slope(0.95, 40.0), on_slope(0.95, -40.0)]
    m._part(panel, [(0, 1, 2), (0, 2, 3)], "Trim", outward_hint=lambda p: (p[0] - 50.0 * nx, p[1], p[2] - 50.0 * nz))
    # The screen's bezel and the gauges' chrome rings; the faces are live displays (ARiptideBoat's gauges).
    sy0, sy1, sf0, sf1 = 17.0, 17.0, 0.12, 0.9
    bezel = [on_slope(sf0, -sy0), on_slope(sf0, sy1), on_slope(sf1, sy1), on_slope(sf1, -sy0), on_slope(sf0, -sy0)]
    m.tube([(p[0] + nx * 1.0, p[1], p[2] + nz * 1.0) for p in bezel], 0.7, "Trim", sides=6)
    for gy in (-28.5, 28.5):
        c = on_slope(0.5, gy, 1.2)
        ring = [(c[0] + 7.2 * math.sin(a) * ux, c[1] + 7.2 * math.cos(a), c[2] + 7.2 * math.sin(a) * uz)
                for a in [2 * math.pi * i / 24 for i in range(25)]]
        m.tube(ring, 0.9, "Frame", sides=6)

    # Wheel: tilted back toward the helmsman, on a shaft coming out of the console's rear face.
    tilt = math.radians(35.0)
    axis = (-math.cos(tilt), 0.0, math.sin(tilt))      # the wheel faces back and up
    up = (math.sin(tilt), 0.0, math.cos(tilt))          # in the wheel's plane, pointing up
    centre = (-53.0, 0.0, deck + 88.0)
    hub_in = (centre[0] - axis[0] * 10.0, 0.0, centre[2] - axis[2] * 10.0)
    m.tube([hub_in, centre], 2.2, "Trim", sides=8)
    radius = 18.0

    def rim(a, r=radius):
        return (centre[0] + r * math.sin(a) * up[0], r * math.cos(a), centre[2] + r * math.sin(a) * up[2])
    m.tube([rim(a) for a in [2 * math.pi * i / 24 for i in range(25)]], 1.7, "Trim", sides=8)
    for k in range(3):
        a = math.pi / 2 + 2 * math.pi * k / 3
        m.tube([centre, rim(a, radius - 1.0)], 1.0, "Trim", sides=6)


def _t_top(m):
    deck = DECK_Z
    top = DECK_Z + 220.0
    # Legs close in against the console and leaning post, leaving the side decks clear to walk.
    legs = [(-150.0, 48.0), (-150.0, -48.0), (20.0, 48.0), (20.0, -48.0)]
    for x, y in legs:
        m.tube([(x, y, deck), (x, y * 1.05, deck + 110.0), (x + (8.0 if x > 0 else -8.0), y * 1.25, top)], 3.5, "Frame")
    # Canopy frame and roof.
    ring = [(-158.0, 60.0, top), (28.0, 60.0, top), (28.0, -60.0, top), (-158.0, -60.0, top), (-158.0, 60.0, top)]
    m.tube(ring, 3.5, "Frame")
    m.box((-165.0, -70.0, top + 2.0), (35.0, 70.0, top + 8.0), "Canopy")
    roof = top + 8.0
    # Searchlight (starboard) and FLIR thermal camera ball (port) at the front edge, each on a pan-tilt base. The
    # searchlight's head is its own model (build_searchlight), aimed from the helm.
    m.tube([(26.0, 40.0, roof), (26.0, 40.0, roof + 8.0)], 5.0, "Trim", sides=12)
    m.tube([(26.0, -40.0, roof), (26.0, -40.0, roof + 6.0)], 5.0, "Trim", sides=12)
    _ball(m, (26.0, -40.0, roof + 15.0), 9.0, "White")
    m.fan([(26.0 + 8.6, -40.0 + 4.0 * math.cos(a), roof + 15.0 + 4.0 * math.sin(a)) for a in [2 * math.pi * i / 12 for i in range(12)]],
          "Trim", outward_hint=lambda p: (0.0, -40.0, roof + 15.0))
    # Open-array radar pedestal (the array itself is a separate, spinning model: build_radar_array).
    m.box((-80.0, -12.0, roof), (-60.0, 12.0, roof + 22.0), "White")
    m.tube([(RADAR[0], 0.0, roof + 22.0), (RADAR[0], 0.0, RADAR[2])], 5.0, "White", sides=12)
    # Satellite comms dome aft to port, two GPS antennas to starboard.
    _dome(m, (-140.0, -45.0, roof), 20.0, 26.0, "White")
    for x, y in ((-125.0, 35.0), (-100.0, 57.0)):
        m.tube([(x, y, roof), (x, y, roof + 3.0)], 2.0, "White", sides=8)
        _dome(m, (x, y, roof + 3.0), 6.5, 5.0, "White")
    # VHF whips (2.4 m) on ratchet mounts at the aft corners, raked slightly aft, and a short AIS whip.
    for y in (-62.0, 62.0):
        m.box((-162.0, y - 3.0, roof), (-156.0, y + 3.0, roof + 6.0), "Frame")
        m.tube([(-159.0, y, roof + 6.0), (-175.0, y * 1.02, roof + 240.0)], 1.2, "White", sides=6)
    m.tube([(-150.0, 20.0, roof), (-153.0, 20.0, roof + 95.0)], 0.9, "Trim", sides=6)
    # All-round white masthead light on a pole, aft centre, clear above everything.
    m.tube([(-162.0, 0.0, roof), (-162.0, 0.0, roof + 85.0)], 2.0, "Frame", sides=8)
    m.tube([(-162.0, 0.0, roof + 85.0), (-162.0, 0.0, roof + 93.0)], 3.2, "NavWhite", sides=12)
    m.box((-165.0, -3.5, roof + 93.0), (-159.0, 3.5, roof + 95.0), "Trim")
    # Loudhailer horn under the canopy's front lip, and the ship's horn beside it.
    m.box((24.0, -8.0, top - 12.0), (32.0, 8.0, top + 2.0), "Trim")
    _cone(m, (32.0, 0.0, top - 5.0), (46.0, 0.0, top - 5.0), 4.0, 10.0, "White")
    _cone(m, (22.0, 20.0, top - 6.0), (34.0, 20.0, top - 6.0), 2.0, 5.0, "Frame")
    # Overhead electronics box above the helm with two VHF radios and a speaker in its face.
    m.box((-75.0, -32.0, top - 22.0), (-15.0, 32.0, top + 2.0), "Console")
    for y0, y1 in ((-28.0, -6.0), (-2.0, 20.0)):
        m.box((-76.0, y0, top - 18.0), (-75.0, y1, top - 6.0), "Trim")
        m.box((-76.4, y0 + 2.0, top - 14.0), (-76.0, y0 + 10.0, top - 9.0), "Lamp")
    m.tube([(-75.5, 26.0, top - 11.0), (-76.5, 26.0, top - 11.0)], 4.0, "Trim", sides=10)
    # (The life ring is on the console's front: see _fittings.)


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
    _fittings(m)
    return m


SEARCHLIGHT = (26.0, 40.0, DECK_Z + 220.0 + 8.0 + 16.0)   # the head's pivot (ARiptideBoat's SearchlightPivot)


def build_searchlight():
    """The searchlight's head: origin at its pivot, the beam along +X, on a yoke that sits on the T-top base."""
    m = Mesh()
    m.tube([(-6.0, 0.0, 0.0), (12.0, 0.0, 0.0)], 8.0, "Frame", sides=16)
    m.fan([(-6.0, 7.9 * math.cos(a), 7.9 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]], "Frame",
          outward_hint=lambda p: (6.0, 0.0, 0.0))
    m.fan([(12.0, 7.2 * math.cos(a), 7.2 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]], "Lamp",
          outward_hint=lambda p: (0.0, 0.0, 0.0))
    for y in (-9.5, 9.5):
        m.box((-2.0, y - 1.0, -8.0), (4.0, y + 1.0, 1.0), "Trim")
    m.box((-2.0, -10.5, -9.0), (4.0, 10.5, -7.0), "Trim")
    m.tube([(-6.0, 0.0, 5.0), (-10.0, 0.0, 9.0)], 1.0, "Trim", sides=6)   # handle
    return m


def build_radar_array():
    """The open-array radar antenna that spins on the T-top: origin at its hub, the array lying fore and aft."""
    m = Mesh()
    m.tube([(0.0, 0.0, -4.0), (0.0, 0.0, 2.0)], 7.0, "White", sides=14)
    m.box((-55.0, -3.0, 2.0), (55.0, 3.0, 12.0), "White")
    m.box((-55.5, -3.5, 5.0), (55.5, -3.0, 9.0), "Trim")
    return m


def _foil_ring(z, le_x, chord, thick, n=10):
    """A horizontal slice through a streamlined leg: a symmetric foil (NACA 00xx, closed tail) with its leading edge
    forward at le_x (+X, toward the boat), `chord` long and `thick` at its widest, as a closed ring of points."""
    def half(xc):
        return 5.0 * thick * (0.2969 * math.sqrt(xc) - 0.126 * xc - 0.3516 * xc ** 2 + 0.2843 * xc ** 3 - 0.1036 * xc ** 4)
    xcs = [0.5 * (1.0 - math.cos(math.pi * i / n)) for i in range(n + 1)]          # bunched at nose and tail
    upper = [(le_x - chord * xc, half(xc), z) for xc in xcs]
    lower = [(le_x - chord * xc, -half(xc), z) for xc in reversed(xcs[1:-1])]
    return upper + lower


def _superellipse_ring(z, cx, sx, sy, n=3.0, count=24):
    """A rounded-rectangle slice (a cowling's): half-length sx, half-width sy, centred at x = cx."""
    pts = []
    for i in range(count):
        a = 2 * math.pi * i / count
        c, s = math.cos(a), math.sin(a)
        pts.append((cx + sx * math.copysign(abs(c) ** (2.0 / n), c), sy * math.copysign(abs(s) ** (2.0 / n), s), z))
    return pts


def _loft(m, rings, material, centre_x, cap_bottom=False, cap_top=False):
    """A closed surface through horizontal rings (each the same number of points), optionally capped."""
    m.grid(rings, material, outward_hint=lambda p: (centre_x, 0.0, p[2]), close_rows=True)
    if cap_bottom:
        m.fan(rings[0], material, outward_hint=lambda p: (centre_x, 0.0, p[2] + 10.0))
    if cap_top:
        m.fan(rings[-1], material, outward_hint=lambda p: (centre_x, 0.0, p[2] - 10.0))


# The outboard's lower unit, in its own frame (origin at the tilt tube, the transom's outer face at X = +5): a 25 in
# shaft puts the anti-ventilation plate 64 cm under the clamp, 2-3 cm below the hull's bottom at the motor, and the
# prop's centre 22 cm under the plate, so its tips just clear it.
PLATE_Z = -78.0
PROP_CENTRE = (-45.0, 0.0, -100.0)      # the boat's PropInOutboard
PROP_RADIUS = 19.0                       # a 15 in x 19 in stainless three-blade


def build_outboard():
    """A big outboard (a 300 hp V8 on a 25 in shaft). Origin = the tilt tube at the top of its clamp bracket (a
    separate model, fixed to the transom): the motor trims and steers about it. The prop sits at PROP_CENTRE."""
    m = Mesh()
    pz = PLATE_Z
    # Cowling: flat-sided and round-cornered, tallest at the back, over a lower cowl (the chaps) that narrows down
    # into the midsection. A black split line where the top cowl lifts off.
    chaps = [_superellipse_ring(z, cx, sx, sy) for z, cx, sx, sy in (
        (-16.0, -24.0, 20.0, 9.0), (-6.0, -25.0, 27.0, 19.0), (4.0, -26.0, 30.0, 25.0))]
    _loft(m, chaps, "Cowling", -26.0, cap_bottom=True)
    top = [_superellipse_ring(z, cx, sx, sy) for z, cx, sx, sy in (
        (4.0, -26.0, 30.0, 25.0), (12.0, -26.0, 33.5, 28.0), (46.0, -27.0, 34.0, 28.5), (64.0, -28.5, 31.5, 26.5),
        (74.0, -30.0, 26.0, 21.5), (80.0, -31.0, 17.0, 13.5), (82.5, -31.5, 7.0, 5.0))]
    _loft(m, top, "Cowling", -27.0, cap_top=True)
    split = _superellipse_ring(8.0, -26.0, 32.3, 26.8)
    m.tube(split + [split[0]], 0.8, "Trim", sides=6)
    # Air intake grille across the top of the back, and the latch at the front.
    for i in range(4):
        z = 58.0 + 3.0 * i
        m.box((-62.0 + 0.4 * i, -12.0, z), (-60.0 + 0.4 * i, 12.0, z + 1.2), "Trim")
    m.box((6.8, -5.0, 20.0), (7.8, 5.0, 24.0), "Trim")

    # Steering: the swivel bracket hangs off the tilt tube and carries the steering tube; the motor rides on rubber
    # mounts on it, just in front of the midsection.
    for y in (-11.0, 11.0):
        m.box((-6.0, y - 2.0, -4.0), (1.0, y + 2.0, 4.0), "Trim")
    m.box((-6.0, -10.0, -3.0), (-2.0, 10.0, 3.0), "Trim")
    m.tube([(-4.5, 0.0, -24.0), (-4.5, 0.0, 3.0)], 3.5, "Trim", sides=10)
    m.box((-10.0, -6.0, -24.0), (-3.0, 6.0, -18.0), "Trim")

    # Midsection: the driveshaft housing, a streamlined leg from inside the chaps down to the plate.
    mid = [_foil_ring(z, le, c, t) for z, le, c, t in (
        (-12.0, -8.0, 36.0, 13.0), (-40.0, -7.0, 35.0, 12.0), (pz + 1.0, -6.0, 34.0, 11.0))]
    _loft(m, mid, "Cowling", -24.0)

    # Anti-ventilation plate: widest over the prop, round at the back, its front wrapped round the leg.
    half_outline = [(-5.0, 0.0), (-9.0, 5.0), (-18.0, 9.5), (-30.0, 13.0), (-42.0, 14.0), (-51.0, 12.0), (-56.0, 7.0),
                    (-57.5, 0.0)]
    outline = half_outline + [(x, -y) for x, y in reversed(half_outline[1:-1])]
    z0, z1 = pz, pz + 1.6
    m.fan([(x, y, z1) for x, y in outline], "Cowling", outward_hint=lambda p: (p[0], p[1], p[2] - 10.0))
    m.fan([(x, y, z0) for x, y in outline], "Cowling", outward_hint=lambda p: (p[0], p[1], p[2] + 10.0))
    m.grid([[(x, y, z0), (x, y, z1)] for x, y in outline + [outline[0]]], "Cowling", outward_hint=lambda p: (-30.0, 0.0, p[2]))
    # Its trim-tab anode under the back of the plate.
    m.prism([(-40.0, pz), (-50.0, pz), (-49.0, pz - 5.0), (-42.0, pz - 5.0)], -1.0, 1.0, "Prop")

    # Gearcase: the strut from the plate down to the torpedo (thinner than the leg), as one piece with it.
    cx, cy, cz = PROP_CENTRE
    strut = [_foil_ring(z, le, c, t) for z, le, c, t in (
        (pz + 0.5, -6.0, 34.0, 9.0), (pz - 12.0, -5.0, 33.0, 8.0), (cz + 2.0, -4.0, 32.0, 7.0))]
    _loft(m, strut, "Cowling", -22.0)
    # Water intakes on its sides.
    for side in (1.0, -1.0):
        m.box((-22.0, min(side * 3.4, side * 4.2), pz - 13.0), (-12.0, max(side * 3.4, side * 4.2), pz - 5.0), "Trim")
    # Torpedo: the bullet that holds the gears, nose forward, running back into the prop hub.
    torpedo = []
    for i in range(13):
        f = i / 12.0
        x = 3.0 - 41.0 * f
        r = 7.5 * math.sin(math.pi / 2 * min(1.0, f / 0.3)) ** 0.5 if f < 0.6 else 7.5 - 2.2 * (f - 0.6) / 0.4
        r = max(r, 0.8)
        torpedo.append([(x, r * math.cos(a), cz + r * math.sin(a)) for a in [2 * math.pi * j / 14 for j in range(14)]])
    m.grid(torpedo, "Cowling", outward_hint=lambda p: (p[0], 0.0, cz), close_rows=True)
    m.fan(torpedo[0], "Cowling", outward_hint=lambda p: (p[0] - 10.0, 0.0, cz))
    # Skeg under it, raked back, reaching just below the prop's tips to guard them.
    m.prism([(-10.0, cz - 5.0), (-37.0, cz - 5.0), (-36.0, cz - PROP_RADIUS - 5.0), (-28.0, cz - PROP_RADIUS - 6.5)], -1.3, 1.3,
            "Cowling")

    # Propeller: hub, exhaust through its centre, and three pitched, skewed blades.
    m.tube([(-38.0, 0.0, cz), (-45.0, 0.0, cz), (-51.0, 0.0, cz)], 5.3, "Prop", sides=14)
    _cone(m, (-51.0, 0.0, cz), (-54.0, 0.0, cz), 5.3, 4.2, "Prop", sides=14)
    m.fan([(-54.0, 4.2 * math.cos(a), cz + 4.2 * math.sin(a)) for a in [2 * math.pi * j / 14 for j in range(14)]], "Trim",
          outward_hint=lambda p: (-40.0, 0.0, cz))
    pitch = 48.0
    hub_r = 5.0
    for k in range(3):
        base = 2 * math.pi * k / 3 + 0.4
        rows = []
        for u in (0.0, 0.2, 0.4, 0.6, 0.8, 0.93, 1.0):
            r = hub_r + (PROP_RADIUS - hub_r) * u
            chord = 7.0 + 13.0 * math.sin(math.pi * u) ** 0.7 if u < 1.0 else 3.0
            skew = 0.35 * u * u                            # the blades sweep back against the turn
            rake = -2.5 * u                                # and lean aft
            row = []
            for j in range(5):
                dphi = (j / 4.0 - 0.5) * chord / r
                x = cx + rake - dphi * pitch / (2 * math.pi)
                row.append((x, r * math.cos(base + skew + dphi), cz + r * math.sin(base + skew + dphi)))
            rows.append(row)
        m.grid(rows, "Prop")
        m.grid([list(reversed(row)) for row in rows], "Prop")
    return m


def build_outboard_bracket():
    """The clamp bracket that stays on the transom while the motor trims: a plate down the transom's outer face, a
    hook over the sill, and the tilt tube. Same origin as build_outboard (the tilt tube)."""
    m = Mesh()
    m.box((-1.0, -12.0, -28.0), (5.0, 12.0, 6.0), "Trim")          # against the transom (its outer face is at X = +5)
    m.box((5.0, -12.0, 5.0), (5.0 + TRANSOM_THICK + 2.0, 12.0, 8.0), "Trim")   # hooked over the sill
    m.box((5.0 + TRANSOM_THICK, -12.0, -6.0), (5.0 + TRANSOM_THICK + 2.0, 12.0, 8.0), "Trim")
    m.tube([(0.0, -16.0, 0.0), (0.0, 16.0, 0.0)], 2.6, "Cowling", sides=10)    # tilt tube
    for y in (-8.0, 8.0):                                                         # clamp screws
        m.tube([(-1.0, y, -20.0), (-5.0, y, -20.0)], 1.5, "Prop", sides=8)
    return m


def build_throttle_lever():
    """One lever of the twin throttle: origin at its pivot, standing up, with a black grip across the top."""
    m = Mesh()
    m.tube([(0.0, 0.0, 0.0), (0.0, 0.0, 17.0)], 1.1, "Frame", sides=8)
    m.tube([(0.0, -3.5, 18.0), (0.0, 3.5, 18.0)], 1.9, "Trim", sides=10)
    return m


if __name__ == "__main__":
    import os
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, mesh in (("skiff", build_skiff()), ("outboard", build_outboard()), ("bracket", build_outboard_bracket()),
                       ("lever", build_throttle_lever()), ("radar", build_radar_array()), ("searchlight", build_searchlight())):
        mesh.write_obj(os.path.join(out, name + ".obj"))
        print(name, len(mesh.verts), "verts", sum(len(f) for f in mesh.faces.values()), "tris")
