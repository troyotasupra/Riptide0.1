"""The rest of the game's items as coded models (riptide_item_models.py holds the first few and the shared shapes).

Every builder returns a riptide_palm_mesh.Mesh in metres, lying on the ground at z = 0 the way the thing would come to
rest (a rifle on its side, a shirt folded flat), roughly centred on the origin. Families share a builder with
settings: the fish by species, the berries by kind, the ammunition by calibre, the clothes by garment. Weapons and the
outboard reuse the models the crew and the boat already have (riptide_crew_mesh.build_rifle,
riptide_boat_mesh.build_outboard), turned and scaled to lie on the ground.

New material slots are listed in FINISHES and merged into riptide_item_models.FINISHES.
"""
import math
import random

from riptide_palm_mesh import Mesh, _add, _cross, _mul, _norm, _sub
from riptide_item_models import box, lathe, merge, ring, stick, _sandbag, _hash

# What each new slot looks like: colour (linear), roughness, metallic.
FINISHES = {
    "FishSilver": ((0.55, 0.58, 0.62), 0.35, 0.3), "FishBlue": ((0.12, 0.22, 0.38), 0.35, 0.2),
    "FishRed": ((0.6, 0.12, 0.08), 0.4, 0.1), "FishGold": ((0.75, 0.6, 0.12), 0.35, 0.2),
    "FishGreen": ((0.25, 0.4, 0.2), 0.4, 0.1), "FishBrown": ((0.3, 0.22, 0.14), 0.5, 0.0),
    "FishDark": ((0.12, 0.13, 0.15), 0.4, 0.1), "FishBelly": ((0.85, 0.85, 0.8), 0.4, 0.1),
    "FishFlesh": ((0.85, 0.5, 0.45), 0.55, 0.0), "Cooked": ((0.42, 0.24, 0.1), 0.7, 0.0),
    "Dried": ((0.28, 0.18, 0.1), 0.85, 0.0), "RawMeat": ((0.55, 0.08, 0.07), 0.5, 0.0),
    "Fat": ((0.85, 0.78, 0.65), 0.6, 0.0), "Spoiled": ((0.25, 0.3, 0.12), 0.8, 0.0),
    "BerryRed": ((0.6, 0.03, 0.05), 0.3, 0.0), "BerryBlue": ((0.1, 0.12, 0.35), 0.3, 0.0),
    "BerryDried": ((0.22, 0.06, 0.08), 0.8, 0.0), "Leaf": ((0.12, 0.3, 0.06), 0.6, 0.0),
    "Soil": ((0.16, 0.11, 0.07), 0.95, 0.0), "Paper": ((0.8, 0.76, 0.62), 0.9, 0.0),
    "Leather": ((0.32, 0.2, 0.1), 0.7, 0.0), "Brass": ((0.7, 0.5, 0.18), 0.3, 1.0),
    "Copper": ((0.7, 0.35, 0.2), 0.35, 1.0), "Red": ((0.6, 0.04, 0.03), 0.5, 0.0),
    "Orange": ((0.85, 0.3, 0.02), 0.5, 0.0), "Yellow": ((0.8, 0.65, 0.05), 0.5, 0.0),
    "Green": ((0.12, 0.25, 0.1), 0.6, 0.0), "White": ((0.8, 0.8, 0.78), 0.6, 0.0),
    "Black": ((0.02, 0.02, 0.02), 0.5, 0.0), "GunMetal": ((0.04, 0.04, 0.045), 0.35, 0.8),
    "Polymer": ((0.05, 0.05, 0.05), 0.55, 0.0), "Glass": ((0.04, 0.08, 0.1), 0.05, 0.0),
    "Rubber": ((0.03, 0.03, 0.03), 0.8, 0.0), "Cloth": ((0.7, 0.7, 0.68), 0.9, 0.0),
    "ClothBlue": ((0.12, 0.2, 0.4), 0.9, 0.0), "ClothRed": ((0.5, 0.08, 0.06), 0.9, 0.0),
    "Khaki": ((0.45, 0.38, 0.22), 0.9, 0.0), "Wool": ((0.4, 0.3, 0.22), 1.0, 0.0),
    "Olive": ((0.22, 0.24, 0.13), 0.85, 0.0), "TarpBlue": ((0.05, 0.2, 0.5), 0.6, 0.0),
    "Bone": ((0.85, 0.82, 0.72), 0.6, 0.0), "Feather": ((0.7, 0.68, 0.62), 0.9, 0.0),
}


# --- Shapes --------------------------------------------------------------------------------------------------------

def _rotate(p, roll, pitch, yaw):
    """Turns p about x (roll), then y (pitch), then z (yaw), radians."""
    x, y, z = p
    c, s = math.cos(roll), math.sin(roll)
    y, z = y * c - z * s, y * s + z * c
    c, s = math.cos(pitch), math.sin(pitch)
    x, z = x * c + z * s, -x * s + z * c
    c, s = math.cos(yaw), math.sin(yaw)
    x, y = x * c - y * s, x * s + y * c
    return (x, y, z)


def place(m, sub, at=(0.0, 0.0, 0.0), roll=0.0, pitch=0.0, yaw=0.0, scale=1.0, rename=None):
    """Adds sub into m turned (roll, pitch, yaw), scaled and moved; rename maps sub's material slots to others."""
    offset = len(m.verts)
    for p, n, uv in sub.verts:
        q = _rotate(_mul(p, scale), roll, pitch, yaw)
        m.verts.append((_add(q, at), _rotate(n, roll, pitch, yaw), uv))
    for material, faces in sub.faces.items():
        slot = (rename or {}).get(material, material)
        for a, b, d in faces:
            m.tri(slot, a + offset, b + offset, d + offset)


def grounded(sub):
    """sub moved so it rests on z = 0, centred over the origin."""
    xs = [p[0] for p, _, _ in sub.verts]
    ys = [p[1] for p, _, _ in sub.verts]
    zs = [p[2] for p, _, _ in sub.verts]
    shift = (-(min(xs) + max(xs)) / 2.0, -(min(ys) + max(ys)) / 2.0, -min(zs))
    m = Mesh()
    place(m, sub, at=shift)
    return m


def blob(m, material, centre, size, sides=10, seed=1, bumps=0.08, squash=(1.0, 1.0)):
    """A rounded lump (a berry, a grub segment, a lump of soil): size is (radius, height)."""
    r, h = size
    profile = [(0.0, 0.0)] + [(r * math.sin(math.pi * t), h * 0.5 * (1.0 - math.cos(math.pi * t))) for t in (0.2, 0.4, 0.6, 0.8)] + [(0.0, h)]
    sub = Mesh()
    lathe(sub, material, profile, sides=sides, squash=squash, bumps=bumps, seed=seed)
    place(m, sub, at=centre)


