"""The game's items as models, built in code (plain Python here; the Unreal half is at the bottom).

Every item the crew can hold is a small mesh made by a builder in MODELS, keyed by the item's id from the C++ item
table (Source/Riptide/Data/RiptideItemTable.cpp). Items that have no builder yet are drawn as a plain crate and
listed in the log, so nothing is ever invisible. Models are in metres with the item resting on the ground at z = 0,
centred on the origin; write_obj turns them into Unreal's centimetres.

Materials are named by what the thing is made of (Wood, Driftwood, Bark, Stone, Flint, Fiber, Rope, Husk, Steel,
Plastic, Canvas), one plain material each, made in the editor by make_item_assets.
"""
import math
import os
import random

from riptide_palm_mesh import Mesh, _add, _cross, _mul, _norm, _sub

ITEM_MODELS_VERSION = "3"


# --- Shapes ------------------------------------------------------------------------------------------------------

def _hash(a, b, seed):
    n = (int(a) * 374761393 + int(b) * 668265263 + seed * 1274126177) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1103515245) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def lathe(mesh, material, profile, sides=16, squash=(1.0, 1.0), bumps=0.0, seed=1, caps=True):
    """A solid turned about the z axis from a profile of (radius, z) pairs, bottom to top; squash scales it in x
    and y, bumps roughens the surface by that fraction of the radius."""
    rings = []
    for i, (r, z) in enumerate(profile):
        ring = []
        for k in range(sides + 1):
            a = 2.0 * math.pi * (k % sides) / sides
            rough = 1.0 + bumps * (_hash(i, k % sides, seed) - 0.5) * 2.0
            x, y = math.cos(a) * r * rough * squash[0], math.sin(a) * r * rough * squash[1]
            # The normal follows the profile's slope.
            below = profile[max(i - 1, 0)]
            above = profile[min(i + 1, len(profile) - 1)]
            dr, dz = above[0] - below[0], above[1] - below[1]
            nz = -dr
            nr = dz if abs(dz) > 1e-9 else 1.0
            n = _norm((math.cos(a) * nr / max(squash[0], 1e-6), math.sin(a) * nr / max(squash[1], 1e-6), nz))
            ring.append(mesh.vert((x, y, z), n, (k / sides, i / max(len(profile) - 1, 1))))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(sides):
            mesh.quad(material, rings[i][k], rings[i][k + 1], rings[i + 1][k + 1], rings[i + 1][k])
    if caps:
        for ring, z, up in ((rings[0], profile[0][1], -1.0), (rings[-1], profile[-1][1], 1.0)):
            centre = mesh.vert((0.0, 0.0, z), (0.0, 0.0, up), (0.5, 0.5))
            for k in range(sides):
                mesh.tri(material, ring[k], ring[k + 1], centre)


def box(mesh, material, size, at=(0.0, 0.0, 0.0), yaw=0.0):
    """A box of (x, y, z) size, its base centred at `at`, turned about z by yaw radians."""
    sx, sy, sz = size[0] / 2.0, size[1] / 2.0, size[2]
    c, s = math.cos(yaw), math.sin(yaw)

    def p(x, y, z):
        return (at[0] + x * c - y * s, at[1] + x * s + y * c, at[2] + z)

    def n(x, y, z):
        return (x * c - y * s, x * s + y * c, z)

    faces = [((1, 0, 0), [(sx, -sy, 0), (sx, sy, 0), (sx, sy, sz), (sx, -sy, sz)]),
             ((-1, 0, 0), [(-sx, sy, 0), (-sx, -sy, 0), (-sx, -sy, sz), (-sx, sy, sz)]),
             ((0, 1, 0), [(sx, sy, 0), (-sx, sy, 0), (-sx, sy, sz), (sx, sy, sz)]),
             ((0, -1, 0), [(-sx, -sy, 0), (sx, -sy, 0), (sx, -sy, sz), (-sx, -sy, sz)]),
             ((0, 0, 1), [(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)]),
             ((0, 0, -1), [(-sx, sy, 0), (sx, sy, 0), (sx, -sy, 0), (-sx, -sy, 0)])]
    for normal, corners in faces:
        ids = [mesh.vert(p(*corner), n(*normal), (u, v)) for corner, (u, v) in zip(corners, ((0, 0), (1, 0), (1, 1), (0, 1)))]
        mesh.quad(material, *ids)


