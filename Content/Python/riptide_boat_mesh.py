"""Builds the patrol skiff model in code and writes it as OBJ files that Unreal imports.

No third-party model: the hull is lofted from cross-sections, the fittings are simple solids. Pure Python (no
Unreal), so it runs anywhere. Coordinates are Unreal's: X forward, Y right (starboard), Z up, centimetres, with
the origin at the centre of the boat's physics box (HullExtent 395 x 130 x 35 in ARiptideBoat), so the waterline
sits at Z = -15. Full size for a 26 ft patrol boat: 7.9 m long, 2.6 m beam, with room to walk around the console.

    build_skiff()             -> Mesh   hull, collar, deck, console, leaning post, T-top, rails and fittings
    build_outboard()          -> Mesh   one outboard, origin at its tilt tube; the boat carries two
    build_outboard_bracket()  -> Mesh   its clamp bracket on the transom (fixed)
    build_outboard_swivel()   -> Mesh   its swivel bracket (trims, doesn't steer)
    Mesh.write_obj(path)
"""

import math

# --- Shape ------------------------------------------------------------------------------------------------------

LENGTH = 790.0           # stern at X = -395, stem at X = +395
HALF_BEAM = 130.0
STERN_X = -395.0
STATIONS = 60
DECK_Z = 20.0            # the cockpit deck, 35 cm above the waterline (self-bailing, and clear of the swell)
GUNWALE_W = 14.0         # width of the gunwale cap: the bulwark walls stand this far in from the sheer
COLLAR_OVER = 3.0        # the fender collar's top laps this far in over the cap; fittings go inboard of it
GUNWALE_MID = 0.5 * (COLLAR_OVER + GUNWALE_W)      # the middle of the cap's exposed width, where fittings sit

# The stern: a bulkhead across the boat in front of the motors, gunwale-high, closing the cockpit off from the sea.
# Behind it the transom is notched down to where the motors clamp on, with a splashwell between the two (its floor
# well above the waterline) to catch spray coming over the notch.
BULKHEAD_X = -340.0      # aft face of the cockpit
WELL_HALF = 80.0         # half the width of the motor notch and splashwell (the cowlings swing into it at full lock)
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
    keel_z = -42.0 + 50.0 * smoothstep(0.55, 1.0, t) ** 1.7   # the forefoot rising fair into the stem
    chine_b = sheer_b * (0.88 - 0.1 * smoothstep(0.6, 1.0, t))
    chine_z = -24.0 + 34.0 * smoothstep(0.55, 1.0, t)       # chine sweeps up into the bow
    chine_z = min(chine_z, sheer_z - 6.0)
    # The keel runs up into the stem to meet the chines there, always below them, the V sharpening to the very tip.
    keel_z = min(keel_z, chine_z - 3.0 - 15.0 * (1.0 - smoothstep(0.93, 1.0, t)))
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
            if n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-8:
                continue        # zero area (where a row closes to a point: a pole, the stem): nothing to draw
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

    def tube(self, path, radius, material, sides=10, closed=False, caps=False):
        """Round tube along a polyline. The cross-section is carried along the path without twisting (parallel
        transport) and mitred at bends, so the tube keeps its full thickness round corners. closed: the path is a
        loop (a ring), joined back to its start. caps: close the two ends with flat discs (for ends left in the
        open; ends buried in another part can stay open)."""
        path = list(path)
        if closed and len(path) > 2 and sum((path[0][a] - path[-1][a]) ** 2 for a in range(3)) < 1e-6:
            path = path[:-1]
        # Drop repeated points: they have no direction.
        path = [p for i, p in enumerate(path) if i == 0 or sum((p[a] - path[i - 1][a]) ** 2 for a in range(3)) > 1e-8]
        n = len(path)
        if closed:
            segs = [norm(sub(path[(i + 1) % n], path[i])) for i in range(n)]
        else:
            segs = [norm(sub(path[i + 1], path[i])) for i in range(n - 1)]
        rows, u = [], None
        for i, p in enumerate(path):
            din, dout = (segs[i - 1], segs[i]) if closed else (segs[max(i - 1, 0)], segs[min(i, n - 2)])
            d = norm((din[0] + dout[0], din[1] + dout[1], din[2] + dout[2]))
            if u is None:
                ref = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
                u = norm(cross(d, ref))
            else:
                k = u[0] * d[0] + u[1] * d[1] + u[2] * d[2]
                u = norm((u[0] - k * d[0], u[1] - k * d[1], u[2] - k * d[2]))
            v = cross(d, u)
            c = max(0.35, din[0] * d[0] + din[1] * d[1] + din[2] * d[2])      # cos of half the bend
            b = sub(dout, din)
            bl = math.sqrt(b[0] ** 2 + b[1] ** 2 + b[2] ** 2)
            b = (b[0] / bl, b[1] / bl, b[2] / bl) if bl > 1e-6 else (0.0, 0.0, 0.0)
            row = []
            for s in range(sides):
                a = 2 * math.pi * s / sides
                o = [radius * (math.cos(a) * u[j] + math.sin(a) * v[j]) for j in range(3)]
                ob = o[0] * b[0] + o[1] * b[1] + o[2] * b[2]
                row.append(tuple(p[j] + o[j] + b[j] * ob * (1.0 / c - 1.0) for j in range(3)))
            rows.append(row)
        if closed:
            rows.append(rows[0])
        centre = path + ([path[0]] if closed else [])

        def hint(q):
            return min(centre, key=lambda c_: (c_[0] - q[0]) ** 2 + (c_[1] - q[1]) ** 2 + (c_[2] - q[2]) ** 2)
        self.grid(rows, material, outward_hint=hint, close_rows=True)
        if caps and not closed:
            # Each disc faces away from the next point along the tube.
            self.fan(rows[0], material, outward_hint=lambda q, inner=path[1]: inner)
            self.fan(rows[-1], material, outward_hint=lambda q, inner=path[-2]: inner)

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

        # Inside: bulwark wall from the gunwale down to the deck, the gunwale cap's width in from the sheer.
        wall = [[(s["x"], side * max(0.0, s["sheer_b"] - GUNWALE_W), s["sheer_z"]),
                 (s["x"], side * max(0.0, s["sheer_b"] - GUNWALE_W), s["deck_z"])] for s in secs[:-2]]
        m.grid(wall, "HullInside", outward_hint=lambda p: (p[0], side * 500.0, p[2]))
        # Gunwale cap closing the top of the shell, right to the stem, where the two sides meet (and the bow platform
        # fills between their inner edges).
        cap = [[(s["x"], side * s["sheer_b"], s["sheer_z"]), (s["x"], side * max(0.0, s["sheer_b"] - GUNWALE_W), s["sheer_z"])]
               for s in secs]
        m.grid(cap, "Aluminium", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))

    _collar(m, secs)

    # Deck: flat between the bulwark walls, rising into the bow.
    deck = [[(s["x"], -max(0.0, s["sheer_b"] - GUNWALE_W), s["deck_z"]), (s["x"], max(0.0, s["sheer_b"] - GUNWALE_W), s["deck_z"])]
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
    # Where the deck ends, the walls have met over the stem and the gunwale caps close the bow; only with a
    # narrow gunwale is there a gap between the walls there, which a bulkhead and a bow platform fill.
    if se["sheer_b"] - GUNWALE_W > 0.5:
        m.fan([(se["x"], -max(0.0, se["sheer_b"] - GUNWALE_W), se["deck_z"]), (se["x"], max(0.0, se["sheer_b"] - GUNWALE_W), se["deck_z"]),
               (se["x"], max(0.0, se["sheer_b"] - GUNWALE_W), se["sheer_z"]), (se["x"], -max(0.0, se["sheer_b"] - GUNWALE_W), se["sheer_z"])],
              "HullInside", outward_hint=lambda p: (p[0] + 50.0, 0.0, p[2]))

        # Bow platform (the anchor locker's lid) closing the stem over the end of the deck at gunwale height, so the bow
        # reads as solid from inside the boat rather than showing the backs of the hull's outer faces.
        tip = secs[-1]
        rim = [(s["x"], max(0.0, s["sheer_b"] - GUNWALE_W), s["sheer_z"]) for s in secs[-3:-1]]
        outline = [(se["x"], -rim[0][1], rim[0][2]), (rim[1][0], -rim[1][1], rim[1][2]), (tip["x"], 0.0, tip["sheer_z"]),
                   (rim[1][0], rim[1][1], rim[1][2]), (se["x"], rim[0][1], rim[0][2])]
        m.fan(outline, "Deck", outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))
        # Its edges down to the hull sides, so there's no gap under the lid where the walls stop.
        for side in (1.0, -1.0):
            edge = [[(x, side * y, z), (x, side * y, z - 30.0)] for x, y, z in
                    [(se["x"], rim[0][1], rim[0][2]), (rim[1][0], rim[1][1], rim[1][2]), (tip["x"], 0.0, tip["sheer_z"])]]
            m.grid(edge, "HullInside", outward_hint=lambda p: (p[0], side * 500.0, p[2]))