def cylinder_x(m, material, x0, x1, radius, y=0.0, z=None, sides=12, radius_end=None):
    """A capped tube along x (a barrel, a scope, a cartridge lying down)."""
    z = radius if z is None else z
    sub = Mesh()
    lathe(sub, material, [(0.0, 0.0), (radius, 0.0), (radius_end if radius_end is not None else radius, x1 - x0),
                          (0.0, x1 - x0)], sides=sides)
    place(m, sub, at=(x0, y, z), pitch=math.pi / 2.0)


def slab(m, material, length, width, thick, at=(0.0, 0.0, 0.0), round_ends=0.3, sides=12, yaw=0.0):
    """A flat rounded slab (a fillet, a steak, a folded garment's soft edge): an ellipse-ish outline extruded."""
    outline = []
    for k in range(sides * 2):
        a = 2.0 * math.pi * k / (sides * 2)
        c, s = math.cos(a), math.sin(a)
        # A superellipse: rounder ends as round_ends grows toward 1.
        e = 2.0 / (1.0 + round_ends * 3.0)
        x = math.copysign(abs(c) ** e, c) * length / 2.0
        y = math.copysign(abs(s) ** e, s) * width / 2.0
        outline.append((x, y))
    cy, sy = math.cos(yaw), math.sin(yaw)

    def p(x, y, z):
        return (at[0] + x * cy - y * sy, at[1] + x * sy + y * cy, at[2] + z)
    top = [m.vert(p(x, y, thick), (0.0, 0.0, 1.0), (x / length + 0.5, y / width + 0.5)) for x, y in outline]
    bottom = [m.vert(p(x, y, 0.0), (0.0, 0.0, -1.0), (x / length + 0.5, y / width + 0.5)) for x, y in outline]
    ct = m.vert(p(0.0, 0.0, thick), (0.0, 0.0, 1.0), (0.5, 0.5))
    cb = m.vert(p(0.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.5, 0.5))
    n = len(outline)
    for k in range(n):
        m.tri(material, top[k], top[(k + 1) % n], ct)
        m.tri(material, bottom[(k + 1) % n], bottom[k], cb)
        x0, y0 = outline[k]
        x1, y1 = outline[(k + 1) % n]
        side = _norm((y1 - y0, -(x1 - x0), 0.0))
        side = (side[0] * cy - side[1] * sy, side[0] * sy + side[1] * cy, 0.0)
        ids = [m.vert(p(x0, y0, 0.0), side, (k / n, 0)), m.vert(p(x1, y1, 0.0), side, ((k + 1) / n, 0)),
               m.vert(p(x1, y1, thick), side, ((k + 1) / n, 1)), m.vert(p(x0, y0, thick), side, (k / n, 1))]
        m.quad(material, *ids)


def heap(m, material, radius, height, seed, bumps=0.15):
    """A loose pile (sand, soil): a low lumpy mound."""
    profile = [(0.0, 0.0), (radius, 0.0), (radius * 0.85, height * 0.25), (radius * 0.55, height * 0.65), (radius * 0.2, height * 0.95), (0.0, height)]
    lathe(m, material, profile, sides=16, bumps=bumps, seed=seed)


# --- Food ------------------------------------------------------------------------------------------------------------

def _berry_cluster(skin, seed, count=9, size=0.009, stem=True):
    m = Mesh()
    rng = random.Random(seed)
    for k in range(count):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.0, 0.025)
        z = 0.0 if d > 0.012 else rng.uniform(0.004, 0.01)
        blob(m, skin, (math.cos(a) * d, math.sin(a) * d, z), (size * rng.uniform(0.85, 1.15), size * 1.8), sides=8, seed=seed + k, bumps=0.04)
    if stem:
        stick(m, "Wood", (0.0, 0.0, 0.012), (0.035, 0.01, 0.022), 0.0015, 0.001, sides=4, steps=3)
        blob(m, "Leaf", (0.035, 0.01, 0.02), (0.012, 0.003), sides=6, seed=seed + 99, bumps=0.0, squash=(1.0, 0.45))
    return m


def build_berries():
    return _berry_cluster("BerryBlue", 201)


def build_red_berries():
    return _berry_cluster("BerryRed", 203)


def build_dried_berries():
    return _berry_cluster("BerryDried", 205, count=12, size=0.006, stem=False)


# Fish: length (m), body depth and width as fractions of length, skin, back, and how the head and tail look.
FISH = {
    "fish": (0.32, 0.22, 0.1, "FishSilver", "FishBlue", 0.5),
    "sardine": (0.16, 0.18, 0.09, "FishSilver", "FishBlue", 0.6),
    "mullet": (0.34, 0.2, 0.11, "FishSilver", "FishDark", 0.5),
    "pufferfish": (0.22, 0.5, 0.45, "FishBrown", "FishBrown", 0.2),
    "snapper": (0.4, 0.32, 0.11, "FishRed", "FishRed", 0.5),
    "grouper": (0.55, 0.32, 0.16, "FishBrown", "FishDark", 0.45),
    "barracuda": (0.8, 0.12, 0.07, "FishSilver", "FishDark", 0.7),
    "mahi_mahi": (0.75, 0.28, 0.08, "FishGold", "FishGreen", 0.55),
    "tuna": (0.7, 0.26, 0.15, "FishSilver", "FishBlue", 0.6),
}


def _fish(kind, state):
    """A whole fish lying on its side: raw (its species' colours), cooked (browned) or dried (flat and dark)."""
    length, depth, width, skin, back, tail_size = FISH[kind]
    if state == "cooked":
        skin = back = "Cooked"
    elif state == "dried":
        skin = back = "Dried"
        width *= 0.4
    m = Mesh()
    body = Mesh()
    # The body: a spindle turned about its length, flattened sideways (lying on its side: thin in z).
    n = 10
    profile = []
    for i in range(n + 1):
        t = i / n
        r = math.sin(math.pi * (t ** 0.85)) * 0.5 * length * depth
        profile.append((max(r, 0.0005), t * length * (1.0 - tail_size * 0.25)))
    lathe(body, skin, profile, sides=14, squash=(width / depth, 1.0))
    # Turned to lie along x on its side: the lathe's squashed axis becomes its thickness (z), its depth stays y.
    place(m, body, at=(-length * 0.5, 0.0, length * width * 0.5), pitch=math.pi / 2.0)
    # The belly paler, a stripe of the back colour along the top edge, an eye, and the tail fin.
    body_end = -length * 0.5 + length * (1.0 - tail_size * 0.25)
    zc = length * width * 0.5
    fin = min(tail_size * length * 0.35, length * depth * 0.55)     # a slim fish (a barracuda) has a slim tail
    ids = [m.vert((body_end - 0.01, 0.0, zc), (0.0, 0.0, 1.0), (0, 0)),
           m.vert((body_end + fin, fin * 0.9, zc), (0.0, 0.0, 1.0), (1, 0)),
           m.vert((body_end + fin * 0.75, 0.0, zc), (0.0, 0.0, 1.0), (1, 0.5)),
           m.vert((body_end + fin, -fin * 0.9, zc), (0.0, 0.0, 1.0), (1, 1))]
    m.tri(back, ids[0], ids[1], ids[2])
    m.tri(back, ids[0], ids[2], ids[3])
    m.tri(back, ids[2], ids[1], ids[0])
    m.tri(back, ids[3], ids[2], ids[0])
    if state != "dried":
        eye = (-length * 0.5 + length * 0.12, length * depth * 0.12, zc + length * width * 0.42)
        blob(m, "Black", eye, (length * 0.018, length * 0.012), sides=6, bumps=0.0)
        # The dorsal fin along the back (here +y, lying on its side).
        top = length * depth * 0.5
        ids = [m.vert((-length * 0.15, top * 0.95, zc), (0.0, 0.0, 1.0), (0, 0)), m.vert((length * 0.18, top * 0.85, zc), (0.0, 0.0, 1.0), (1, 0)),
               m.vert((length * 0.05, top * 1.35, zc), (0.0, 0.0, 1.0), (0.5, 1))]
        m.tri(back, ids[0], ids[1], ids[2])
        m.tri(back, ids[2], ids[1], ids[0])
    return grounded(m)