def stick(mesh, material, start, end, radius_start, radius_end, sides=8, wobble=0.0, seed=1, steps=8, cap=True):
    """A tube from start to end, with a little bend and roughness when wobble > 0."""
    rng = random.Random(seed)
    direction = _sub(end, start)
    length = math.sqrt(sum(d * d for d in direction))
    axis = _norm(direction)
    side = _norm(_cross(axis, (0.0, 0.0, 1.0) if abs(axis[2]) < 0.9 else (1.0, 0.0, 0.0)))
    other = _cross(axis, side)
    bend = (rng.uniform(-1, 1) * wobble, rng.uniform(-1, 1) * wobble)
    points, radii = [], []
    for i in range(steps + 1):
        t = i / steps
        arc = math.sin(t * math.pi) * length
        p = _add(start, _mul(direction, t))
        p = _add(p, _add(_mul(side, bend[0] * arc), _mul(other, bend[1] * arc)))
        points.append(p)
        radii.append((radius_start + (radius_end - radius_start) * t) * (1.0 + rng.uniform(-0.5, 0.5) * wobble * 3.0))
    mesh.tube(material, points, radii, sides, v_per_metre=4.0, cap_end=cap)
    # A flat start cap.
    first = points[0]
    back = _mul(axis, -1.0)
    centre = mesh.vert(first, back, (0.5, 0.0))
    ring = []
    for k in range(sides + 1):
        a = 2.0 * math.pi * k / sides
        out = _add(_mul(side, math.cos(a)), _mul(other, math.sin(a)))
        ring.append(mesh.vert(_add(first, _mul(out, radii[0])), back, (0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a))))
    for k in range(sides):
        mesh.tri(material, ring[k + 1], ring[k], centre)
    return points


def ring(mesh, material, centre, radius, thickness, axis=(0.0, 0.0, 1.0), sides=10, segments=16):
    """A torus: rope lashed round something."""
    axis = _norm(axis)
    side = _norm(_cross(axis, (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)))
    other = _cross(axis, side)
    points, radii = [], []
    for i in range(segments + 1):
        a = 2.0 * math.pi * i / segments
        points.append(_add(centre, _add(_mul(side, math.cos(a) * radius), _mul(other, math.sin(a) * radius))))
        radii.append(thickness)
    mesh.tube(material, points, radii, sides, v_per_metre=12.0, cap_end=False)


# --- The items ---------------------------------------------------------------------------------------------------

def build_stone():
    m = Mesh()
    profile = [(0.0, 0.0)] + [(0.062 * math.sin(math.pi * t), 0.045 * (1.0 - math.cos(math.pi * t))) for t in (0.2, 0.4, 0.6, 0.8)] + [(0.0, 0.045 * 2.0)]
    lathe(m, "Stone", profile, sides=14, squash=(1.0, 0.8), bumps=0.14, seed=3)
    return m


def build_flint():
    m = Mesh()
    profile = [(0.0, 0.0), (0.04, 0.004), (0.05, 0.012), (0.044, 0.02), (0.0, 0.026)]
    lathe(m, "Flint", profile, sides=7, squash=(1.0, 0.6), bumps=0.12, seed=5)
    return m


def build_coconut():
    m = Mesh()
    profile = [(0.0, 0.0)] + [(0.07 * math.sin(math.pi * t), 0.085 * (1.0 - math.cos(math.pi * t))) for t in (0.15, 0.3, 0.45, 0.6, 0.75, 0.9)] + [(0.0, 0.17)]
    lathe(m, "Husk", profile, sides=16, squash=(1.0, 0.95), bumps=0.05, seed=7)
    return m


def build_driftwood():
    m = Mesh()
    stick(m, "Driftwood", (-0.34, 0.0, 0.035), (0.34, 0.03, 0.03), 0.038, 0.022, sides=9, wobble=0.06, seed=11, steps=10)
    stick(m, "Driftwood", (0.05, 0.0, 0.04), (0.2, -0.12, 0.09), 0.016, 0.006, sides=6, wobble=0.04, seed=12, steps=5)
    return m


def build_log():
    m = Mesh()
    stick(m, "Bark", (-0.5, 0.0, 0.09), (0.5, 0.0, 0.09), 0.09, 0.08, sides=12, wobble=0.015, seed=13, steps=8, cap=False)
    # Cut ends show the wood.
    for x, out in ((-0.5, -1.0), (0.5, 1.0)):
        centre = m.vert((x, 0.0, 0.09), (out, 0.0, 0.0), (0.5, 0.5))
        r = 0.09 if x < 0 else 0.08
        ids = [m.vert((x, math.cos(a) * r, 0.09 + math.sin(a) * r), (out, 0.0, 0.0), (0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a)))
               for a in (2.0 * math.pi * k / 12 for k in range(13))]
        for k in range(12):
            m.tri("Wood", ids[k], ids[k + 1], centre)
    return m


def build_fiber():
    m = Mesh()
    rng = random.Random(17)
    for i in range(28):
        a = rng.uniform(0, 2 * math.pi)
        spread = rng.uniform(0.0, 0.03)
        start = (math.cos(a) * spread, math.sin(a) * spread, 0.01)
        droop = rng.uniform(0.12, 0.2)
        end = (math.cos(a) * droop, math.sin(a) * droop, 0.004)
        mid = ((start[0] + end[0]) * 0.5, (start[1] + end[1]) * 0.5, 0.05 + rng.uniform(0.0, 0.03))
        m.tube("Fiber", [start, mid, end], [0.0025, 0.002, 0.0008], 4, v_per_metre=10.0)
    ring(m, "Fiber", (0.0, 0.0, 0.012), 0.018, 0.006, sides=6, segments=10)
    return m


