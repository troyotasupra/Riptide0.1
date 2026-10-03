"""Draws a top-down picture of a designed island and checks its shape, outside the engine.

    python Tools/island_preview.py [StartCay] [output folder]

Writes <island>_plan.png (ground types, shaded by slope, a contour line every metre of height, the 10 m grid) and
<island>_section.png (a cut from the sea up the wash-up beach to the summit, 1 m squares), and prints the checks:
the crew can walk from the beach to the summit and over nearly all the land, a boat can reach the beach, and
nothing above the waterline is too steep to stand on except rock.
"""
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Python"))
import riptide_island_shape as shape  # noqa: E402

COLOURS = {
    "deep": (22, 66, 104), "shallows": (88, 196, 196), "reef": (60, 140, 128), "wet_sand": (205, 188, 140),
    "sand": (236, 220, 172), "rock": (140, 134, 122), "grove": (84, 140, 72),
}


def write_png(path, width, height, pixels):
    """pixels: a list of rows, each a list of (r, g, b)."""
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def plan(island, path, half=200, scale=3):
    n = 2 * half + 1
    heights = [[island.height(ix - half, half - iy) for ix in range(n)] for iy in range(n)]
    rows = []
    for iy in range(n):
        row = []
        for ix in range(n):
            h = heights[iy][ix]
            hx = (heights[iy][min(n - 1, ix + 1)] - heights[iy][max(0, ix - 1)]) / 2.0
            hy = (heights[max(0, iy - 1)][ix] - heights[min(n - 1, iy + 1)][ix]) / 2.0
            slope = math.degrees(math.atan(math.hypot(hx, hy)))
            kind = island.ground(ix - half, half - iy, h, slope)
            r, g, b = COLOURS[kind]
            if h < -0.25:
                # Water darkens with depth, from the wadeable shallows to the drop-off.
                k = 1.0 - shape.smoothstep(0.0, 12.0, -h) * 0.75
                r, g, b = r * k + 22 * (1 - k), g * k + 66 * (1 - k), b * k + 104 * (1 - k)
            # Lit from the north-west, so slopes read.
            shade = max(0.55, min(1.25, 1.0 + (-hx * 0.6 + hy * 0.6) * 1.4))
            if h > -0.25:
                r, g, b = r * shade, g * shade, b * shade
            # A contour every metre above the sea (darker every 5 m), and at the waterline.
            right, down = heights[iy][min(n - 1, ix + 1)], heights[min(n - 1, iy + 1)][ix]
            for other in (right, down):
                if math.floor(h) != math.floor(other) and max(h, other) > -0.01:
                    level = max(math.floor(h), math.floor(other))
                    k = 0.45 if level % 5 == 0 or level == 0 else 0.75
                    r, g, b = r * k, g * k, b * k
                    break
            if (ix - half) % 10 == 0 or (half - iy) % 10 == 0:
                k = 0.86 if ((ix - half) % 50 == 0 or (half - iy) % 50 == 0) else 0.95
                r, g, b = r * k, g * k, b * k
            row.append((int(max(0, min(255, r))), int(max(0, min(255, g))), int(max(0, min(255, b)))))
        rows.append(row)
    big = []
    for row in rows:
        wide = [px for px in row for _ in range(scale)]
        big.extend([wide] * scale)
    write_png(path, n * scale, n * scale, big)