def _fish_builder(kind, state):
    return lambda: _fish(kind, state)


def build_fish_steak():
    m = Mesh()
    slab(m, "FishFlesh", 0.16, 0.12, 0.025, round_ends=0.8)
    ring(m, "FishSilver", (0.0, 0.0, 0.0125), 0.068, 0.006, axis=(0.0, 0.0, 1.0), sides=5, segments=20)
    return m


def _meat(material, seed, fat=True, scale=1.0):
    m = Mesh()
    slab(m, material, 0.17 * scale, 0.11 * scale, 0.035 * scale, round_ends=0.6)
    if fat:
        slab(m, "Fat", 0.17 * scale, 0.02 * scale, 0.035 * scale, at=(0.0, 0.055 * scale, 0.0), round_ends=0.2)
    return m


def build_raw_meat():
    return _meat("RawMeat", 1)


def build_cooked_meat():
    return _meat("Cooked", 2)


def build_dried_meat():
    m = Mesh()
    for k in range(3):
        slab(m, "Dried", 0.15, 0.03, 0.006, at=(0.0, (k - 1) * 0.035, k * 0.006), round_ends=0.2, yaw=0.1 * (k - 1))
    return m


def build_raw_shark_meat():
    m = Mesh()
    slab(m, "FishFlesh", 0.22, 0.12, 0.05, round_ends=0.4)
    slab(m, "FishDark", 0.22, 0.01, 0.05, at=(0.0, 0.06, 0.0), round_ends=0.1)
    return m


def build_cooked_shark():
    m = Mesh()
    slab(m, "Cooked", 0.2, 0.11, 0.045, round_ends=0.4)
    return m


def build_spoiled_food():
    m = Mesh()
    for k in range(4):
        blob(m, "Spoiled", (0.03 * math.cos(k * 1.7), 0.03 * math.sin(k * 1.7), 0.0), (0.035, 0.03), sides=9, seed=300 + k, bumps=0.25)
    return m


def build_ration_pack():
    m = Mesh()
    box(m, "Olive", (0.2, 0.12, 0.035))
    box(m, "Paper", (0.1, 0.07, 0.002), at=(0.0, 0.0, 0.035))
    return m


def build_grub():
    m = Mesh()
    for k in range(7):
        t = k / 6.0
        blob(m, "Bone", (-0.025 + t * 0.05, 0.006 * math.sin(t * 3.0), 0.0), (0.006 * (1.0 - abs(t - 0.4) * 0.6), 0.01), sides=7, bumps=0.0)
    return m


def build_cut_bait():
    m = Mesh()
    slab(m, "FishFlesh", 0.05, 0.03, 0.012, round_ends=0.3)
    slab(m, "FishSilver", 0.05, 0.03, 0.002, at=(0.0, 0.0, 0.012), round_ends=0.3)
    return m


# --- Materials and fishing ----------------------------------------------------------------------------------------

def build_soil():
    m = Mesh()
    heap(m, "Soil", 0.14, 0.07, 401)
    return m


def build_sand():
    m = Mesh()
    heap(m, "Sand", 0.15, 0.06, 403, bumps=0.06)
    return m


def build_tarp():
    m = Mesh()
    for k in range(4):
        slab(m, "TarpBlue", 0.36 - k * 0.01, 0.24 - k * 0.006, 0.008, at=(0.0, 0.0, k * 0.008), round_ends=0.05)
    for x in (-0.16, 0.16):
        ring(m, "Steel", (x, 0.1, 0.033), 0.008, 0.002, sides=4, segments=10)
    return m


def build_paracord():
    m = Mesh()
    for k in range(5):
        ring(m, "Olive", (0.0, 0.0, 0.004 + k * 0.006), 0.055 - k * 0.002, 0.003, sides=5, segments=24)
    return m


def build_lure():
    m = Mesh()
    body = Mesh()
    lathe(body, "FishSilver", [(0.0, 0.0), (0.006, 0.01), (0.009, 0.035), (0.004, 0.06), (0.0, 0.065)], sides=10, squash=(0.6, 1.0))
    place(m, body, at=(-0.032, 0.0, 0.006), pitch=math.pi / 2.0)
    box(m, "Red", (0.012, 0.012, 0.004), at=(-0.03, 0.0, 0.0))
    stick(m, "Steel", (0.034, 0.0, 0.006), (0.05, 0.0, 0.006), 0.0012, 0.0012, sides=4, steps=2)
    ring(m, "Steel", (0.055, 0.0, 0.006), 0.006, 0.001, axis=(0.0, 1.0, 0.0), sides=4, segments=10)
    return m


def build_jig():
    m = Mesh()
    blob(m, "Yellow", (0.0, 0.0, 0.0), (0.008, 0.016), sides=10, bumps=0.0)
    for k in range(6):
        a = 2.0 * math.pi * k / 6
        stick(m, "Red", (0.006, 0.0, 0.008), (0.05, 0.008 * math.cos(a), 0.008 + 0.005 * math.sin(a)), 0.002, 0.0008, sides=4, steps=2)
    stick(m, "Steel", (-0.008, 0.0, 0.008), (-0.03, 0.0, 0.008), 0.001, 0.001, sides=4, steps=2)
    return m


# --- Ammunition ------------------------------------------------------------------------------------------------------

# Calibre: cartridge length and radius (m), case material, and the box's colour and size (x, y, z).
AMMO = {
    "ammo_45": (0.032, 0.0058, "Brass", "Green", (0.11, 0.07, 0.04)),
    "ammo_9mm": (0.029, 0.0049, "Brass", "Yellow", (0.1, 0.065, 0.036)),
    "ammo_556": (0.057, 0.0048, "Brass", "Olive", (0.13, 0.07, 0.03)),
    "ammo_12ga": (0.07, 0.0105, "Red", "Red", (0.14, 0.12, 0.07)),
    "ammo_408": (0.116, 0.0081, "Brass", "Black", (0.16, 0.08, 0.045)),
}