def build_rope():
    m = Mesh()
    points, radii = [], []
    turns, coil, r = 3.0, 0.11, 0.013
    steps = int(turns * 20)
    for i in range(steps + 1):
        t = i / steps
        a = 2.0 * math.pi * turns * t
        points.append((math.cos(a) * coil, math.sin(a) * coil, r + t * r * 2.0 * turns * 0.9))
        radii.append(r)
    m.tube("Rope", points, radii, 8, v_per_metre=20.0, cap_end=True)
    return m


def build_stone_hatchet():
    m = Mesh()
    stick(m, "Wood", (0.0, 0.0, 0.0), (0.0, 0.0, 0.46), 0.017, 0.015, sides=9, wobble=0.01, seed=19, steps=6)
    # The head: a flint wedge lashed to the top of the handle, its edge out along +x.
    head = Mesh()
    lathe(head, "Flint", [(0.0, 0.0), (0.045, 0.006), (0.05, 0.016), (0.045, 0.026), (0.0, 0.032)], sides=7, squash=(1.5, 0.45), bumps=0.08, seed=23)
    for p, n, uv in head.verts:
        m.verts.append(((p[0] + 0.035, p[1], p[2] + 0.41), n, uv))
    offset = len(m.verts) - len(head.verts)
    for material, faces in head.faces.items():
        for a, b, c in faces:
            m.tri(material, a + offset, b + offset, c + offset)
    for z in (0.395, 0.405, 0.415, 0.425):
        ring(m, "Rope", (0.012, 0.0, z), 0.028, 0.004, axis=(0.0, 0.0, 1.0), sides=5, segments=12)
    return m


def build_oar():
    m = Mesh()
    stick(m, "Wood", (0.0, 0.0, 0.02), (1.55, 0.0, 0.02), 0.019, 0.016, sides=10, wobble=0.0, seed=29, steps=4)
    # The blade: flat and a little curved, lying on the ground.
    for i in range(6):
        t0, t1 = i / 6.0, (i + 1) / 6.0
        w0, w1 = 0.04 + 0.1 * math.sin(t0 * math.pi * 0.85), 0.04 + 0.1 * math.sin(t1 * math.pi * 0.85)
        x0, x1 = 1.5 + t0 * 0.45, 1.5 + t1 * 0.45
        for side_sign, material in ((1.0, "Wood"), (-1.0, "Wood")):
            z = 0.02 + side_sign * 0.009
            ids = [m.vert((x0, -w0, z), (0.0, 0.0, side_sign), (0, t0)), m.vert((x0, w0, z), (0.0, 0.0, side_sign), (1, t0)),
                   m.vert((x1, w1, z), (0.0, 0.0, side_sign), (1, t1)), m.vert((x1, -w1, z), (0.0, 0.0, side_sign), (0, t1))]
            m.quad(material, *ids)
        for sign in (1.0, -1.0):
            ids = [m.vert((x0, sign * w0, 0.011), (0.0, sign, 0.0), (0, 0)), m.vert((x1, sign * w1, 0.011), (0.0, sign, 0.0), (1, 0)),
                   m.vert((x1, sign * w1, 0.029), (0.0, sign, 0.0), (1, 1)), m.vert((x0, sign * w0, 0.029), (0.0, sign, 0.0), (0, 1))]
            m.quad("Wood", *ids)
    box(m, "Wood", (0.02, 0.09, 0.018), at=(1.955, 0.0, 0.011))
    ring(m, "Wood", (-0.01, 0.0, 0.02), 0.024, 0.012, axis=(1.0, 0.0, 0.0), sides=8, segments=12)
    return m


def build_knife():
    m = Mesh()
    box(m, "Wood", (0.11, 0.026, 0.018), at=(-0.055, 0.0, 0.0))
    # The blade tapers to its point and its edge.
    for i in range(5):
        t0, t1 = i / 5.0, (i + 1) / 5.0
        x0, x1 = t0 * 0.14, t1 * 0.14
        h0, h1 = 0.024 * (1.0 - t0 * 0.9), 0.024 * (1.0 - t1 * 0.9)
        for sign in (1.0, -1.0):
            ids = [m.vert((x0, sign * 0.0015, 0.004), (0.0, sign, 0.0), (0, 0)), m.vert((x1, sign * 0.0015, 0.004), (0.0, sign, 0.0), (1, 0)),
                   m.vert((x1, 0.0, 0.004 + h1), (0.0, sign, 0.3), (1, 1)), m.vert((x0, 0.0, 0.004 + h0), (0.0, sign, 0.3), (0, 1))]
            m.quad("Steel", *ids)
        ids = [m.vert((x0, -0.0015, 0.004), (0.0, 0.0, -1.0), (0, 0)), m.vert((x0, 0.0015, 0.004), (0.0, 0.0, -1.0), (1, 0)),
               m.vert((x1, 0.0015, 0.004), (0.0, 0.0, -1.0), (1, 1)), m.vert((x1, -0.0015, 0.004), (0.0, 0.0, -1.0), (0, 1))]
        m.quad("Steel", *ids)
    box(m, "Steel", (0.012, 0.04, 0.03), at=(0.0, 0.0, 0.0))
    return m