def section(island, path, a, b, scale=8, up=12, down=8):
    """A side-on cut along the line a -> b, one pixel per 1/scale metre, with 1 m squares."""
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    w, hgt = int(length * scale), (up + down) * scale
    cols = []
    for px in range(w):
        t = px / w
        cols.append(island.height(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    rows = []
    for py in range(hgt):
        z = up - py / scale
        row = []
        for px in range(w):
            ground = cols[px]
            if z <= ground:
                c = (196, 178, 132) if ground - z < 0.3 else (150, 136, 104)
            elif z <= 0.0:
                c = (96, 190, 200)
            else:
                c = (226, 236, 244)
            if px % scale == 0 or py % scale == 0:
                c = tuple(int(v * (0.8 if (px % (10 * scale) == 0 or abs(z) < 1e-6) else 0.93)) for v in c)
            row.append(c)
        rows.append(row)
    write_png(path, w, hgt, rows)


def checks(island):
    ok = True

    def report(passed, text):
        nonlocal ok
        ok &= passed
        print(("PASS  " if passed else "FAIL  ") + text)

    xs = [island.cx[i] for i in range(island.cn)]
    ys = [island.cy[i] for i in range(island.cn)]
    print(f"{island.name}: {max(xs) - min(xs):.0f} m east-west, {max(ys) - min(ys):.0f} m north-south")
    top = island.height(*island.summit)
    best = max(((island.height(island.summit[0] + ox, island.summit[1] + oy), ox, oy)
                for ox in range(-12, 13) for oy in range(-12, 13)))
    print(f"summit {best[0]:.1f} m (at the knoll's centre {top:.1f} m)")

    reached, land = shape.walk_check(island, island.beach)
    limit, step = 200.0, 1.0
    summit_cell = (int(round((island.summit[0] + best[1] + limit) / step)), int(round((island.summit[1] + best[2] + limit) / step)))
    report(summit_cell in reached, "the crew can walk from the wash-up beach to the summit")
    walked = len(reached & land) / max(1, len(land))
    report(walked > 0.97, f"the crew can walk over {walked * 100:.1f}% of the land (area {len(land)} m2)")

    # Coming in to the beach by boat: how close the bow gets before the keel touches (0.6 m of water), and how
    # deep the crew wades from there.
    bx, by = island.beach
    ox, oy = island.beach_out
    length = math.hypot(ox - bx, oy - by)
    ground_at = None
    for i in range(int(length * 4), -1, -1):
        t = i / (length * 4)
        x, y = ox + (bx - ox) * t, oy + (by - oy) * t
        if island.height(x, y) > -0.6:
            ground_at = (x, y)
            break
    wade = abs(island.coast_distance(*ground_at)) if ground_at else 999.0
    report(ground_at is not None and wade < 15.0, f"a boat drawing 0.6 m grounds {wade:.1f} m off the beach's waterline")
    steepest = max(island.slope_deg(ox + (bx - ox) * i / 60.0, oy + (by - oy) * i / 60.0) for i in range(40, 61))
    report(steepest < 15.0, f"the beach and its shallows are gentle (steepest {steepest:.1f} degrees on the way in)")

    # Nothing that isn't rock is too steep to stand on.
    bad = 0
    for ix, iy in land:
        x, y = ix * step - limit, iy * step - limit
        s = island.slope_deg(x, y)
        if s > shape.WALKABLE_SLOPE and island.ground(x, y, None, s) != "rock":
            bad += 1
    report(bad == 0, f"only rock is too steep to walk ({bad} m2 of other ground is)")
    edge = max(abs(island.height(sx * island.extent, t * island.extent) - island.deep)
               for sx in (-1, 1) for t in (-1.0, -0.5, 0.0, 0.5, 1.0))
    report(edge < 0.5, "the seabed reaches its deep level all round the patch's edge")

    # The sea cliffs: every edge of each cliff skin is underground, and no part of a face is turned inland.
    import riptide_island_cliff as cliff
    meshes, edges, folds = cliff.build_cliffs(island)
    checked, exposed, shallowest = cliff.check_sealed(island, edges)
    report(exposed == 0, f"the {len(meshes)} cliffs are sealed into the ground ({checked} edge points, shallowest {shallowest:.2f} m under)")
    report(folds == 0, f"no cliff face is turned inside out ({folds} folded quads)")
    return ok


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "StartCay"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "IslandPreview")
    os.makedirs(out, exist_ok=True)
    island = getattr(shape, name)()
    good = checks(island)
    plan(island, os.path.join(out, name + "_plan.png"))
    section(island, os.path.join(out, name + "_section.png"), island.beach_out, island.summit)
    print("pictures in", os.path.abspath(out))
    sys.exit(0 if good else 1)