def _ammo(calibre):
    length, radius, case, colour, size = AMMO[calibre]
    m = Mesh()
    box(m, colour, size)
    box(m, "Paper", (size[0] * 0.5, size[1] * 0.6, 0.001), at=(0.0, 0.0, size[2]))
    # A few rounds loose beside the box.
    for k in range(3):
        y = size[1] * 0.5 + radius + 0.004 + k * (radius * 2.2)
        x0 = -length * 0.5
        cylinder_x(m, case, x0, x0 + length * 0.72, radius, y=y)
        cylinder_x(m, "Copper" if case == "Brass" else "Brass", x0 + length * 0.72, x0 + length, radius * 0.85, y=y, z=radius, radius_end=radius * 0.3)
    return m


def _ammo_builder(calibre):
    return lambda: _ammo(calibre)


def build_arrow():
    m = Mesh()
    stick(m, "Wood", (-0.35, 0.0, 0.006), (0.33, 0.0, 0.006), 0.004, 0.004, sides=6, steps=4)
    head = Mesh()
    lathe(head, "Flint", [(0.0, 0.0), (0.009, 0.004), (0.0, 0.04)], sides=4, squash=(1.0, 0.35))
    place(m, head, at=(0.33, 0.0, 0.006), pitch=math.pi / 2.0)
    for sign in (1.0, -1.0):
        ids = [m.vert((-0.34, 0.0, 0.006), (0.0, 0.0, 1.0), (0, 0)), m.vert((-0.26, 0.0, 0.006), (0.0, 0.0, 1.0), (1, 0)),
               m.vert((-0.32, sign * 0.014, 0.006), (0.0, 0.0, 1.0), (0, 1))]
        m.tri("Feather", ids[0], ids[1], ids[2])
        m.tri("Feather", ids[2], ids[1], ids[0])
    return m


def build_flare():
    m = Mesh()
    cylinder_x(m, "Red", -0.1, 0.08, 0.017)
    cylinder_x(m, "Black", 0.08, 0.11, 0.019)
    return m


# --- Blades and tools --------------------------------------------------------------------------------------------

def _blade(m, length, width, at_x, thick=0.002, material="Steel", curve=0.0):
    """A flat blade along +x from at_x: tapering to a point, its spine straight and its edge curved."""
    steps = 8
    for i in range(steps):
        t0, t1 = i / steps, (i + 1) / steps
        x0, x1 = at_x + t0 * length, at_x + t1 * length
        w0 = width * (1.0 - t0 ** 2.5) + curve * math.sin(math.pi * t0) * width
        w1 = width * (1.0 - t1 ** 2.5) + curve * math.sin(math.pi * t1) * width
        for sign in (1.0, -1.0):
            ids = [m.vert((x0, 0.0, thick if sign > 0 else 0.0), (0.0, 0.0, sign), (t0, 0)), m.vert((x1, 0.0, thick if sign > 0 else 0.0), (0.0, 0.0, sign), (t1, 0)),
                   m.vert((x1, -w1, thick * 0.5), (0.0, 0.0, sign), (t1, 1)), m.vert((x0, -w0, thick * 0.5), (0.0, 0.0, sign), (t0, 1))]
            if sign > 0:
                m.quad(material, *ids)
            else:
                m.quad(material, ids[3], ids[2], ids[1], ids[0])


def build_dagger():
    m = Mesh()
    box(m, "Leather", (0.11, 0.024, 0.02), at=(-0.06, 0.0, 0.0))
    box(m, "Steel", (0.01, 0.06, 0.016), at=(0.0, 0.0, 0.002))
    _blade(m, 0.17, 0.014, 0.005, thick=0.004)
    _blade(m, 0.17, -0.014, 0.005, thick=0.004)
    return m


def build_machete():
    m = Mesh()
    box(m, "Polymer", (0.13, 0.03, 0.022), at=(-0.07, 0.0, 0.0))
    _blade(m, 0.44, 0.05, 0.0, thick=0.003, curve=0.3)
    return m


def build_fishing_rod():
    m = Mesh()
    stick(m, "Polymer", (-0.7, 0.0, 0.012), (-0.4, 0.0, 0.009), 0.012, 0.01, sides=8, steps=3)
    stick(m, "FishDark", (-0.4, 0.0, 0.009), (0.75, 0.0, 0.004), 0.007, 0.0025, sides=6, steps=6)
    reel = Mesh()
    lathe(reel, "Steel", [(0.0, 0.0), (0.03, 0.0), (0.032, 0.025), (0.0, 0.025)], sides=14)
    place(m, reel, at=(-0.5, 0.022, 0.03), roll=math.pi / 2.0)
    for x in (-0.2, 0.15, 0.45, 0.68):
        ring(m, "Steel", (x, 0.0, 0.014), 0.005, 0.0008, axis=(1.0, 0.0, 0.0), sides=4, segments=8)
    return m


def build_spear():
    m = Mesh()
    stick(m, "Wood", (-0.8, 0.0, 0.015), (0.75, 0.0, 0.015), 0.015, 0.012, sides=8, wobble=0.005, seed=501, steps=6)
    head = Mesh()
    lathe(head, "Flint", [(0.0, 0.0), (0.02, 0.01), (0.0, 0.1)], sides=4, squash=(1.0, 0.3))
    place(m, head, at=(0.75, 0.0, 0.015), pitch=math.pi / 2.0)
    ring(m, "Rope", (0.74, 0.0, 0.015), 0.016, 0.003, axis=(1.0, 0.0, 0.0), sides=4, segments=10)
    return m


def build_bow():
    m = Mesh()
    pts = []
    for k in range(13):
        t = k / 12.0
        pts.append((-0.6 + t * 1.2, 0.12 * math.sin(math.pi * t), 0.012))
    radii = [0.012 - 0.006 * abs(t / 6.0 - 1.0) for t in range(13)]
    m.tube("Wood", pts, radii, 7, v_per_metre=4.0)
    stick(m, "Fiber", pts[0], pts[-1], 0.0015, 0.0015, sides=4, steps=2)
    ring(m, "Leather", (0.0, 0.12, 0.012), 0.015, 0.004, axis=(1.0, 0.0, 0.0), sides=4, segments=10)
    return m


def build_cleaning_kit():
    m = Mesh()
    box(m, "Olive", (0.18, 0.08, 0.04))
    stick(m, "Brass", (-0.12, 0.06, 0.006), (0.12, 0.06, 0.006), 0.003, 0.003, sides=5, steps=2)
    blob(m, "White", (0.0, 0.0, 0.04), (0.02, 0.01), sides=8, bumps=0.2)
    return m


def build_binoculars():
    m = Mesh()
    for y in (-0.035, 0.035):
        cylinder_x(m, "Polymer", -0.07, 0.06, 0.028, y=y)
        cylinder_x(m, "Glass", 0.06, 0.062, 0.024, y=y, z=0.028)
    box(m, "GunMetal", (0.04, 0.04, 0.012), at=(0.0, 0.0, 0.022))
    return m