def build_canteen():
    m = Mesh()
    lathe(m, "Steel", [(0.0, 0.0), (0.075, 0.004), (0.085, 0.03), (0.085, 0.13), (0.07, 0.17), (0.03, 0.19), (0.028, 0.2), (0.0, 0.2)],
          sides=18, squash=(1.0, 0.55))
    lathe(m, "Plastic", [(0.0, 0.2), (0.032, 0.2), (0.032, 0.225), (0.0, 0.225)], sides=12)
    return m


def build_lighter():
    m = Mesh()
    box(m, "Plastic", (0.025, 0.013, 0.06), at=(0.0, 0.0, 0.0))
    box(m, "Steel", (0.025, 0.013, 0.02), at=(0.0, 0.0, 0.06))
    return m


def build_torch():
    m = Mesh()
    stick(m, "Wood", (-0.3, 0.0, 0.03), (0.22, 0.0, 0.03), 0.016, 0.018, sides=8, wobble=0.012, seed=31, steps=5, cap=False)
    stick(m, "Fiber", (0.18, 0.0, 0.03), (0.33, 0.0, 0.03), 0.034, 0.03, sides=9, wobble=0.04, seed=33, steps=4)
    for x in (0.2, 0.235, 0.27):
        ring(m, "Fiber", (x, 0.0, 0.03), 0.036, 0.005, axis=(1.0, 0.0, 0.0), sides=5, segments=12)
    return m


def build_raft_kit():
    m = Mesh()
    for y, z in ((-0.1, 0.05), (0.0, 0.05), (0.1, 0.05), (-0.05, 0.14), (0.05, 0.14)):
        stick(m, "Bark", (-0.3, y, z), (0.3, y, z), 0.05, 0.045, sides=9, wobble=0.01, seed=int(37 + y * 100 + z * 10), steps=4)
    for x in (-0.18, 0.18):
        ring(m, "Rope", (x, 0.0, 0.095), 0.165, 0.008, axis=(1.0, 0.0, 0.0), sides=6, segments=14)
    return m


def build_campfire_kit():
    m = Mesh()
    for k in range(7):
        a = 2.0 * math.pi * k / 7
        stone = Mesh()
        lathe(stone, "Stone", [(0.0, 0.0), (0.05, 0.02), (0.055, 0.045), (0.04, 0.07), (0.0, 0.08)], sides=9, squash=(1.0, 0.8), bumps=0.12, seed=41 + k)
        offset = len(m.verts)
        for p, n, uv in stone.verts:
            m.verts.append(((p[0] + math.cos(a) * 0.22, p[1] + math.sin(a) * 0.22, p[2]), n, uv))
        for material, faces in stone.faces.items():
            for i, j, l in faces:
                m.tri(material, i + offset, j + offset, l + offset)
    return m


def build_crate():
    m = Mesh()
    box(m, "Wood", (0.4, 0.4, 0.3))
    return m


# --- The structures ----------------------------------------------------------------------------------------------
# One model per build stage, SM_Structure_<type>_<stage>; the last stage is the finished thing (RiptideStructures.cpp).

def merge(m, sub, at=(0.0, 0.0, 0.0), yaw=0.0):
    """Adds another mesh into m, moved to `at` and turned by yaw radians."""
    c, s = math.cos(yaw), math.sin(yaw)
    offset = len(m.verts)
    for p, n, uv in sub.verts:
        m.verts.append(((at[0] + p[0] * c - p[1] * s, at[1] + p[0] * s + p[1] * c, at[2] + p[2]),
                        (n[0] * c - n[1] * s, n[0] * s + n[1] * c, n[2]), uv))
    for material, faces in sub.faces.items():
        for a, b, d in faces:
            m.tri(material, a + offset, b + offset, d + offset)


def _stone(seed, size=1.0):
    stone = Mesh()
    lathe(stone, "Stone", [(0.0, 0.0), (0.06 * size, 0.025 * size), (0.065 * size, 0.055 * size), (0.045 * size, 0.085 * size), (0.0, 0.1 * size)],
          sides=9, squash=(1.0, 0.8), bumps=0.12, seed=seed)
    return stone