# The fender collar's cross-section: (out from the sheer, up from it) going round the tube. Its top laps in over the
# gunwale cap by COLLAR_OVER, and its lowest point is a lip tucked into the topsides, so no slit shows anywhere.
COLLAR_PROFILE = ((-COLLAR_OVER, -0.2), (-1.0, 4.0), (7.0, 1.0), (9.0, -6.0), (7.0, -13.0), (2.5, -17.0), (-1.5, -20.0))
COLLAR_CORE = (3.0, -5.0)


def _collar(m, secs):
    """The fender collar: one black rubber tube from the port quarter round the stem to the starboard quarter, its
    ends rounded off just behind the transom corners and wrapped round the bow (a little slimmer there)."""
    rows, cores = [], []

    def add(x0, y0, z0, nx, ny, scale=1.0, lap=1.0, bulge=1.0):
        # scale shrinks the section about its core (to round off the ends); lap scales the parts that reach in over
        # the cap and into the topsides, which narrow to nothing at the stem where the two sides meet, and bulge
        # how far it stands out there, so the tube wraps round the bow without folding through itself.
        ring = []
        for o, dz in COLLAR_PROFILE:
            o = o * lap if o < 0.0 else o * bulge
            o = COLLAR_CORE[0] * bulge + (o - COLLAR_CORE[0] * bulge) * scale
            dz = COLLAR_CORE[1] + (dz - COLLAR_CORE[1]) * scale
            ring.append((x0 + nx * o, y0 + ny * o, z0 + dz))
        rows.append(ring)
        cores.append((x0 + nx * COLLAR_CORE[0] * bulge, y0 + ny * COLLAR_CORE[0] * bulge, z0 + COLLAR_CORE[1]))
    s0, tip = secs[0], secs[-1]
    ends = ((3, 0.25), (2, 0.6), (1, 0.88))
    for k, sc in ends:                                   # the port end, rounded off behind the transom
        add(STERN_X - 2.0 * k, -s0["sheer_b"], s0["sheer_z"], 0.0, -1.0, sc)
    for s in secs:
        add(s["x"], -s["sheer_b"], s["sheer_z"], 0.0, -1.0, 1.0, min(1.0, s["sheer_b"] / 6.0))
    for k in range(1, 8):                                # round the stem
        th = math.pi * k / 8
        add(tip["x"], 0.0, tip["sheer_z"], math.sin(th), -math.cos(th), 1.0, 0.0, 1.0 - 0.4 * math.sin(th))
    for s in reversed(secs):
        add(s["x"], s["sheer_b"], s["sheer_z"], 0.0, 1.0, 1.0, min(1.0, s["sheer_b"] / 6.0))
    for k, sc in reversed(ends):                         # and the starboard end
        add(STERN_X - 2.0 * k, s0["sheer_b"], s0["sheer_z"], 0.0, 1.0, sc)

    def core(p):
        return min(cores, key=lambda c: (c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2)
    # Round the tube, with a hard edge at the tucked-in lip (its two faces meet at a sharp angle there).
    m.grid(rows, "Collar", outward_hint=core)
    m.grid([[r[-1], r[0]] for r in rows], "Collar", outward_hint=core)
    m.fan(rows[0], "Collar", outward_hint=lambda p: (p[0] + 50.0, p[1], p[2]))
    m.fan(rows[-1], "Collar", outward_hint=lambda p: (p[0] + 50.0, p[1], p[2]))


RADAR = (-70.0, 0.0, DECK_Z + 220.0 + 8.0 + 30.0)   # spinning array's hub (ARiptideBoat's RadarHub)

# Fittings that the game also knows the place of (ARiptideBoat): the fuel filler on the starboard gunwale
# (GetFuelFillerTransform), the bow light (BowLightPoint) and the anchor locker's lid (the anchor locker's spot).
FUEL_FILLER = (-237.0, station(0.2)["sheer_b"] - GUNWALE_MID, station(0.2)["sheer_z"])
BOW_LIGHT = (381.0, 0.0, station((381.0 - STERN_X) / LENGTH)["sheer_z"] + 4.0)    # the lamps' centre
ANCHOR_LID_X = 375.0
# The gunwale grab rails: from the stern quarters to abreast of the console, GRAB_RAIL_H above the cap.
GRAB_RAIL_X = (-290.0, -40.0)
GRAB_RAIL_STANCHIONS = (-200.0, -120.0)
GRAB_RAIL_H = 25.0
# The boarding ladder: its rails reach this far down, and its treads (ARiptideBoat's ladder foot is among them).
LADDER_BOTTOM_Z = -75.0
LADDER_RUNGS = (-70.0, -54.0, -38.0, -22.0, -6.0, 10.0, 26.0, 42.0, 58.0)


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
    m.fan(pts[0], material, outward_hint=lambda p: (c[0], c[1], c[2] + 10.0))     # its flat base


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
    # The inside of the bell, so it's solid looking into it too.
    m.grid(rows, material, outward_hint=lambda p: (2 * p[0] - min(axis_pts, key=lambda q: sum((q[k] - p[k]) ** 2 for k in range(3)))[0],
                                                   2 * p[1] - min(axis_pts, key=lambda q: sum((q[k] - p[k]) ** 2 for k in range(3)))[1],
                                                   2 * p[2] - min(axis_pts, key=lambda q: sum((q[k] - p[k]) ** 2 for k in range(3)))[2]),
           close_rows=True)


def _ring(m, c, r, tube_r, plane, material):
    """A small eye or ring: plane "xz" (faces sideways) or "yz" (faces fore and aft)."""
    pts = []
    for i in range(16):
        a = 2 * math.pi * i / 16
        if plane == "xz":
            pts.append((c[0] + r * math.cos(a), c[1], c[2] + r * math.sin(a)))
        else:
            pts.append((c[0], c[1] + r * math.cos(a), c[2] + r * math.sin(a)))
    m.tube(pts, tube_r, material, sides=6, closed=True)


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
    _obox(m, T, (-7.0, -2.4, -0.3), (7.0, 2.4, 1.0), "Frame")
    for s in (-4.0, 4.0):
        m.tube([T((s, 0.0, 0.8)), T((s * 1.15, 0.0, 4.4))], 1.35, "Frame", sides=8, caps=True)
    horn = [T((s, 0.0, 5.2 + 0.012 * s * s)) for s in (-12.5, -10.0, -6.0, 0.0, 6.0, 10.0, 12.5)]
    m.tube(horn, 1.25, "Frame", sides=8, caps=True)
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
    gasket, with flush pull latches. Each layer follows the deck where it rises into the bow."""
    def panel(xa, xb, ya, yb, lift, material):
        n = max(1, int(math.ceil((xb - xa) / 6.0)))
        xs = [xa + (xb - xa) * i / n for i in range(n + 1)]
        m.grid([[(x, ya, _deck_z_at(x) + lift), (x, yb, _deck_z_at(x) + lift)] for x in xs], material,
               outward_hint=lambda p: (p[0], p[1], p[2] - 50.0))
    g = 2.0      # the gasket: a black border 2 cm wide around the lid
    panel(x0 - g, x1 + g, -hy - g, hy + g, 0.1, "Trim")
    panel(x0, x1, -hy, hy, 0.2, "Deck")
    if handle:
        for y in (-hy * 0.5, hy * 0.5):
            xh = x0 + 7.0
            panel(xh - 3.0, xh + 3.0, y - 5.0, y + 5.0, 0.3, "Frame")


def _side_y(st, z):
    """Half-width of the topsides at height z (between their middle line and the sheer) in station st."""
    zm = 0.5 * (st["chine_z"] + st["sheer_z"])
    return st["sheer_b"] * (0.99 + 0.01 * (z - zm) / (st["sheer_z"] - zm))


def _circle(c, r, n=12, axis="z"):
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n
        if axis == "z":
            out.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a), c[2]))
        elif axis == "x":
            out.append((c[0], c[1] + r * math.cos(a), c[2] + r * math.sin(a)))
        else:
            out.append((c[0] + r * math.cos(a), c[1], c[2] + r * math.sin(a)))
    return out


def _gunwale_point(x, inset, lift=0.0, side=1.0):
    """A point on the gunwale cap at X, `inset` cm in from the sheer, on the given side."""
    st = station((x - STERN_X) / LENGTH)
    return (x, side * (st["sheer_b"] - inset), st["sheer_z"] + lift)


def _fittings(m):
    """Deck gear, tie-off points and hull fittings, each where it goes on a patrol boat of this kind."""
    # Floor storage lockers in front of the console, and the bilge access hatch in the aft cockpit.
    _deck_hatch(m, 55.0, 150.0, 40.0)
    _deck_hatch(m, 170.0, 240.0, 28.0)
    _deck_hatch(m, -300.0, -245.0, 26.0)
    # Anchor locker lid on the bow platform, where the gunwale caps close in over the stem.
    tip = station(1.0)
    ax = ANCHOR_LID_X
    az = station((ax - STERN_X) / LENGTH)["sheer_z"]
    m.box((ax - 6.0, -5.0, az - 0.2), (ax + 6.0, 5.0, az + 0.6), "Trim")

    # Cleats: bow pair, midship (spring) pair on the gunwale caps, lying along the gunwale as it curves and rises,
    # and the stern pair on the gunwale beside the stern box. All on the middle of the cap, inboard of the collar.
    for t in (0.87, 0.49):
        for side in (1.0, -1.0):
            _cleat(m, _gunwale_frame(t, side, GUNWALE_MID))
    s0 = station(0.0)
    for side in (1.0, -1.0):
        _cleat(m, _frame((STERN_X + 33.0, side * (s0["sheer_b"] - GUNWALE_MID), s0["sheer_z"]), (1.0, 0.0, 0.0)))
    # Towing bitts: a short post with a cross-pin on each stern box top, behind its hatch, to tow from or be towed.
    for side in (1.0, -1.0):
        bx, by, bz = STERN_X + 6.0, side * 98.0, s0["sheer_z"]
        m.box((bx - 4.5, by - 4.5, bz - 0.2), (bx + 4.5, by + 4.5, bz + 1.2), "Frame")
        m.tube([(bx, by, bz + 1.0), (bx, by, bz + 18.0)], 3.0, "Frame", sides=12, caps=True)
        m.tube([(bx, by - 7.0, bz + 13.0), (bx, by + 7.0, bz + 13.0)], 1.2, "Frame", sides=8, caps=True)
    # Tow post (samson post) on the foredeck, just aft of the anchor locker.
    tx = 345.0
    tz = _deck_z_at(tx)
    m.box((tx - 6.0, -6.0, tz - 0.6), (tx + 6.0, 6.0, tz + 1.5), "Frame")
    m.tube([(tx, 0.0, tz + 1.5), (tx, 0.0, tz + 32.0)], 3.0, "Frame", sides=12, caps=True)
    m.tube([(tx, -9.0, tz + 24.0), (tx, 9.0, tz + 24.0)], 1.5, "Frame", sides=8, caps=True)
    # Bow eye standing out of the stem above the waterline, and towing / lifting eyes on the transom's upper
    # corners, outboard of the motor notch and clear of the ladder; each eye stands square out of its face.
    _ring(m, (tip["x"] + 1.5, 0.0, 22.0), 3.5, 1.0, "xz", "Frame")
    for side in (1.0, -1.0):
        _ring(m, (STERN_X - 2.5, side * 100.0, 55.0), 3.5, 1.0, "xz", "Frame")

    # Combination bow light on the foredeck at the stem: red to port, green to starboard, on a low base.
    lx = BOW_LIGHT[0]
    lz = BOW_LIGHT[2] - 2.0
    base_z = station((lx - 4.0 - STERN_X) / LENGTH)["sheer_z"] - 0.5
    m.box((lx - 3.0, -3.0, lz), (lx + 3.0, 0.0, lz + 4.0), "NavRed")
    m.box((lx - 3.0, 0.0, lz), (lx + 3.0, 3.0, lz + 4.0), "NavGreen")
    m.box((lx - 4.0, -3.5, base_z), (lx + 4.0, 3.5, lz), "Trim")

    # Weapon mount sockets: one on the foredeck behind the tow post, one at each aft corner of the cockpit on the
    # gunwale cap. Each is a stainless tube with a dark bore.
    for x, y, z in ((315.0, 0.0, _deck_z_at(315.0)), _gunwale_point(-315.0, GUNWALE_MID),
                    _gunwale_point(-315.0, GUNWALE_MID, side=-1.0)):
        hw = 6.0 if y == 0.0 else 4.5
        m.box((x - 6.0, y - hw, z - 0.6), (x + 6.0, y + hw, z + 1.2), "Frame")
        m.tube([(x, y, z + 1.2), (x, y, z + 14.0)], 3.2, "Frame", sides=12)
        top = z + 14.0
        m.grid([_circle((x, y, top), 3.2), _circle((x, y, top), 2.2)], "Frame",
               outward_hint=lambda p, top=top: (p[0], p[1], top - 10.0), close_rows=True)
        m.grid([_circle((x, y, top), 2.2), _circle((x, y, top - 2.0), 2.2)], "Trim", close_rows=True,
               outward_hint=lambda p, x=x, y=y: (2 * p[0] - x, 2 * p[1] - y, p[2]))      # the bore's wall, facing in
        m.fan(_circle((x, y, top - 2.0), 2.2), "Trim", outward_hint=lambda p, top=top: (p[0], p[1], top - 10.0))

    # The life ring in its holder on the console's front face, where it's to hand from the foredeck and the helm.
    ring_c = (40.5, 0.0, DECK_Z + 36.0)
    m.tube([(ring_c[0], 30.0 * math.cos(a), ring_c[2] + 30.0 * math.sin(a)) for a in [2 * math.pi * i / 24 for i in range(24)]],
           5.0, "Safety", sides=10, closed=True)
    for dy in (-18.0, 18.0):
        m.tube([(35.0, dy, ring_c[2] + 26.0), (41.0, dy, ring_c[2] + 26.0), (41.0, dy, ring_c[2] + 32.0)], 0.9, "Frame",
               sides=6, caps=True)

    # Console: grab rails down both sides, the fire extinguisher on its bracket to port, and the compass up top.
    for side in (1.0, -1.0):
        y = side * 49.0
        m.tube([(-38.0, side * 44.5, DECK_Z + 70.0), (-38.0, y, DECK_Z + 70.0), (22.0, y, DECK_Z + 70.0),
                (22.0, side * 44.5, DECK_Z + 70.0)], 1.3, "Frame", sides=8)
    m.tube([(-10.0, -51.0, DECK_Z + 14.0), (-10.0, -51.0, DECK_Z + 50.0)], 5.5, "Red", sides=12)
    m.fan(_circle((-10.0, -51.0, DECK_Z + 14.0), 5.5), "Red", outward_hint=lambda p: (p[0], p[1], p[2] + 10.0))
    _dome(m, (-10.0, -51.0, DECK_Z + 50.0), 5.5, 4.0, "Red")
    m.tube([(-10.0, -51.0, DECK_Z + 54.0), (-13.0, -51.0, DECK_Z + 58.0)], 1.0, "Trim", sides=6, caps=True)
    m.box((-8.0, -46.0, DECK_Z + 20.0), (-5.0, -45.0, DECK_Z + 46.0), "Frame")
    _dome(m, (-8.0, 0.0, DECK_Z + 112.0), 6.0, 5.0, "Glass")
    m.tube([(-8.0, 0.0, DECK_Z + 112.0), (-8.0, 0.0, DECK_Z + 113.5)], 6.5, "Trim", sides=12)
    m.grid([_circle((-8.0, 0.0, DECK_Z + 113.5), 6.5), _circle((-8.0, 0.0, DECK_Z + 113.5), 5.5)], "Trim",
           outward_hint=lambda p: (p[0], p[1], p[2] - 10.0), close_rows=True)

    # Leaning post: backrest bolster and a grab rail across its back, its ends into the bolster.
    m.box((-177.0, -38.0, DECK_Z + 92.0), (-171.0, 38.0, DECK_Z + 120.0), "Cushion")
    m.tube([(-175.5, -36.0, DECK_Z + 100.0), (-182.0, -36.0, DECK_Z + 124.0), (-182.0, 36.0, DECK_Z + 124.0),
            (-175.5, 36.0, DECK_Z + 100.0)], 1.3, "Frame", sides=8)

    # Cockpit drains (scuppers) at the foot of the stern bulkhead, and the fuel filler on the starboard gunwale
    # with its tank's vents on the hull sides below the collar, just forward of it.
    for side in (1.0, -1.0):
        m.box((BULKHEAD_X - 0.5, side * 60.0 - 8.0, DECK_Z), (BULKHEAD_X + 0.3, side * 60.0 + 8.0, DECK_Z + 6.0), "Trim")
    fx, fy, fz = FUEL_FILLER
    m.tube([(fx, fy, fz - 0.5), (fx, fy, fz + 0.8)], 2.4, "Frame", sides=12, caps=True)
    m.box((fx - 0.4, fy - 1.6, fz + 0.8), (fx + 0.4, fy + 1.6, fz + 1.3), "Frame")
    for side in (1.0, -1.0):
        vx = fx + 25.0
        st = station((vx - STERN_X) / LENGTH)
        vz = st["sheer_z"] - 27.0
        vy = _side_y(st, vz)
        # A clamshell vent: a half-round hood opening aft, on a flat plate set into the side.
        m.box((vx - 4.0, side * (vy - 0.3), vz - 2.5), (vx + 4.0, side * (vy + 0.4), vz + 2.5), "Frame")
        hood = [[(vx + 3.5 - 7.0 * f, side * (vy + 0.4 + 1.8 * math.sin(a) * (1.0 - 0.6 * f)), vz + 2.0 * math.cos(a))
                 for a in [math.pi * k / 6 for k in range(7)]] for f in (0.0, 0.5, 1.0)]
        m.grid(hood, "Frame", outward_hint=lambda p, side=side, vy=vy, vz=vz: (p[0], side * vy, vz))
        m.fan(hood[0], "Frame", outward_hint=lambda p, vx=vx: (vx - 10.0, p[1], p[2]))
        m.fan(hood[-1], "Trim", outward_hint=lambda p, vx=vx: (vx + 10.0, p[1], p[2]))

    # A grab rail along each gunwale from the stern quarters to the cockpit's forward end, on stanchions: the
    # handhold for crew along the side decks (the bow rail carries on forward of it).
    for side in (1.0, -1.0):
        x0, x1 = GRAB_RAIL_X
        n = 20
        xs = [x0 + (x1 - x0) * i / n for i in range(n + 1)]
        a = _gunwale_point(x0 - 4.0, GUNWALE_MID, 0.0, side)
        b = _gunwale_point(x1 + 4.0, GUNWALE_MID, 0.0, side)
        rail = [_gunwale_point(x, GUNWALE_MID, GRAB_RAIL_H, side) for x in xs]
        m.tube([(a[0], a[1], a[2] - 0.5), (a[0] + 1.0, a[1], a[2] + GRAB_RAIL_H - 5.0)] + rail +
               [(b[0] - 1.0, b[1], b[2] + GRAB_RAIL_H - 5.0), (b[0], b[1], b[2] - 0.5)], 1.6, "Frame", sides=8)
        for x in [x0 - 4.0] + list(GRAB_RAIL_STANCHIONS) + [x1 + 4.0]:
            p = _gunwale_point(x, GUNWALE_MID, 0.0, side)
            if x in GRAB_RAIL_STANCHIONS:
                m.tube([(p[0], p[1], p[2] - 0.5), (p[0], p[1], p[2] + GRAB_RAIL_H)], 1.4, "Frame", sides=8)
            m.tube([(p[0], p[1], p[2] - 0.2), (p[0], p[1], p[2] + 1.0)], 2.8, "Frame", sides=12, caps=True)

    # Transom: hydraulic trim tabs hinged along the bottom edge between the motors and the corners, following the
    # V of the bottom and set down a few degrees, each worked by a ram from a mount higher up the transom. Clear of
    # the motors' lower units at full lock and trim, and of the ladder.
    for side in (1.0, -1.0):
        _trim_tab(m, s0, side, 60.0, 88.0)
    # The boarding ladder reaches well into the water, so a swimmer can climb out (ARiptideBoat's ladder foot): two
    # stainless rails on standoffs off the transom's port corner, flat treads, and a grab handle over the top that
    # comes down onto the stern box, aft of its hatch.
    lx = STERN_X - 4.0
    for y in (-114.0, -94.0):
        m.tube([(lx, y, LADDER_BOTTOM_Z), (lx, y, 72.0), (STERN_X + 2.0, y, 80.0), (STERN_X + 6.0, y, 80.0),
                (STERN_X + 7.0, y, s0["sheer_z"] - 0.5)], 1.2, "Frame", sides=8, caps=True)
        for z in (8.0, 58.0):
            m.box((lx, y - 1.0, z - 2.0), (STERN_X, y + 1.0, z + 2.0), "Frame")
    for z in LADDER_RUNGS:
        m.box((lx - 2.2, -113.0, z - 0.7), (lx + 2.2, -95.0, z + 0.7), "Frame")
    # Bilge pump outlets on the hull sides, aft, set into the plating.
    for x in (-300.0, -200.0):
        st = station((x - STERN_X) / LENGTH)
        z = st["sheer_z"] - 28.0
        for side in (1.0, -1.0):
            _ring(m, (x, side * (_side_y(st, z) + 0.3), z), 2.0, 0.6, "xz", "Frame")

    # Running strakes along the bottom: they throw the spray out sideways and give the hull its grip.
    for f in (0.38, 0.68):
        for side in (1.0, -1.0):
            path = []
            for t in [0.08 + 0.8 * i / 24 for i in range(25)]:
                st = station(t)
                y = st["chine_b"] * f
                z = st["keel_z"] + (st["chine_z"] - st["keel_z"]) * (f ** 0.9) - 1.0
                path.append((st["x"], side * y, z))
            m.tube(path, 1.6, "Aluminium", sides=5, caps=True)


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
    for i, out in ((0, 0.0), (-1, side * 300.0)):          # the inboard end faces in, the outboard end out
        _quad(m, hinge[i], trail[i], (trail[i][0], trail[i][1], trail[i][2] - thick), (hinge[i][0], hinge[i][1], hinge[i][2] - thick),
              "Frame", (STERN_X - length / 2, out, hinge[i][2]))
    m.tube([(STERN_X - 0.8, p[1], p[2] - 0.3) for p in hinge], 0.9, "Frame", sides=6, caps=True)
    # The ram: its cylinder on a bracket up the transom, its rod down to a clevis on the tab's top.
    ym = side * 0.5 * (y0 + y1)
    zm = _bottom_z(s0, ym)
    upper = (STERN_X - 3.5, ym, zm + 30.0)
    lower = (STERN_X - length * 0.6, ym, zm - drop * 0.6 + 1.2)
    mid = tuple(upper[a] + (lower[a] - upper[a]) * 0.55 for a in range(3))
    m.box((STERN_X - 2.0, ym - 3.0, zm + 27.0), (STERN_X, ym + 3.0, zm + 33.0), "Trim")
    m.tube([upper, mid], 2.2, "Trim", sides=10, caps=True)
    m.tube([mid, lower], 0.8, "Frame", sides=6, caps=True)
    m.box((lower[0] - 2.0, ym - 1.5, lower[2] - 1.2), (lower[0] + 2.0, ym + 1.5, lower[2] + 0.6), "Frame")


def _quad(m, a, b, c, d, material, facing):
    """A flat four-cornered panel whose face points toward the point `facing`."""
    m._part([a, b, c, d], [(0, 1, 2), (0, 2, 3)], material,
            outward_hint=lambda p: (2 * p[0] - facing[0], 2 * p[1] - facing[1], 2 * p[2] - facing[2]))


def _stern_box(m, s0):
    """The bulkhead across the stern, its flat top with hatches, and the splashwell behind it in front of the motors."""
    top = s0["sheer_z"]
    inner = s0["sheer_b"] - GUNWALE_W
    x0, x1 = STERN_X + TRANSOM_THICK, BULKHEAD_X        # inside of the transom, front of the box
    cockpit = (0.0, 0.0, DECK_Z + 60.0)
    # Bulkhead facing the cockpit, deck to gunwale, the full width between the bulwarks.
    _quad(m, (x1, -inner, DECK_Z), (x1, inner, DECK_Z), (x1, inner, top), (x1, -inner, top), "HullInside", cockpit)
    # Box top either side of the well, flush with the gunwale caps, each with a hatch lid.
    for side in (1.0, -1.0):
        _quad(m, (x0 - TRANSOM_THICK, side * WELL_HALF, top), (x1, side * WELL_HALF, top), (x1, side * inner, top),
              (x0 - TRANSOM_THICK, side * inner, top), "Deck", (-360.0, side * 90.0, top + 100.0))
        hy0, hy1 = side * (WELL_HALF + 6.0), side * (inner - 4.0)       # outboard of it, the stern cleat
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
                    (x0 + 8.0, y, WELL_FLOOR_Z + 3.0), (x0 + 1.0, y, SILL_Z + 6.0), (STERN_X - 12.0, y, SILL_Z + 5.0)],
                   r, "Trim", sides=6, caps=True)  # over the bracket's hook and into the cowling's front


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
    m.tube([(bx, by - 8.0, bz), (bx, by + 8.0, bz)], 2.2, "Frame", sides=10, caps=True)

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
    corner_r, front_x, half = 22.0, 5.0, 45.0      # corners rounder than the front's rake, so the top edge stays fair
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
        # The front rakes well back, the wings stand nearly upright, blending smoothly round the corners.
        lean = wing_lean + (front_lean - wing_lean) * (1.0 - abs(ny))
        base.append((x, y, base_z))
        top_row.append((x + nx * lean, y + ny * lean, base_z + height))
    m.grid([base, top_row], "Glass", outward_hint=lambda p: (-60.0, 0.0, p[2]))
    # Frame: a grab rail along the top, posts at the wing ends and the front corners, and a trim strip along the base.
    m.tube(top_row, 1.8, "Frame", sides=8, caps=True)
    for i in (0, 2 + 5 - 1, len(base) - 1 - (2 + 5 - 1), len(base) - 1):
        m.tube([base[i], top_row[i]], 1.4, "Frame", sides=8, caps=True)
    m.tube(base, 1.2, "Trim", sides=6, caps=True)


WHEEL_CENTRE = (-56.0, 0.0, DECK_Z + 88.0)   # the wheel's hub (ARiptideBoat's WheelCentre)
WHEEL_TILT_DEG = 35.0                        # its face tilted back from upright, toward the helmsman


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
    panel = [on_slope(0.05, -40.0, 0.15), on_slope(0.05, 40.0, 0.15), on_slope(0.95, 40.0, 0.15), on_slope(0.95, -40.0, 0.15)]
    m._part(panel, [(0, 1, 2), (0, 2, 3)], "Trim", outward_hint=lambda p: (p[0] - 50.0 * nx, p[1], p[2] - 50.0 * nz))
    # The screen's bezel and the gauges' chrome rings; the faces are live displays (ARiptideBoat's gauges).
    sy0, sy1, sf0, sf1 = 17.0, 17.0, 0.12, 0.9
    bezel = [on_slope(sf0, -sy0), on_slope(sf0, sy1), on_slope(sf1, sy1), on_slope(sf1, -sy0)]
    m.tube([(p[0] + nx * 0.4, p[1], p[2] + nz * 0.4) for p in bezel], 0.7, "Trim", sides=6, closed=True)
    for gy in (-28.5, 28.5):
        c = on_slope(0.5, gy, 1.2)
        ring = [(c[0] + 7.2 * math.sin(a) * ux, c[1] + 7.2 * math.cos(a), c[2] + 7.2 * math.sin(a) * uz)
                for a in [2 * math.pi * i / 24 for i in range(24)]]
        m.tube(ring, 0.9, "Frame", sides=6, closed=True)

    # The helm: a tilt-helm bezel on the console's rear face and the shaft out of it to the wheel (build_wheel, which
    # turns on it: ARiptideBoat's WheelCentre).
    tilt = math.radians(WHEEL_TILT_DEG)
    axis = (-math.cos(tilt), 0.0, math.sin(tilt))      # the wheel faces back and up
    cx, cy, cz = WHEEL_CENTRE
    shaft_in = (cx - axis[0] * 15.0, 0.0, cz - axis[2] * 15.0)
    hub_back = (cx - axis[0] * 4.5, 0.0, cz - axis[2] * 4.5)
    m.tube([shaft_in, hub_back], 1.6, "Frame", sides=10)
    _cone(m, (cx - axis[0] * 15.5, 0.0, cz - axis[2] * 15.5), (cx - axis[0] * 12.0, 0.0, cz - axis[2] * 12.0), 6.5, 3.0, "Trim", sides=14)


def _t_top(m):
    deck = DECK_Z
    top = DECK_Z + 220.0
    # Legs close in against the console and leaning post, leaving the side decks clear to walk.
    legs = [(-150.0, 48.0), (-150.0, -48.0), (20.0, 48.0), (20.0, -48.0)]
    for x, y in legs:
        m.tube([(x, y, deck - 0.5), (x, y * 1.05, deck + 110.0), (x + (8.0 if x > 0 else -8.0), y * 1.25, top)], 3.5, "Frame")
    # Canopy frame and roof.
    ring = [(-158.0, 60.0, top), (28.0, 60.0, top), (28.0, -60.0, top), (-158.0, -60.0, top), (-158.0, 60.0, top)]
    m.tube(ring, 3.5, "Frame", closed=True)
    m.box((-165.0, -70.0, top + 2.0), (35.0, 70.0, top + 8.0), "Canopy")
    roof = top + 8.0
    # Searchlight (starboard) and FLIR thermal camera ball (port) at the front edge, each on a pan-tilt base. The
    # searchlight's head is its own model (build_searchlight), aimed from the helm.
    m.tube([(26.0, 40.0, roof), (26.0, 40.0, roof + 8.0)], 5.0, "Trim", sides=12, caps=True)
    m.tube([(26.0, -40.0, roof), (26.0, -40.0, roof + 8.0)], 5.0, "Trim", sides=12)    # up into the ball
    _ball(m, (26.0, -40.0, roof + 15.0), 9.0, "White")
    m.tube([(26.0 + 6.5, -40.0, roof + 15.0), (26.0 + 9.4, -40.0, roof + 15.0)], 3.6, "Trim", sides=12, caps=True)
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
        m.tube([(-159.0, y, roof + 6.0), (-175.0, y * 1.02, roof + 240.0)], 1.2, "White", sides=6, caps=True)
    m.tube([(-150.0, 20.0, roof), (-153.0, 20.0, roof + 95.0)], 0.9, "Trim", sides=6, caps=True)
    # All-round white masthead light on a pole, aft centre, clear above everything.
    m.tube([(-162.0, 0.0, roof), (-162.0, 0.0, roof + 85.0)], 2.0, "Frame", sides=8)
    m.tube([(-162.0, 0.0, roof + 85.0), (-162.0, 0.0, roof + 93.0)], 3.2, "NavWhite", sides=12)
    m.tube([(-162.0, 0.0, roof + 84.2), (-162.0, 0.0, roof + 85.2)], 3.6, "Trim", sides=12, caps=True)   # the lens base
    m.box((-165.6, -3.6, roof + 93.0), (-158.4, 3.6, roof + 95.0), "Trim")
    # Loudhailer horn under the canopy's front lip, and the ship's horn beside it.
    m.box((24.0, -8.0, top - 12.0), (32.0, 8.0, top + 2.0), "Trim")
    _cone(m, (32.0, 0.0, top - 5.0), (46.0, 0.0, top - 5.0), 4.0, 10.0, "White")
    m.fan([(33.0, 4.0 * math.cos(a), top - 5.0 + 4.0 * math.sin(a)) for a in [2 * math.pi * i / 12 for i in range(12)]],
          "Trim", outward_hint=lambda p: (20.0, 0.0, top - 5.0))            # the driver, deep in the throat
    m.tube([(46.0, 10.0 * math.cos(a), top - 5.0 + 10.0 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]],
           0.6, "White", sides=6, closed=True)                                              # a rolled lip on the bell
    _cone(m, (22.0, 20.0, top - 6.0), (34.0, 20.0, top - 6.0), 2.0, 5.0, "Frame")
    m.box((20.0, 18.0, top - 8.0), (24.0, 22.0, top - 4.0), "Trim")
    # Overhead electronics box above the helm with two VHF radios and a speaker in its face.
    _electronics_box(m, top)
    # (The life ring is on the console's front: see _fittings.)


# The VHF's hand mic hangs on a clip under the overhead box, right of the wheel; its coiled cord runs from a jack on
# the radio's face (ARiptideBoat's MicHook and MicCordJack; the mic itself is build_mic, the cord is drawn live).
MIC_HOOK = (-77.5, 29.0, DECK_Z + 220.0 - 21.0)
MIC_CORD_JACK = (-77.0, 20.5, DECK_Z + 220.0 - 17.5)


def _electronics_box(m, top):
    """The overhead box under the T-top, above the helm, facing the helmsman: the main VHF (with the hand mic), a
    second radio for the AIS, and a speaker, each set into the box's face."""
    x = -75.0
    m.box((x, -32.0, top - 22.0), (-15.0, 32.0, top + 2.0), "Console")
    # Main VHF: a black face with a display, knobs and keys, and the mic jack at its lower right.
    m.box((x - 1.2, 1.0, top - 19.0), (x, 24.0, top - 5.0), "Trim")
    m.box((x - 1.5, 6.0, top - 15.5), (x - 1.2, 18.0, top - 8.5), "Lamp")
    for yk, zk, r in ((3.5, top - 9.0, 1.4), (3.5, top - 15.0, 1.4), (21.0, top - 9.0, 1.1)):
        m.tube([(x - 1.2, yk, zk), (x - 3.2, yk, zk)], r, "Frame", sides=10, caps=True)
    for i in range(4):
        m.box((x - 1.6, 7.0 + 2.8 * i, top - 18.0), (x - 1.2, 9.0 + 2.8 * i, top - 16.6), "Console")
    m.tube([(x - 1.2, MIC_CORD_JACK[1], MIC_CORD_JACK[2]), (MIC_CORD_JACK[0], MIC_CORD_JACK[1], MIC_CORD_JACK[2])], 0.8, "Frame", sides=8, caps=True)
    # Second radio (the AIS), smaller.
    m.box((x - 1.2, -17.0, top - 17.0), (x, -2.0, top - 7.0), "Trim")
    m.box((x - 1.5, -14.5, top - 14.5), (x - 1.2, -5.0, top - 9.5), "Lamp")
    # Speaker grille.
    m.tube([(x, -25.0, top - 11.0), (x - 1.0, -25.0, top - 11.0)], 5.0, "Trim", sides=14, caps=True)
    for dz in (-3.0, -1.0, 1.0, 3.0):
        w = math.sqrt(max(0.0, 16.0 - dz * dz))
        m.box((x - 1.3, -25.0 - w, top - 11.0 + dz - 0.3), (x - 1.0, -25.0 + w, top - 11.0 + dz + 0.3), "Frame")
    # The mic's clip, under the box's front edge.
    hx, hy, hz = MIC_HOOK
    m.box((hx - 0.5, hy - 2.0, hz + 0.2), (x, hy + 2.0, top - 22.0), "Frame")
    m.box((hx - 1.4, hy - 1.6, hz - 1.0), (hx - 0.5, hy + 1.6, hz + 0.8), "Frame")


def _bow_rail(m):
    """The bow rail (pulpit): one continuous stainless tube from the port gunwale round the bow to the starboard
    gunwale, each end coming down to a foot on the cap, with stanchions between, all on the middle of the cap."""
    inset, h = GUNWALE_MID, 30.0
    lo, hi = 0.9, 1.0                    # where the rail's line, `inset` in from the sheer, meets the centreline
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        lo, hi = (mid, hi) if station(mid)["sheer_b"] > inset else (lo, mid)
    tip = station(lo)
    secs = [station(0.55 + (lo - 0.012 - 0.55) * i / 20) for i in range(21)]
    x0 = secs[0]["x"] - 6.0
    foot = _gunwale_point(x0, inset)
    stbd = [(foot[0], foot[1], foot[2] - 0.5), (x0 + 1.5, foot[1], foot[2] + h - 4.0)]
    stbd += [(s["x"], s["sheer_b"] - inset, s["sheer_z"] + h) for s in secs]
    port = [(x, -y, z) for x, y, z in stbd]
    m.tube(port + [(tip["x"], 0.0, tip["sheer_z"] + h)] + list(reversed(stbd)), 2.2, "Frame", sides=8)
    for side in (1.0, -1.0):
        for i in (None, 4, 9, 14, 18):
            p = _gunwale_point(x0, inset, 0.0, side) if i is None else _gunwale_point(secs[i]["x"], inset, 0.0, side)
            if i is not None:
                m.tube([(p[0], p[1], p[2] - 0.5), (p[0], p[1], p[2] + h)], 1.8, "Frame", sides=8)
            m.tube([(p[0], p[1], p[2] - 0.2), (p[0], p[1], p[2] + 1.0)], 3.0, "Frame", sides=12, caps=True)


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
    m.fan([(-6.0, 8.0 * math.cos(a), 8.0 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]], "Frame",
          outward_hint=lambda p: (6.0, 0.0, 0.0))
    m.fan([(12.0, 7.2 * math.cos(a), 7.2 * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]], "Lamp",
          outward_hint=lambda p: (0.0, 0.0, 0.0))
    m.grid([[(12.0, r * math.cos(a), r * math.sin(a)) for a in [2 * math.pi * i / 16 for i in range(16)]] for r in (8.0, 7.2)],
           "Trim", outward_hint=lambda p: (0.0, 0.0, 0.0), close_rows=True)     # the bezel round the lens
    for y in (-9.5, 9.5):
        m.box((-2.0, y - 1.0, -8.0), (4.0, y + 1.0, 1.0), "Trim")
    m.box((-2.0, -10.5, -9.0), (4.0, 10.5, -7.0), "Trim")
    m.tube([(-6.0, 0.0, 5.0), (-10.0, 0.0, 9.0)], 1.0, "Trim", sides=6, caps=True)   # handle
    return m


def build_radar_array():
    """The open-array radar antenna that spins on the T-top: origin at its hub, the array lying fore and aft."""
    m = Mesh()
    m.tube([(0.0, 0.0, -4.0), (0.0, 0.0, 2.0)], 7.0, "White", sides=14, caps=True)
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


# The outboard's lower unit, in its own frame (origin at the tilt tube, the transom's outer face at X = +5). The
# transom in the motor notch stands 80 cm above the hull's bottom under each motor, so the anti-ventilation plate is
# 78 cm under the tilt tube (83 cm under the sill): 2.7 cm below the bottom there, as it should be, and the prop's
# centre 22 cm under the plate, so its tips just clear it. (That's a long-shaft leg, between a 30 in XL and a 35 in
# XXL; the motor's mounting holes make up the difference.)
PLATE_Z = -78.0
# The motor's body (cowling, leg, gearcase, prop) sits this far aft of the tilt tube, behind its swivel bracket, so
# the cowling's front corners clear the transom when it's steered hard over and trimmed out.
BODY_AFT = 13.0
PROP_CENTRE = (-45.0 - BODY_AFT, 0.0, -100.0)      # the boat's PropInOutboard
PROP_RADIUS = 19.0                       # a 15 in x 19 in stainless three-blade


def build_outboard():
    """A big outboard (a 300 hp V8 on a long shaft). Origin = the tilt tube at the top of its clamp bracket (a
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
    m.tube(split, 0.8, "Trim", sides=6, closed=True)
    # Air intake grille across the top of the back, and the latch at the front.
    for i in range(4):
        z = 58.0 + 3.0 * i
        m.box((-62.0 + 0.4 * i, -12.0, z), (-60.0 + 0.4 * i, 12.0, z + 1.2), "Trim")
    m.box((6.8, -5.0, 20.0), (7.8, 5.0, 24.0), "Trim")

    # Steering: the motor rides on rubber mounts off the swivel bracket's steering tube (build_outboard_swivel, which
    # trims with the motor but doesn't steer), just in front of the leg and the lower cowl.
    mounts_start = len(m.verts)
    m.box((-10.0 - BODY_AFT, -4.0, -24.0), (-7.5, 4.0, -18.0), "Trim")
    m.box((-8.0 - BODY_AFT, -4.0, -8.0), (-7.5, 4.0, -2.0), "Trim")
    mounts_end = len(m.verts)

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

    # The whole body (everything but its mounts) sits BODY_AFT behind the tilt tube.
    m.verts = [v if mounts_start <= i < mounts_end else ((v[0][0] - BODY_AFT, v[0][1], v[0][2]), v[1], v[2])
               for i, v in enumerate(m.verts)]
    return m


def build_outboard_swivel():
    """The swivel bracket: hung on the tilt tube, it tilts with the motor's trim but doesn't steer (the motor turns
    in its steering tube). Same origin as build_outboard (ARiptideBoat's swivel components, posed by trim only)."""
    m = Mesh()
    for y in (-11.0, 11.0):
        m.box((-6.0, y - 2.0, -4.0), (1.0, y + 2.0, 4.0), "Trim")
    m.box((-6.0, -10.0, -3.0), (-2.0, 10.0, 3.0), "Trim")
    m.tube([(-4.5, 0.0, -24.0), (-4.5, 0.0, 3.0)], 3.5, "Trim", sides=10, caps=True)
    return m


def build_propeller():
    """The outboard's prop, spinning on its shaft: origin at its centre (the outboard's PROP_CENTRE), shaft along X.
    A 15 x 19 in stainless three-blade: hub with the exhaust through it, blades pitched, skewed back and raked aft."""
    m = Mesh()
    cx, cz = 0.0, 0.0
    m.tube([(7.0, 0.0, 0.0), (0.0, 0.0, 0.0), (-6.0, 0.0, 0.0)], 5.3, "Prop", sides=14)
    _cone(m, (-6.0, 0.0, 0.0), (-9.0, 0.0, 0.0), 5.3, 4.2, "Prop", sides=14)
    m.fan([(-9.0, 4.2 * math.cos(a), 4.2 * math.sin(a)) for a in [2 * math.pi * j / 14 for j in range(14)]], "Trim",
          outward_hint=lambda p: (5.0, 0.0, 0.0))
    m.tube([(-9.1, 2.6 * math.cos(a), 2.6 * math.sin(a)) for a in [2 * math.pi * j / 14 for j in range(15)]], 0.5, "Prop", sides=6)
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
        # A thin blade seen from both sides: the same triangles twice, facing each way.
        pts = [q for row in rows for q in row]
        w = len(rows[0])
        tris = []
        for r in range(len(rows) - 1):
            for c in range(w - 1):
                a, b, cc, d = r * w + c, r * w + c + 1, (r + 1) * w + c + 1, (r + 1) * w + c
                tris += [(a, b, cc), (a, cc, d)]
        m._part(pts, tris, "Prop")
        m._part(pts, [(i, k, j) for i, j, k in tris], "Prop")
    return m


def build_wheel():
    """The steering wheel: origin at its hub, its axis along +X (toward the helmsman), the rim in the Y/Z plane with
    the spinner knob at the top (+Z) when the helm is centred. ARiptideBoat tilts it onto the helm's shaft and turns
    it about X with the steering. A 15.5 in (39 cm) destroyer-style stainless wheel with a black foam grip: five
    dished spokes, a hub cap, and a spinner knob for fast turns."""
    m = Mesh()
    r_rim = 19.0
    dish = 4.5                                   # the hub sits back from the rim's plane, toward the console

    def ring_path(r, x, n=48):
        return [(x, r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n + 1)]
    m.tube(ring_path(r_rim, 0.0), 0.7, "Frame", sides=8, closed=True)        # stainless core, showing at the spokes
    m.tube(ring_path(r_rim, 0.0), 1.55, "Trim", sides=12, closed=True)       # black grip over it
    # Spokes: flat stainless bars, dished back to the hub.
    for k in range(5):
        a = math.pi / 2 + 2 * math.pi * k / 5
        ca, sa = math.cos(a), math.sin(a)
        path = [(-dish + 0.5, 3.8 * ca, 3.8 * sa), (-dish * 0.45, 11.0 * ca, 11.0 * sa), (0.0, (r_rim - 1.0) * ca, (r_rim - 1.0) * sa)]
        for i in range(len(path) - 1):
            p0, p1 = path[i], path[i + 1]
            d = norm(sub(p1, p0))
            T = _frame(p0, d, (1.0, 0.0, 0.0))
            L = math.sqrt(sum((p1[j] - p0[j]) ** 2 for j in range(3)))
            _obox(m, T, (0.0, -1.1, -0.35), (L, 1.1, 0.35), "Frame")
    # Hub and cap.
    m.tube([(-dish - 2.0, 0.0, 0.0), (-dish + 1.0, 0.0, 0.0)], 4.2, "Frame", sides=16)
    m.fan([(-dish - 2.0, 4.2 * math.cos(b), 4.2 * math.sin(b)) for b in [2 * math.pi * j / 16 for j in range(16)]], "Frame",
          outward_hint=lambda p: (0.0, 0.0, 0.0))                                 # the hub's back, where the shaft goes in
    cap = []
    for i in range(5):
        a = (math.pi / 2) * i / 4
        cap.append([(-dish + 1.0 + 1.8 * math.sin(a), 4.2 * math.cos(a) * math.cos(b), 4.2 * math.cos(a) * math.sin(b))
                    for b in [2 * math.pi * j / 16 for j in range(16)]])
    m.grid(cap, "Trim", outward_hint=lambda p: (-dish - 2.0, 0.0, 0.0), close_rows=True)
    # Spinner knob on the rim at the top, standing out toward the helmsman.
    kz = r_rim
    m.tube([(0.0, 0.0, kz), (5.0, 0.0, kz)], 0.7, "Frame", sides=8)
    m.tube([(5.0, 0.0, kz), (9.5, 0.0, kz)], 1.5, "Trim", sides=12, caps=True)
    _ball(m, (9.5, 0.0, kz), 1.5, "Trim", rows=4, cols=12)
    return m


def build_mic():
    """The VHF's hand mic: origin at the tab on its top, where it hangs on its clip; the grille faces -X (toward the
    helmsman while it hangs), the push-to-talk bar is on its left side, and the cord leaves its bottom at
    MIC_CORD_EXIT."""
    m = Mesh()
    body = [_superellipse_ring(z, 0.0, sx, sy, n=3.5, count=20) for z, sx, sy in (
        (-1.5, 1.1, 2.0), (-2.5, 1.6, 2.9), (-6.0, 1.7, 3.1), (-9.5, 1.6, 2.7), (-11.0, 1.0, 1.7))]
    _loft(m, body, "Trim", 0.0, cap_bottom=True, cap_top=True)
    m.box((-1.9, -2.0, -7.2), (-1.5, 2.0, -3.0), "Console")                   # grille
    for i in range(5):
        m.box((-2.0, -1.7, -6.8 + 0.8 * i), (-1.85, 1.7, -6.5 + 0.8 * i), "Frame")
    m.box((-0.8, -3.5, -8.5), (0.8, -3.0, -3.5), "Console")                   # push-to-talk bar
    m.box((-0.4, -0.8, -1.6), (0.4, 0.8, 0.0), "Trim")                         # hanging tab
    m.tube([(0.0, 0.0, -11.0), (0.0, 0.0, -12.6)], 0.6, "Trim", sides=8, caps=True)     # strain relief
    return m


MIC_CORD_EXIT = (0.0, 0.0, -12.6)


def build_outboard_bracket():
    """The clamp bracket that stays on the transom while the motor trims: a plate down the transom's outer face, a
    hook over the sill, and the tilt tube. Same origin as build_outboard (the tilt tube)."""
    m = Mesh()
    w = 10.0                                                                     # half its width
    m.box((-1.0, -w, -28.0), (5.0, w, 6.0), "Trim")          # against the transom (its outer face is at X = +5)
    m.box((5.0, -w, 5.0), (5.0 + TRANSOM_THICK + 2.0, w, 8.0), "Trim")   # hooked over the sill
    m.box((5.0 + TRANSOM_THICK, -w, WELL_FLOOR_Z - OUTBOARD_PIVOT_Z), (5.0 + TRANSOM_THICK + 2.0, w, 8.0), "Trim")
    m.tube([(0.0, -13.0, 0.0), (0.0, 13.0, 0.0)], 2.6, "Cowling", sides=10, caps=True)    # tilt tube
    for y in (-6.0, 6.0):                                                         # through-bolt heads
        for z in (-13.0, -26.0):
            m.tube([(-0.5, y, z), (-2.0, y, z)], 1.5, "Prop", sides=8, caps=True)
    return m


def build_throttle_lever():
    """One lever of the twin throttle: origin at its pivot, standing up, with a black grip across the top."""
    m = Mesh()
    m.tube([(0.0, 0.0, -1.0), (0.0, 0.0, 17.0)], 1.1, "Frame", sides=8, caps=True)
    m.tube([(0.0, -3.5, 18.0), (0.0, 3.5, 18.0)], 1.9, "Trim", sides=10, caps=True)
    return m


if __name__ == "__main__":
    import os
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, mesh in (("skiff", build_skiff()), ("outboard", build_outboard()), ("bracket", build_outboard_bracket()),
                       ("swivel", build_outboard_swivel()),
                       ("lever", build_throttle_lever()), ("radar", build_radar_array()), ("searchlight", build_searchlight()),
                       ("propeller", build_propeller()), ("wheel", build_wheel()), ("mic", build_mic())):
        mesh.write_obj(os.path.join(out, name + ".obj"))
        print(name, len(mesh.verts), "verts", sum(len(f) for f in mesh.faces.values()), "tris")