def build_handheld_radio():
    upright = Mesh()
    box(upright, "Polymer", (0.065, 0.035, 0.13))
    stick(upright, "Rubber", (0.02, 0.0, 0.13), (0.02, 0.0, 0.25), 0.006, 0.004, sides=6, steps=3)
    box(upright, "Glass", (0.045, 0.002, 0.03), at=(0.0, -0.018, 0.08))
    m = Mesh()
    place(m, upright, roll=math.pi / 2.0)          # lying on its back
    return grounded(m)


def build_tool_kit():
    m = Mesh()
    box(m, "Red", (0.36, 0.16, 0.14))
    box(m, "Steel", (0.36, 0.165, 0.01), at=(0.0, 0.0, 0.1))
    stick(m, "Black", (-0.08, 0.0, 0.14), (0.08, 0.0, 0.14), 0.008, 0.008, sides=6, steps=2)
    return m


# --- Firearms ----------------------------------------------------------------------------------------------------

def _from_boat_mesh(src, rename, scale=0.01, roll=math.pi / 2.0):
    """A riptide_boat_mesh.Mesh (centimetres) as an item mesh in metres, laid on its side."""
    sub = Mesh()
    for p, n, uv in src.verts:
        sub.verts.append((p, n, uv))
    for material, faces in src.faces.items():
        for a, b, d in faces:
            sub.tri(rename.get(material, material), a, b, d)
    m = Mesh()
    place(m, sub, roll=roll, scale=scale)
    return grounded(m)


def build_m4():
    import riptide_crew_mesh
    return _from_boat_mesh(riptide_crew_mesh.build_rifle(), {"GunMetal": "GunMetal", "Furniture": "Polymer", "Lens": "Glass"})


def _gun(parts):
    """A firearm from boxes and barrels laid out along x in its own upright frame, then laid on its side."""
    up = Mesh()
    for kind, args in parts:
        if kind == "box":
            material, size, at = args
            box(up, material, size, at=at)
        else:
            material, x0, x1, radius, z = args
            cylinder_x(up, material, x0, x1, radius, z=z)
    m = Mesh()
    place(m, up, roll=math.pi / 2.0)
    return grounded(m)


def build_m1911():
    return _gun([("box", ("GunMetal", (0.2, 0.026, 0.035), (0.03, 0.0, 0.07))),
                 ("box", ("Wood", (0.045, 0.03, 0.09), (-0.045, 0.0, -0.02))),
                 ("box", ("GunMetal", (0.04, 0.008, 0.02), (0.0, 0.0, 0.05)))])


def build_uzi():
    return _gun([("box", ("GunMetal", (0.27, 0.04, 0.055), (0.0, 0.0, 0.05))),
                 ("box", ("Polymer", (0.04, 0.035, 0.12), (-0.02, 0.0, -0.07))),
                 ("box", ("GunMetal", (0.025, 0.02, 0.14), (0.01, 0.0, -0.09))),
                 ("cyl", ("GunMetal", 0.135, 0.18, 0.009, 0.075))])


def build_mossberg():
    return _gun([("cyl", ("GunMetal", 0.0, 0.6, 0.012, 0.06)), ("cyl", ("GunMetal", 0.0, 0.5, 0.011, 0.035)),
                 ("box", ("GunMetal", (0.2, 0.035, 0.06), (-0.08, 0.0, 0.03))),
                 ("box", ("Polymer", (0.18, 0.045, 0.04), (0.28, 0.0, 0.025))),
                 ("box", ("Polymer", (0.36, 0.035, 0.09), (-0.36, 0.0, 0.01)))])


def build_intervention():
    return _gun([("cyl", ("GunMetal", 0.05, 0.75, 0.011, 0.07)), ("cyl", ("GunMetal", 0.75, 0.8, 0.016, 0.07)),
                 ("box", ("Polymer", (0.42, 0.05, 0.07), (0.0, 0.0, 0.03))),
                 ("box", ("Polymer", (0.36, 0.045, 0.11), (-0.36, 0.0, 0.0))),
                 ("cyl", ("GunMetal", -0.15, 0.2, 0.02, 0.13)), ("box", ("GunMetal", (0.06, 0.02, 0.03), (0.02, 0.0, 0.1)))])


def build_flare_gun():
    return _gun([("cyl", ("Orange", 0.0, 0.16, 0.017, 0.07)),
                 ("box", ("Orange", (0.05, 0.03, 0.11), (-0.03, 0.0, -0.03)))])


def build_outboard_motor():
    import riptide_boat_mesh
    rename = {"Cowling": "Black", "Trim": "Polymer", "Prop": "Steel", "Frame": "Steel", "White": "White", "Aluminium": "Steel",
              "Console": "Polymer", "Red": "Red", "Safety": "Orange", "Lamp": "Glass", "Glass": "Glass", "Deck": "Polymer",
              "HullInside": "Steel", "Collar": "Black", "Cushion": "Black", "Canopy": "Polymer"}
    # The boat's are 300 hp motors over 2 m tall: the one you carry is a small dinghy outboard, about 1.1 m.
    return _from_boat_mesh(riptide_boat_mesh.build_outboard(), rename, scale=0.0055, roll=math.pi / 2.0)


def build_fuel_drum():
    m = Mesh()
    lathe(m, "Red", [(0.0, 0.0), (0.12, 0.0), (0.125, 0.01), (0.125, 0.33), (0.12, 0.34), (0.0, 0.34)], sides=20)
    for z in (0.11, 0.23):
        ring(m, "Red", (0.0, 0.0, z), 0.127, 0.004, sides=4, segments=24)
    blob(m, "Black", (0.07, 0.0, 0.335), (0.02, 0.02), sides=8, bumps=0.0)
    return m


# --- Gun attachments -------------------------------------------------------------------------------------------

def _scope(length, radius, front=None, rear=None, mount=True):
    m = Mesh()
    cylinder_x(m, "GunMetal", -length / 2.0, length / 2.0, radius, z=radius + 0.012)
    if front:
        cylinder_x(m, "GunMetal", length / 2.0 - 0.001, length / 2.0 + front[0], front[1], z=radius + 0.012, radius_end=front[1])
        cylinder_x(m, "Glass", length / 2.0 + front[0], length / 2.0 + front[0] + 0.001, front[1] * 0.9, z=radius + 0.012)
    if rear:
        cylinder_x(m, "Rubber", -length / 2.0 - rear[0], -length / 2.0, rear[1], z=radius + 0.012)
    if mount:
        for x in (-length * 0.25, length * 0.25):
            box(m, "GunMetal", (0.015, radius * 1.6, 0.014), at=(x, 0.0, 0.0))
    return m


def build_red_dot():
    m = Mesh()
    box(m, "GunMetal", (0.05, 0.03, 0.01))
    m2 = _scope(0.04, 0.014, mount=False)
    place(m, m2, at=(0.0, 0.0, 0.0))
    return m