def _stone_ring(m, count=9, radius=0.42):
    for k in range(count):
        a = 2.0 * math.pi * k / count
        merge(m, _stone(61 + k, 1.0 + 0.2 * _hash(k, 1, 3)), (math.cos(a) * radius, math.sin(a) * radius, 0.0), a)


def build_campfire_0():
    # A patch scraped clear: two sticks laid across to mark it.
    m = Mesh()
    stick(m, "Driftwood", (-0.3, -0.05, 0.02), (0.3, 0.05, 0.02), 0.02, 0.015, sides=7, wobble=0.03, seed=71, steps=5)
    stick(m, "Driftwood", (-0.05, 0.3, 0.02), (0.08, -0.3, 0.02), 0.018, 0.014, sides=7, wobble=0.03, seed=72, steps=5)
    return m


def build_campfire_1():
    m = Mesh()
    _stone_ring(m)
    return m


def build_campfire_2():
    m = Mesh()
    _stone_ring(m)
    # Wood stood in a cone over a bed of smaller sticks.
    for k in range(6):
        a = 2.0 * math.pi * k / 6 + 0.3
        stick(m, "Bark", (math.cos(a) * 0.26, math.sin(a) * 0.26, 0.02), (math.cos(a) * 0.03, math.sin(a) * 0.03, 0.42), 0.03, 0.02, sides=7, wobble=0.02, seed=81 + k, steps=4)
    for k in range(4):
        a = 2.0 * math.pi * k / 4 + 0.8
        stick(m, "Driftwood", (math.cos(a) * 0.2, math.sin(a) * 0.2, 0.03), (-math.cos(a) * 0.2, -math.sin(a) * 0.2, 0.03), 0.015, 0.012, sides=6, wobble=0.03, seed=91 + k, steps=4)
    return m


def _thatch(m, start, end, width, count, seed):
    """Fronds laid side by side along a slope: thin flat quads, lit from both sides."""
    rng = random.Random(seed)
    axis = _norm(_sub(end, start))
    side = _norm(_cross(axis, (0.0, 0.0, 1.0)))
    for i in range(count):
        t = (i + 0.5) / count
        off = _mul(side, (t - 0.5) * width)
        a, b = _add(start, off), _add(end, off)
        b = _add(b, _mul(axis, rng.uniform(-0.1, 0.1)))
        w = width / count * 0.6
        up = _norm(_cross(side, axis))
        for sign in (1.0, -1.0):
            n = _mul(up, sign)
            z = 0.004 * sign
            ids = [m.vert(_add(a, _add(_mul(side, -w), (0, 0, z))), n, (0, 0)), m.vert(_add(a, _add(_mul(side, w), (0, 0, z))), n, (1, 0)),
                   m.vert(_add(b, _add(_mul(side, w), (0, 0, z))), n, (1, 1)), m.vert(_add(b, _add(_mul(side, -w), (0, 0, z))), n, (0, 1))]
            if sign > 0:
                m.quad("Frond", *ids)
            else:
                m.quad("Frond", ids[3], ids[2], ids[1], ids[0])


def build_lean_to_0():
    # Two forked uprights, a ridge pole, and a slope of sticks thatched with fronds. Open to -x.
    m = Mesh()
    for y in (-1.1, 1.1):
        stick(m, "Bark", (0.0, y, 0.0), (0.0, y, 1.5), 0.045, 0.035, sides=8, wobble=0.01, seed=101 + int(y > 0), steps=4)
    stick(m, "Bark", (0.0, -1.25, 1.52), (0.0, 1.25, 1.52), 0.04, 0.04, sides=8, wobble=0.0, seed=103, steps=3)
    for k in range(7):
        y = -1.05 + k * 0.35
        stick(m, "Driftwood", (0.0, y, 1.5), (1.6, y, 0.02), 0.025, 0.02, sides=6, wobble=0.015, seed=110 + k, steps=4)
    _thatch(m, (0.02, 0.0, 1.56), (1.68, 0.0, 0.08), 2.3, 22, seed=120)
    return m


def build_tent_0():
    # Four pegs round the pitch.
    m = Mesh()
    for x, y in ((-1.0, -0.8), (1.0, -0.8), (-1.0, 0.8), (1.0, 0.8)):
        stick(m, "Driftwood", (x, y, 0.0), (x * 0.95, y * 0.95, 0.25), 0.02, 0.012, sides=6, wobble=0.0, seed=131, steps=2)
    return m


def build_tent_1():
    # The tarp spread flat, pegged.
    m = build_tent_0()
    for sign, z in ((1.0, 0.03), (-1.0, 0.02)):
        ids = [m.vert((-1.1, -0.9, z), (0.0, 0.0, sign), (0, 0)), m.vert((1.1, -0.9, z), (0.0, 0.0, sign), (1, 0)),
               m.vert((1.1, 0.9, z), (0.0, 0.0, sign), (1, 1)), m.vert((-1.1, 0.9, z), (0.0, 0.0, sign), (0, 1))]
        m.quad("Canvas", *ids) if sign > 0 else m.quad("Canvas", ids[3], ids[2], ids[1], ids[0])
    return m


def build_tent_2():
    # An A-frame: ridge pole on two crossed sticks, the tarp over it, guy ropes to the pegs.
    m = build_tent_0()
    for x in (-1.05, 1.05):
        for sy in (-1.0, 1.0):
            stick(m, "Driftwood", (x, sy * 0.75, 0.0), (x, 0.0, 1.25), 0.025, 0.02, sides=6, wobble=0.0, seed=141, steps=2)
    stick(m, "Bark", (-1.2, 0.0, 1.27), (1.2, 0.0, 1.27), 0.03, 0.03, sides=7, wobble=0.0, seed=142, steps=2)
    for sy in (-1.0, 1.0):
        n_out = _norm((0.0, sy, 0.6))
        for sign in (1.0, -1.0):
            n = _mul(n_out, sign)
            ids = [m.vert((-1.1, sy * 0.85, 0.02), n, (0, 0)), m.vert((1.1, sy * 0.85, 0.02), n, (1, 0)),
                   m.vert((1.1, 0.0, 1.26), n, (1, 1)), m.vert((-1.1, 0.0, 1.26), n, (0, 1))]
            m.quad("Canvas", *ids) if sign > 0 else m.quad("Canvas", ids[3], ids[2], ids[1], ids[0])
    for x in (-1.1, 1.1):
        for sy in (-1.0, 1.0):
            m.tube("Rope", [(x, 0.0, 1.26), (x * 0.95 + (0.0 if x < 0 else 0.0), sy * 0.78, 0.22)], [0.006, 0.006], 5, v_per_metre=10.0)
    return m


def build_compost_bin_0():
    m = Mesh()
    for k in range(4):
        a = math.pi / 2 * k
        for z in (0.12, 0.3, 0.48, 0.66):
            x, y = math.cos(a) * 0.45, math.sin(a) * 0.45
            sx, sy = -math.sin(a), math.cos(a)
            stick(m, "Driftwood", (x - sx * 0.5, y - sy * 0.5, z), (x + sx * 0.5, y + sy * 0.5, z), 0.03, 0.028, sides=6, wobble=0.0, seed=151 + k, steps=2)
    for x, y in ((-0.45, -0.45), (0.45, -0.45), (0.45, 0.45), (-0.45, 0.45)):
        stick(m, "Bark", (x, y, 0.0), (x, y, 0.8), 0.035, 0.03, sides=7, wobble=0.0, seed=160, steps=2)
    return m


def build_drying_rack_0():
    m = Mesh()
    for x in (-0.8, 0.8):
        for sy in (-1.0, 1.0):
            stick(m, "Driftwood", (x, sy * 0.45, 0.0), (x, 0.0, 1.5), 0.028, 0.022, sides=6, wobble=0.0, seed=171, steps=2)
    for z in (1.5, 1.1):
        stick(m, "Bark", (-0.95, 0.0, z), (0.95, 0.0, z), 0.03, 0.03, sides=7, wobble=0.0, seed=172, steps=2)
    for x in (-0.5, -0.15, 0.2, 0.55):
        m.tube("Rope", [(x, 0.0, 1.5), (x, 0.0, 1.2)], [0.005, 0.005], 5, v_per_metre=10.0)
    return m


def _sandbag(seed):
    bag = Mesh()
    lathe(bag, "Sand", [(0.0, 0.0), (0.2, 0.03), (0.24, 0.1), (0.2, 0.17), (0.0, 0.2)], sides=10, squash=(1.0, 0.6), bumps=0.05, seed=seed)
    return bag


def build_sandbag_wall_0():
    m = Mesh()
    stick(m, "Driftwood", (-1.0, 0.0, 0.02), (1.0, 0.0, 0.02), 0.02, 0.02, sides=6, wobble=0.0, seed=181, steps=2)
    return m


def build_sandbag_wall_1():
    m = Mesh()
    for x in (-0.3, 0.3):
        merge(m, _sandbag(185), (x, 0.0, 0.0))
    return m


def build_sandbag_wall_2():
    m = Mesh()
    for x in (-0.6, 0.0, 0.6):
        merge(m, _sandbag(186), (x, 0.0, 0.0))
    for x in (-0.3, 0.3):
        merge(m, _sandbag(187), (x, 0.0, 0.19))
    return m


def build_storage_crate_0():
    m = Mesh()
    box(m, "Wood", (0.9, 0.6, 0.55))
    box(m, "Wood", (0.96, 0.66, 0.06), at=(0.0, 0.0, 0.55))
    for x in (-0.3, 0.0, 0.3):
        box(m, "Driftwood", (0.06, 0.62, 0.5), at=(x, 0.0, 0.03))
    return m