def build_holo_sight():
    m = Mesh()
    box(m, "GunMetal", (0.09, 0.04, 0.015))
    box(m, "GunMetal", (0.01, 0.04, 0.05), at=(0.04, 0.0, 0.015))
    box(m, "GunMetal", (0.01, 0.04, 0.05), at=(-0.04, 0.0, 0.015))
    box(m, "Glass", (0.003, 0.034, 0.04), at=(0.04, 0.0, 0.018))
    return m


def build_prism_3x():
    m = Mesh()
    box(m, "GunMetal", (0.08, 0.045, 0.045))
    cylinder_x(m, "GunMetal", 0.04, 0.06, 0.018, z=0.025)
    cylinder_x(m, "Rubber", -0.06, -0.04, 0.016, z=0.025)
    return m


def build_lpvo_6x():
    return _scope(0.22, 0.015, front=(0.03, 0.019), rear=(0.04, 0.02))


def build_sniper_scope():
    return _scope(0.3, 0.017, front=(0.06, 0.028), rear=(0.05, 0.022))


def build_compensator():
    m = Mesh()
    cylinder_x(m, "GunMetal", -0.03, 0.03, 0.012)
    return m


def build_muzzle_brake():
    m = Mesh()
    cylinder_x(m, "GunMetal", -0.03, 0.03, 0.013)
    for x in (-0.012, 0.0, 0.012):
        box(m, "Black", (0.004, 0.028, 0.008), at=(x, 0.0, 0.009))
    return m


def build_suppressor():
    m = Mesh()
    cylinder_x(m, "GunMetal", -0.09, 0.09, 0.018)
    return m


def build_vertical_grip():
    m = Mesh()
    box(m, "Polymer", (0.06, 0.02, 0.012))
    stick(m, "Polymer", (0.0, 0.0, 0.012), (0.0, 0.0, 0.1), 0.015, 0.013, sides=10, steps=2)
    return grounded(m)


def build_angled_grip():
    m = Mesh()
    box(m, "Polymer", (0.09, 0.02, 0.012))
    ids_box = Mesh()
    box(ids_box, "Polymer", (0.08, 0.022, 0.04))
    place(m, ids_box, at=(0.0, 0.0, 0.012), pitch=-0.5)
    return grounded(m)


def build_bipod():
    m = Mesh()
    box(m, "GunMetal", (0.04, 0.03, 0.02))
    for y in (-0.01, 0.01):
        stick(m, "GunMetal", (0.02, y, 0.01), (0.22, y * 3.0, 0.01), 0.006, 0.005, sides=6, steps=2)
    return m


def _mag(length, curve=0.0, extra=0.0):
    m = Mesh()
    up = Mesh()
    box(up, "Polymer", (0.035, 0.022, length + extra))
    if extra:
        box(up, "Polymer", (0.04, 0.026, 0.02), at=(0.0, 0.0, length + extra - 0.02))
    place(m, up, roll=math.pi / 2.0, yaw=curve)
    return grounded(m)


def build_extended_mag():
    return _mag(0.18, extra=0.06)


def build_quickdraw_mag():
    m = _mag(0.16)
    ring(m, "Red", (0.0, 0.0, 0.025), 0.012, 0.003, axis=(0.0, 1.0, 0.0), sides=4, segments=10)
    return m


def _stock(length, depth, material="Polymer", folding=False):
    m = Mesh()
    up = Mesh()
    box(up, material, (length, 0.04, depth * 0.35), at=(0.0, 0.0, depth * 0.65))
    box(up, material, (0.04, 0.04, depth), at=(-length / 2.0, 0.0, 0.0))
    if folding:
        stick(up, "GunMetal", (length / 2.0, 0.0, depth * 0.8), (-length / 2.0, 0.0, depth * 0.1), 0.006, 0.006, sides=6, steps=2)
    else:
        box(up, material, (length, 0.04, depth * 0.18), at=(0.0, 0.0, 0.0))
    place(m, up, roll=math.pi / 2.0)
    return grounded(m)


def build_light_stock():
    return _stock(0.2, 0.1)


def build_heavy_stock():
    return _stock(0.28, 0.14, material="Wood")


def build_folding_stock():
    return _stock(0.25, 0.11, material="GunMetal", folding=True)


def build_laser():
    m = Mesh()
    box(m, "Polymer", (0.06, 0.03, 0.022))
    cylinder_x(m, "Red", 0.03, 0.034, 0.005, z=0.011)
    return m


def build_flashlight():
    m = Mesh()
    cylinder_x(m, "GunMetal", -0.07, 0.05, 0.013)
    cylinder_x(m, "GunMetal", 0.05, 0.08, 0.018, z=0.018, radius_end=0.018)
    cylinder_x(m, "Glass", 0.08, 0.081, 0.016, z=0.018)
    return grounded(m)


# --- Medical, prosthetics ----------------------------------------------------------------------------------------

def build_bandage():
    m = Mesh()
    lathe(m, "White", [(0.0, 0.0), (0.03, 0.0), (0.03, 0.05), (0.0, 0.05)], sides=16)
    slab(m, "White", 0.08, 0.05, 0.002, at=(0.06, 0.0, 0.0), round_ends=0.05)
    return m


def build_first_aid_kit():
    m = Mesh()
    box(m, "Red", (0.24, 0.16, 0.08))
    box(m, "White", (0.09, 0.025, 0.002), at=(0.0, 0.0, 0.08))
    box(m, "White", (0.025, 0.09, 0.002), at=(0.0, 0.0, 0.08))
    return m


def build_peg_leg():
    m = Mesh()
    lathe(m, "Leather", [(0.0, 0.0), (0.065, 0.0), (0.07, 0.12), (0.0, 0.12)], sides=14)
    up = Mesh()
    lathe(up, "Wood", [(0.0, 0.0), (0.025, 0.0), (0.035, 0.3), (0.06, 0.4), (0.0, 0.4)], sides=12)
    out = Mesh()
    place(out, up, pitch=math.pi / 2.0)
    place(out, m, at=(0.4, 0.0, 0.0), pitch=math.pi / 2.0)
    return grounded(out)


def build_hook_hand():
    up = Mesh()
    lathe(up, "Leather", [(0.0, 0.0), (0.045, 0.0), (0.05, 0.12), (0.03, 0.14), (0.0, 0.14)], sides=12)
    # The hook: up out of the cuff, then curling over.
    pts = [(0.0, 0.0, 0.14), (0.0, 0.0, 0.2)]
    for k in range(1, 9):
        a = math.pi * k / 8.0
        pts.append((0.035 * (1.0 - math.cos(a)), 0.0, 0.2 + 0.04 * math.sin(a)))
    up.tube("Steel", pts, [0.006] * len(pts), 6, v_per_metre=10.0)
    m = Mesh()
    place(m, up, pitch=math.pi / 2.0)
    return grounded(m)


# --- Paper, keys -----------------------------------------------------------------------------------------------

def _book(colour, pages=0.03):
    m = Mesh()
    box(m, colour, (0.2, 0.14, 0.004))
    box(m, "Paper", (0.19, 0.132, pages), at=(0.002, 0.0, 0.004))
    box(m, colour, (0.2, 0.14, 0.004), at=(0.0, 0.0, 0.004 + pages))
    box(m, colour, (0.2, 0.006, pages + 0.008), at=(0.0, -0.07, 0.0))
    return m


def build_survival_book():
    return _book("Olive")


def build_logbook():
    return _book("Leather", 0.02)


def build_journal():
    m = _book("ClothRed", 0.018)
    stick(m, "Black", (0.0, 0.0, 0.03), (0.0, 0.08, 0.03), 0.002, 0.002, sides=4, steps=2)
    return m


def _page(seed):
    m = Mesh()
    slab(m, "Paper", 0.2, 0.14, 0.001, round_ends=0.02, yaw=0.1 * (_hash(seed, 1, 7) - 0.5))
    return m


def build_sea_chart():
    m = Mesh()
    up = Mesh()
    lathe(up, "Paper", [(0.0, 0.0), (0.022, 0.0), (0.022, 0.5), (0.0, 0.5)], sides=14)
    place(m, up, at=(-0.25, 0.0, 0.022), pitch=math.pi / 2.0)
    for x in (-0.12, 0.12):
        ring(m, "Rope", (x, 0.0, 0.022), 0.023, 0.002, axis=(1.0, 0.0, 0.0), sides=4, segments=12)
    return m


def build_compartment_key():
    m = Mesh()
    ring(m, "Brass", (-0.02, 0.0, 0.003), 0.012, 0.003, sides=5, segments=14)
    box(m, "Brass", (0.05, 0.006, 0.004), at=(0.015, 0.0, 0.001))
    box(m, "Brass", (0.006, 0.012, 0.004), at=(0.035, 0.006, 0.001))
    return m


# --- Build kits ------------------------------------------------------------------------------------------------

def _bundle(material, count, length, radius, seed, canvas=None):
    m = Mesh()
    rng = random.Random(seed)
    for k in range(count):
        a = 2.0 * math.pi * k / count
        y, z = math.cos(a) * radius * 2.2, radius * 2.0 + math.sin(a) * radius * 2.0
        stick(m, material, (-length / 2.0, y, z), (length / 2.0, y + rng.uniform(-0.01, 0.01), z), radius, radius * 0.9, sides=7,
              wobble=0.01, seed=seed + k, steps=4)
    for x in (-length * 0.3, length * 0.3):
        ring(m, "Rope", (x, 0.0, radius * 2.0), radius * 3.4, 0.006, axis=(1.0, 0.0, 0.0), sides=5, segments=14)
    if canvas:
        slab(m, canvas, length * 0.5, radius * 6.0, 0.03, at=(0.0, 0.0, radius * 4.0), round_ends=0.2)
    return m


def build_lean_to_kit():
    return _bundle("Wood", 6, 0.9, 0.02, 601)


def build_tent_kit():
    return _bundle("Wood", 4, 0.8, 0.016, 611, canvas="Canvas")


def build_drying_rack_kit():
    return _bundle("Wood", 5, 0.7, 0.015, 621)


def build_storage_crate_kit():
    m = Mesh()
    for k in range(4):
        box(m, "Wood", (0.6, 0.12, 0.02), at=(0.0, 0.0, k * 0.02), yaw=0.05 * (k - 1.5))
    for x in (-0.2, 0.2):
        ring(m, "Rope", (x, 0.0, 0.04), 0.07, 0.005, axis=(1.0, 0.0, 0.0), sides=5, segments=12)
    return m


def build_compost_bin_kit():
    return _bundle("Bark", 5, 0.6, 0.022, 631)


def build_sandbag():
    m = Mesh()
    merge(m, _sandbag(641))
    return m


# --- Clothes (folded, the way they're picked up) and bags --------------------------------------------------------

def _folded(material, length, width, layers, collar=None, seed=1):
    m = Mesh()
    for k in range(layers):
        slab(m, material, length - k * 0.004, width - k * 0.003, 0.012, at=(0.0, 0.0, k * 0.012), round_ends=0.12)
    if collar:
        ring(m, collar, (length * 0.35, 0.0, layers * 0.012), width * 0.18, 0.006, sides=5, segments=14)
    return m


def build_tshirt():
    return _folded("Cloth", 0.26, 0.2, 3, collar="Cloth")


def build_rain_jacket():
    m = _folded("Yellow", 0.3, 0.24, 4, collar="Yellow")
    return m


def build_wool_sweater():
    return _folded("Wool", 0.3, 0.24, 5, collar="Wool")


def build_shorts():
    return _folded("Khaki", 0.24, 0.2, 3)


def build_cargo_pants():
    m = _folded("Olive", 0.34, 0.22, 4)
    box(m, "Olive", (0.08, 0.07, 0.012), at=(0.05, 0.05, 0.048))
    return m


def _shoe(material, sole, height, length=0.27, width=0.1):
    m = Mesh()
    slab(m, sole, length, width, 0.025, round_ends=0.7)
    up = Mesh()
    lathe(up, material, [(0.0, 0.0), (width * 0.48, 0.0), (width * 0.48, height * 0.6), (width * 0.35, height), (0.0, height)], sides=14,
          squash=(length / width * 0.5, 1.0))
    place(m, up, at=(-0.02, 0.0, 0.025), scale=1.0)
    return m


def build_sandals():
    m = Mesh()
    for y in (-0.06, 0.06):
        slab(m, "Rubber", 0.26, 0.095, 0.018, at=(0.0, y, 0.0), round_ends=0.7)
        for x in (0.05, -0.04):
            ring(m, "Leather", (x, y, 0.018), 0.035, 0.006, axis=(1.0, 0.0, 0.0), sides=4, segments=8)
    return m


def build_hiking_boots():
    m = Mesh()
    for y in (-0.07, 0.07):
        place(m, _shoe("Leather", "Rubber", 0.16), at=(0.0, y, 0.0))
    return m


def build_wool_beanie():
    m = Mesh()
    profile = [(0.0, 0.0), (0.09, 0.0), (0.095, 0.03), (0.085, 0.09), (0.05, 0.14), (0.0, 0.155)]
    lathe(m, "Wool", profile, sides=18, squash=(1.0, 0.45), bumps=0.03, seed=651)
    return m


def build_sun_hat():
    m = Mesh()
    lathe(m, "Khaki", [(0.0, 0.0), (0.2, 0.0), (0.2, 0.005), (0.1, 0.012), (0.095, 0.08), (0.0, 0.09)], sides=22)
    ring(m, "Leather", (0.0, 0.0, 0.02), 0.098, 0.006, sides=4, segments=20)
    return m


def build_combat_helmet():
    m = Mesh()
    profile = [(0.0, 0.0)]
    for k in range(1, 9):
        a = (math.pi / 2.0) * k / 8.0
        profile.append((0.13 * math.cos(a) + 0.002, 0.15 * math.sin(a)))
    profile[0] = (0.13, 0.0)
    profile = [(0.0, 0.0)] + profile + [(0.0, 0.152)]
    lathe(m, "Olive", profile, sides=20, squash=(1.1, 0.95))
    return m