def build_raft_site_0():
    # Two skids on the sand to lay the logs across.
    m = Mesh()
    for y in (-0.9, 0.9):
        stick(m, "Driftwood", (-1.2, y, 0.04), (1.2, y, 0.04), 0.04, 0.04, sides=7, wobble=0.01, seed=191, steps=3)
    return m


def build_raft_site_1():
    m = build_raft_site_0()
    for k in range(6):
        x = -1.0 + k * 0.4
        stick(m, "Bark", (x, -1.3, 0.17), (x, 1.3, 0.17), 0.1, 0.09, sides=10, wobble=0.0, seed=201 + k, steps=3)
    return m


def build_raft_site_2():
    m = build_raft_site_1()
    for y in (-0.9, 0.0, 0.9):
        stick(m, "Driftwood", (-1.25, y, 0.3), (1.25, y, 0.3), 0.04, 0.04, sides=7, wobble=0.0, seed=211, steps=3)
        for x in (-1.0, -0.2, 0.6):
            ring(m, "Rope", (x, y, 0.22), 0.14, 0.008, axis=(0.0, 1.0, 0.0), sides=6, segments=12)
    return m


STRUCTURES = {
    "campfire_0": build_campfire_0, "campfire_1": build_campfire_1, "campfire_2": build_campfire_2,
    "lean_to_0": build_lean_to_0,
    "tent_0": build_tent_0, "tent_1": build_tent_1, "tent_2": build_tent_2,
    "compost_bin_0": build_compost_bin_0, "drying_rack_0": build_drying_rack_0,
    "sandbag_wall_0": build_sandbag_wall_0, "sandbag_wall_1": build_sandbag_wall_1, "sandbag_wall_2": build_sandbag_wall_2,
    "storage_crate_0": build_storage_crate_0,
    "raft_site_0": build_raft_site_0, "raft_site_1": build_raft_site_1, "raft_site_2": build_raft_site_2,
}


MODELS = {
    "stone": build_stone, "flint": build_flint, "coconut": build_coconut, "driftwood": build_driftwood, "log": build_log,
    "fiber": build_fiber, "rope": build_rope, "stone_hatchet": build_stone_hatchet, "oar": build_oar, "knife": build_knife,
    "canteen": build_canteen, "canteen_clean": build_canteen, "canteen_dirty": build_canteen, "lighter": build_lighter,
    "torch": build_torch, "raft_kit": build_raft_kit, "campfire_kit": build_campfire_kit, "crate": build_crate,
}

# What each material slot looks like: colour (linear), roughness, metallic.
FINISHES = {
    "Wood": ((0.36, 0.24, 0.12), 0.75, 0.0), "Driftwood": ((0.55, 0.5, 0.42), 0.85, 0.0), "Bark": ((0.26, 0.18, 0.11), 0.9, 0.0),
    "Stone": ((0.42, 0.4, 0.36), 0.8, 0.0), "Flint": ((0.14, 0.14, 0.15), 0.35, 0.0), "Fiber": ((0.62, 0.52, 0.3), 0.9, 0.0),
    "Rope": ((0.5, 0.42, 0.28), 0.85, 0.0), "Husk": ((0.3, 0.19, 0.09), 0.85, 0.0), "Steel": ((0.5, 0.5, 0.52), 0.35, 1.0),
    "Plastic": ((0.08, 0.1, 0.12), 0.45, 0.0), "Canvas": ((0.3, 0.32, 0.22), 0.9, 0.0),
    "Frond": ((0.42, 0.36, 0.14), 0.85, 0.0), "Sand": ((0.72, 0.66, 0.5), 0.95, 0.0),
}


def write_item_models(out_dir):
    """Writes every item's and structure stage's OBJ into out_dir. Returns {asset name: path}."""
    os.makedirs(out_dir, exist_ok=True)
    written = {}
    for item_id, build in MODELS.items():
        path = os.path.join(out_dir, f"SM_Item_{item_id}.obj")
        build().write_obj(path, comment=f"Riptide item {item_id}, generated by riptide_item_models.py")
        written[f"SM_Item_{item_id}"] = path
    for stage_id, build in STRUCTURES.items():
        path = os.path.join(out_dir, f"SM_Structure_{stage_id}.obj")
        build().write_obj(path, comment=f"Riptide structure {stage_id}, generated by riptide_item_models.py")
        written[f"SM_Structure_{stage_id}"] = path
    return written


# --- Unreal ------------------------------------------------------------------------------------------------------

ITEMS_PATH = "/Game/Riptide/Items"