def build_plate_carrier():
    m = Mesh()
    slab(m, "Olive", 0.34, 0.3, 0.035, round_ends=0.15)
    for k in range(3):
        box(m, "Olive", (0.07, 0.04, 0.05), at=(-0.1 + k * 0.1, -0.08, 0.035))
    for x in (-0.1, 0.1):
        box(m, "Olive", (0.04, 0.14, 0.012), at=(x, 0.17, 0.0))
    return m


def _bag(material, size, flap=True):
    m = Mesh()
    sx, sy, sz = size
    up = Mesh()
    lathe(up, material, [(0.0, 0.0), (sx * 0.5, 0.0), (sx * 0.55, sz * 0.4), (sx * 0.5, sz * 0.85), (sx * 0.3, sz), (0.0, sz)], sides=14,
          squash=(1.0, sy / sx), bumps=0.03, seed=661)
    merge(m, up)
    if flap:
        slab(m, material, sx * 0.6, sy * 0.5, 0.01, at=(0.0, -sy * 0.25, sz * 0.75), round_ends=0.4)
    return m


def build_daypack():
    m = _bag("Olive", (0.3, 0.18, 0.42))
    for x in (-0.07, 0.07):                       # the shoulder straps down its back
        stick(m, "Black", (x, 0.09, 0.35), (x, 0.09, 0.06), 0.012, 0.012, sides=4, steps=3)
    return m


def build_satchel():
    m = _bag("Leather", (0.3, 0.1, 0.22))
    stick(m, "Leather", (-0.15, 0.0, 0.2), (0.15, 0.0, 0.2), 0.006, 0.006, sides=4, steps=2)
    return m


def build_loot_bag():
    m = Mesh()
    lathe(m, "Canvas", [(0.0, 0.0), (0.14, 0.0), (0.17, 0.08), (0.15, 0.2), (0.07, 0.27), (0.03, 0.3), (0.05, 0.33), (0.0, 0.34)], sides=14,
          bumps=0.06, seed=671)
    ring(m, "Rope", (0.0, 0.0, 0.29), 0.035, 0.006, sides=5, segments=12)
    return m


# --- The registry ----------------------------------------------------------------------------------------------

def _on_ground(build):
    return lambda: grounded(build())


CATALOG = {
    "berries": build_berries, "red_berries": build_red_berries, "dried_berries": build_dried_berries,
    "raw_fish": _fish_builder("fish", "raw"), "cooked_fish": _fish_builder("fish", "cooked"), "dried_fish": _fish_builder("fish", "dried"),
    "raw_sardine": _fish_builder("sardine", "raw"), "raw_mullet": _fish_builder("mullet", "raw"),
    "raw_pufferfish": _fish_builder("pufferfish", "raw"), "cooked_pufferfish": _fish_builder("pufferfish", "cooked"),
    "raw_snapper": _fish_builder("snapper", "raw"), "raw_grouper": _fish_builder("grouper", "raw"),
    "raw_barracuda": _fish_builder("barracuda", "raw"), "raw_mahi_mahi": _fish_builder("mahi_mahi", "raw"),
    "raw_tuna": _fish_builder("tuna", "raw"), "fish_steak": build_fish_steak,
    "raw_meat": build_raw_meat, "cooked_meat": build_cooked_meat, "dried_meat": build_dried_meat,
    "raw_shark_meat": build_raw_shark_meat, "cooked_shark": build_cooked_shark, "spoiled_food": build_spoiled_food,
    "ration_pack": build_ration_pack, "grub": build_grub, "cut_bait": build_cut_bait,
    "soil": build_soil, "sand": build_sand, "tarp": build_tarp, "paracord": build_paracord, "lure": build_lure, "jig": build_jig,
    "arrow": build_arrow, "flare": build_flare,
    "ammo_45": _ammo_builder("ammo_45"), "ammo_9mm": _ammo_builder("ammo_9mm"), "ammo_556": _ammo_builder("ammo_556"),
    "ammo_12ga": _ammo_builder("ammo_12ga"), "ammo_408": _ammo_builder("ammo_408"),
    "dagger": build_dagger, "machete": build_machete, "fishing_rod": build_fishing_rod, "cleaning_kit": build_cleaning_kit,
    "binoculars": build_binoculars, "handheld_radio": build_handheld_radio, "tool_kit": build_tool_kit, "spear": build_spear,
    "bow": build_bow, "flare_gun": build_flare_gun, "m1911": build_m1911, "uzi": build_uzi, "m4": build_m4,
    "mossberg": build_mossberg, "intervention": build_intervention, "outboard_motor": build_outboard_motor,
    "fuel_drum": build_fuel_drum,
    "red_dot": build_red_dot, "holo_sight": build_holo_sight, "prism_3x": build_prism_3x, "lpvo_6x": build_lpvo_6x,
    "sniper_scope": build_sniper_scope, "compensator": build_compensator, "muzzle_brake": build_muzzle_brake,
    "suppressor": build_suppressor, "vertical_grip": build_vertical_grip, "angled_grip": build_angled_grip,
    "bipod": build_bipod, "extended_mag": build_extended_mag, "quickdraw_mag": build_quickdraw_mag,
    "light_stock": build_light_stock, "heavy_stock": build_heavy_stock, "folding_stock": build_folding_stock,
    "laser": build_laser, "flashlight": build_flashlight,
    "bandage": build_bandage, "first_aid_kit": build_first_aid_kit, "peg_leg": build_peg_leg, "hook_hand": build_hook_hand,
    "survival_book": build_survival_book, "book_page_shelter": lambda: _page(1), "book_page_camp": lambda: _page(2),
    "book_page_prosthetics": lambda: _page(3), "logbook": build_logbook, "journal": build_journal, "sea_chart": build_sea_chart,
    "compartment_key": build_compartment_key,
    "lean_to_kit": build_lean_to_kit, "tent_kit": build_tent_kit, "drying_rack_kit": build_drying_rack_kit,
    "storage_crate_kit": build_storage_crate_kit, "compost_bin_kit": build_compost_bin_kit, "sandbag": build_sandbag,
    "tshirt": build_tshirt, "rain_jacket": build_rain_jacket, "wool_sweater": build_wool_sweater, "shorts": build_shorts,
    "cargo_pants": build_cargo_pants, "sandals": build_sandals, "hiking_boots": build_hiking_boots,
    "wool_beanie": build_wool_beanie, "sun_hat": build_sun_hat, "combat_helmet": build_combat_helmet,
    "plate_carrier": build_plate_carrier, "daypack": build_daypack, "satchel": build_satchel, "loot_bag": build_loot_bag,
}

# Every model set down on the ground (sticks bend and bundles roll a little below z = 0 as they're built).
CATALOG = {item: _on_ground(build) for item, build in CATALOG.items()}