def _finish_material(slot, unreal):
    """M_Item_<slot>: a plain finish with a little grain, so coded items aren't flat."""
    import riptide_island_props
    path = f"{ITEMS_PATH}/Materials/M_Item_{slot}"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == ITEM_MODELS_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    (r, g, b), rough, metal = FINISHES[slot]
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"M_Item_{slot}", f"{ITEMS_PATH}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -700, 0)
    pin = unreal.CustomInput()
    pin.set_editor_property("input_name", "P")
    node.set_editor_property("inputs", [pin])
    out = unreal.CustomOutput()
    out.set_editor_property("output_name", "Rough")
    out.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    node.set_editor_property("additional_outputs", [out])
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("code", f"""
float3 q = P / 1.7;
float g = frac(sin(dot(floor(q), float3(127.1, 311.7, 74.7))) * 43758.5453);
float3 f = frac(q); f = f * f * (3.0 - 2.0 * f);
float g2 = frac(sin(dot(floor(q) + 1.0, float3(127.1, 311.7, 74.7))) * 43758.5453);
float grain = lerp(g, g2, f.x * 0.5 + f.z * 0.5);
Rough = {rough} + (grain - 0.5) * 0.12;
return float3({r}, {g}, {b}) * (0.88 + grain * 0.24);
""")
    mel.connect_material_expressions(world, "", node, "P")
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    metallic = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 300)
    metallic.set_editor_property("r", metal)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(mat)
    riptide_island_props.mark_usage(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", ITEM_MODELS_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def make_item_assets():
    """Builds and imports every item model when missing or when ITEM_MODELS_VERSION changes, and writes the item
    table out as JSON. Items without a model are logged."""
    import unreal
    assets = unreal.EditorAssetLibrary
    marker = f"{ITEMS_PATH}/SM_Item_crate"
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    tables = os.path.join(saved, "Generated", "riptide_tables.json")
    os.makedirs(os.path.dirname(tables), exist_ok=True)
    unreal.RiptideDataLibrary.export_tables(tables)
    ids = [str(i) for i in unreal.RiptideDataLibrary.item_ids()]
    missing = [i for i in ids if i not in MODELS]
    if assets.does_asset_exist(marker) and assets.get_metadata_tag(unreal.load_asset(marker), "RiptideVersion") == ITEM_MODELS_VERSION:
        if missing:
            unreal.log(f"Riptide: {len(missing)} of {len(ids)} items still have no model (drawn as a crate): {', '.join(missing)}")
        return
    unreal.log("Riptide: building the item models")
    if assets.does_directory_exist(ITEMS_PATH):
        assets.delete_directory(ITEMS_PATH)
    written = write_item_models(os.path.join(saved, "Generated", "Items"))
    tasks = []
    for asset_name, obj in written.items():
        task = unreal.AssetImportTask()
        task.filename = obj
        task.destination_path = ITEMS_PATH
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    finishes = {slot: _finish_material(slot, unreal) for slot in FINISHES}
    tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for asset_name in written:
        path = f"{ITEMS_PATH}/{asset_name}"
        mesh = unreal.load_asset(path)
        if not mesh:
            unreal.log_error(f"Riptide: model {asset_name} failed to import")
            continue
        item_id = asset_name.split("_", 2)[2]
        slots = mesh.get_editor_property("static_materials")
        for i, slot in enumerate(slots):
            name = str(slot.get_editor_property("material_slot_name"))
            if name in finishes:
                slot.set_editor_property("material_interface", finishes[name])
                slots[i] = slot
            else:
                unreal.log_warning(f"Riptide: item {item_id} slot '{name}' has no finish")
        mesh.set_editor_property("static_materials", slots)
        # Small things: no Nanite, and a box round the whole thing as its physics body, so it can fall and settle.
        nanite = tools.get_nanite_settings(mesh)
        nanite.set_editor_property("enabled", False)
        tools.set_nanite_settings(mesh, nanite, True)
        if asset_name.startswith("SM_Structure_"):
            # Built things are walked round and into (under a lean-to): their own triangles are the collision.
            body = mesh.get_editor_property("body_setup")
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            assets.set_metadata_tag(mesh, "RiptideVersion", ITEM_MODELS_VERSION)
            assets.save_asset(path, only_if_is_dirty=False)
            continue
        box = mesh.get_bounding_box()
        shape = unreal.KBoxElem()
        shape.set_editor_property("center", (box.min + box.max) * 0.5)
        shape.set_editor_property("x", max(box.max.x - box.min.x, 2.0))
        shape.set_editor_property("y", max(box.max.y - box.min.y, 2.0))
        shape.set_editor_property("z", max(box.max.z - box.min.z, 2.0))
        geom = unreal.KAggregateGeom()
        geom.set_editor_property("box_elems", [shape])
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("agg_geom", geom)
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
        assets.set_metadata_tag(mesh, "RiptideVersion", ITEM_MODELS_VERSION)
        assets.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"Riptide: {len(written)} item models ready; {len(missing)} of {len(ids)} items have no model yet: {', '.join(missing)}")


if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for item_id, path in write_item_models(out).items():
        print(item_id, path)
