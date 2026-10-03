"""The sea cliffs of Riptide's islands, built in code as part of the ground. Plain Python, no Unreal.

A cliff here is not a rock model stood in front of the land. It is one continuous skin of rock that runs along a
rock shore: its foot is buried in the seabed, its face rises out of the water, its top turns inland as a cap of
rock and runs under the ground a few metres back. Both of its edges are underground along its whole length, and
its ends sink into the beach where the shore turns to sand, so there is nowhere a gap can open: no water behind
it, no hollow back to see. check_sealed() verifies that for every edge point, and check_folds() that no part of the
skin is turned inside out (a quad facing inland is a hole from the sea, since Unreal only draws the front).

The face is shaped like weathered limestone: level beds that stand out or sit back as ledges, a notch undercut at
the waterline where the sea eats it, vertical fractures, and rubble spreading at its foot. Its height is whatever
the ground behind it is: a low shelf along an open shore, taller where a bluff comes down to the sea.

Axes and units: built in the island's design frame (metres, x east, y north, z up) and written in Unreal's
(X north, Y east) by write_cliffs. Material: IslandCliff, laid on by position; a vertex's u is how deep it sits
in a joint or under a ledge (0..1), which the material darkens.
"""
import math

from riptide_island_shape import fbm, lerp, noise, ridged, smoothstep
from riptide_palm_mesh import Mesh

COLUMN = 0.3            # metres between columns along the shore
FACE_ROWS = 30          # rows up the face, foot to lip
MAX_HEIGHT = 7.5        # the tallest a cliff grows, metres
CAP = (1.0, 2.3, 3.8)   # how far inland the cap's rows lie; the last is underground
BURY = 0.35             # how far under the ground the buried edges lie, metres


def _columns(island):
    """Points along the whole coast every COLUMN metres: (x, y, out_x, out_y, rockiness, distance along)."""
    cx, cy, cn = island.cx, island.cy, island.cn
    cols = []
    along = 0.0
    carry = 0.0
    for i in range(cn):
        j = (i + 1) % cn
        seg = math.hypot(cx[j] - cx[i], cy[j] - cy[i])
        # The shore's direction, taken over a few metres so the columns don't fan or cross at small kinks.
        tx, ty = cx[(i + 4) % cn] - cx[i - 4], cy[(i + 4) % cn] - cy[i - 4]
        length = math.hypot(tx, ty) or 1.0
        # The coast runs clockwise, so the sea is on its left.
        ox, oy = -ty / length, tx / length
        t = carry
        while t < seg:
            f = t / seg
            cols.append((cx[i] + (cx[j] - cx[i]) * f, cy[i] + (cy[j] - cy[i]) * f, ox, oy,
                         lerp(island.crock[i], island.crock[j], f), along + t))
            t += COLUMN
        carry = t - seg
        along += seg
    return cols


def _hash(a, b, seed):
    n = (int(a) * 374761393 + int(b) * 668265263 + seed * 1274126177) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1103515245) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def _face_offset(s, z, seed):
    """How far the face stands out to sea from the coast line at a point `s` metres along the shore and `z` metres
    above sea level. Returns (offset, recess): recess 0..1 is how deep in a joint or under a ledge the point is."""
    # Beds: level layers of rock, each 0.5-1 m thick. Along the shore each bed is broken into blocks a few metres
    # long by vertical joints; every block stands out or sits back on its own, so the face is stepped into ledges
    # and buttresses with hard edges, the way bedded limestone breaks.
    warped = z + 0.18 * math.sin(s / 8.0) + 0.12 * math.sin(s / 2.9 + 1.7)
    bed = math.floor(warped / 0.72)
    in_bed = warped / 0.72 - bed
    shift = _hash(bed, 3, seed) * 7.0
    length = 2.2 + 3.6 * _hash(bed, 11, seed)
    block = math.floor((s + shift) / length)
    in_block = (s + shift) / length - block
    stand = _hash(bed, block, seed)
    # Next to a joint the block's stand eases toward its neighbour's over 0.25 m, so the step between blocks is a
    # broken edge rather than a sawtooth of full-height teeth every few metres.
    edge_m = min(in_block, 1.0 - in_block) * length
    if edge_m < 0.25:
        beside = _hash(bed, block - 1 if in_block < 0.5 else block + 1, seed)
        stand = lerp((stand + beside) * 0.5, stand, edge_m / 0.25)
    # A whole stretch of cliff leans out or back over tens of metres, and buttresses run the cliff's full height.
    lean = fbm(s / 23.0, z / 9.0, seed + 9) * 0.55
    buttress = (noise(s / 6.5, 0.0, seed + 3) - 0.5) * 0.9
    out = 1.25 + (stand - 0.5) * 0.75 + lean + buttress
    # Joints between blocks and bedding planes between beds are cut back into the face.
    joint = min(in_block, 1.0 - in_block) * length
    plane = min(in_bed, 1.0 - in_bed) * 0.72
    cut = max(1.0 - joint / 0.16, 0.0) * 0.34 + max(1.0 - plane / 0.07, 0.0) * 0.14
    chips = (noise(s / 0.55, z / 0.4, seed + 21) - 0.5) * 0.12
    # Solution pockets: hollows 0.5-1.5 m across eaten back into the face here and there.
    pocket = -0.3 * max(0.0, noise(s / 1.4, z / 1.1, seed + 33) - 0.6) / 0.4
    notch = -0.4 * math.exp(-((z - 0.2) / 0.38) ** 2)            # the sea's undercut at the waterline
    rubble = 0.6 * max(0.0, -z - 0.8)                            # the foot spreads out under water
    offset = out - cut + chips + pocket + notch + rubble
    recess = min(1.0, cut * 2.4 + max(0.0, 0.5 - stand) * 0.5 + max(0.0, -notch) * 1.2 + max(0.0, -pocket) * 1.5)
    return max(0.22, offset), recess


def build_cliffs(island, seed=77):
    """The island's cliffs: (meshes, one per rock shore; the edge points check_sealed verifies; how many face quads
    are folded inland, which must be 0)."""
    cols = _columns(island)
    n = len(cols)
    strength = [smoothstep(0.45, 0.8, c[4]) for c in cols]
    # Start the walk on sand, so no run of rock straddles the list's ends.
    start = next((i for i in range(n) if strength[i] <= 0.0), 0)
    order = [(start + k) % n for k in range(n)]
    runs, run = [], []
    for i in order:
        if strength[i] > 0.0:
            run.append(i)
        elif run:
            runs.append(run)
            run = []
    if run:
        runs.append(run)

    meshes, edges, folds = [], [], 0
    for run in runs:
        if len(run) < 8:
            continue
        # The ground the cliff has to reach, a little way behind the shore, evened out along the run.
        raw = [min(island.height(cols[i][0] - cols[i][2] * 3.2, cols[i][1] - cols[i][3] * 3.2), MAX_HEIGHT) for i in run]
        tops = [sum(raw[max(0, k - 3):k + 4]) / len(raw[max(0, k - 3):k + 4]) for k in range(len(raw))]
        mesh = Mesh()
        grid = []
        for k, i in enumerate(run):
            x, y, ox, oy, _, s = cols[i]
            # Rock gives way to sand over the ends of the run: there the whole skin sinks under the ground.
            w = min(strength[i], smoothstep(0.0, 6.0, k * COLUMN), smoothstep(0.0, 6.0, (len(run) - 1 - k) * COLUMN))
            top = max(tops[k], 0.9)
            column = []

            def put(px, py, pz, row, recess, bury_edge=False):
                ground = island.height(px, py)
                if bury_edge:
                    pz = ground - BURY
                pz = lerp(ground - BURY - 0.25, pz, w)
                column.append(((px, py, pz), row, recess))
                return ground

            # The face, from its buried foot to its lip. The foot is under the seabed and always below the first
            # face row (near a run's ends the seabed is shallow and would otherwise put it above, folding the skin).
            foot, _ = _face_offset(s, -1.7, seed)
            put(x + ox * foot, y + oy * foot, min(island.height(x + ox * foot, y + oy * foot) - BURY, -2.1), 0, 0.6)
            # The top 1.2 m eases into a rounded, weathered lip that follows the cliff's lean and buttresses only,
            # and never juts out past the rock in the metre below it (that made flat visors over the sea).
            lip = 1.1 + fbm(s / 23.0, top / 9.0, seed + 9) * 0.55 + (noise(s / 6.5, 0.0, seed + 3) - 0.5) * 0.9
            below = max(_face_offset(s, top - 1.2 - 0.25 * i, seed)[0] for i in range(4))
            lip = min(lip, below + 0.15)
            for r in range(1, FACE_ROWS + 1):
                z = lerp(-1.7, top, (r - 1) / (FACE_ROWS - 1))
                f, recess = _face_offset(s, z, seed)
                shoulder = smoothstep(top - 1.2, top, z)
                f = lerp(f, lip - 0.35 * shoulder, shoulder)
                recess *= 1.0 - shoulder
                put(x + ox * f, y + oy * f, z, r, recess)
            # The cap: rock running inland over the ground, its last row under it.
            for c, back in enumerate(CAP):
                px, py = x - ox * back, y - oy * back
                ground = island.height(px, py)
                put(px, py, max(top, ground) + 0.06, FACE_ROWS + 1 + c, 0.0, bury_edge=(c == len(CAP) - 1))
            grid.append(column)

        rows = len(grid[0])
        index = [[0] * rows for _ in grid]
        for k, column in enumerate(grid):
            ox, oy = cols[run[k]][2], cols[run[k]][3]
            for r, (p, _, recess) in enumerate(column):
                a = grid[max(k - 1, 0)][r][0]
                b = grid[min(k + 1, len(grid) - 1)][r][0]
                c = column[max(r - 1, 0)][0]
                d = column[min(r + 1, rows - 1)][0]
                u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])     # along the shore
                v = (d[0] - c[0], d[1] - c[1], d[2] - c[2])     # up the face, then inland over the cap
                # v x u: the coast runs clockwise with the sea on its left, so this points to sea on the face, up on
                # the cap, and down under a ledge, all from the one consistent winding.
                nx, ny, nz = v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0]
                length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                # Unreal's X is north (the design's y), Y is east (the design's x).
                index[k][r] = mesh.vert((p[1], p[0], p[2]), (ny / length, nx / length, nz / length), (recess, r / 4.0))
        def inland(a, b, c, ox, oy):
            """Whether the triangle a, b, c (grid order) faces inland: a hole when seen from the sea."""
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            n = (v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0])
            size = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
            return (n[0] * ox + n[1] * oy) / size < -0.35

        for k in range(len(grid) - 1):
            ox, oy = cols[run[k]][2], cols[run[k]][3]
            for r in range(rows - 1):
                i00, i10, i11, i01 = index[k][r], index[k + 1][r], index[k + 1][r + 1], index[k][r + 1]
                if r >= FACE_ROWS:
                    mesh.quad("IslandCliff", i00, i10, i11, i01)
                    continue
                # Where a block steps out above a block that steps back, the quad across the corner is twisted and
                # one way of splitting it leaves a triangle facing inland; the other diagonal is used there.
                p00, p10, p11, p01 = grid[k][r][0], grid[k + 1][r][0], grid[k + 1][r + 1][0], grid[k][r + 1][0]
                # Over the run's ends the skin lies flat under the sand; whichever way those quads face is unseen.
                buried = all(island.height(p[0], p[1]) - p[2] > 0.1 for p in (p00, p10, p11, p01))
                if buried or not (inland(p00, p10, p01, ox, oy) or inland(p11, p01, p10, ox, oy)):
                    mesh.quad("IslandCliff", i00, i10, i11, i01)
                elif not (inland(p00, p10, p11, ox, oy) or inland(p00, p11, p01, ox, oy)):
                    mesh.tri("IslandCliff", i00, i10, i11)
                    mesh.tri("IslandCliff", i00, i11, i01)
                else:
                    mesh.quad("IslandCliff", i00, i10, i11, i01)
                    folds += 1
        meshes.append(mesh)
        edges.append([(column[0][0], column[-1][0]) for column in grid] + [(c[0], c[0]) for c in grid[0]] + [(c[0], c[0]) for c in grid[-1]])
    return meshes, edges, folds


def check_sealed(island, edges):
    """Every point on a cliff's edges (its foot, the back of its cap, and both its ends) is under the ground.
    Returns (edge points checked, how many are not buried, the shallowest burial in metres)."""
    checked, exposed, shallowest = 0, 0, 1e9
    for run in edges:
        for a, b in run:
            for p in (a, b):
                depth = island.height(p[0], p[1]) - p[2]
                checked += 1
                shallowest = min(shallowest, depth)
                if depth < 0.05:
                    exposed += 1
    return checked, exposed, shallowest


def write_cliffs(island, out_dir):
    """Writes each cliff's OBJ into out_dir. Returns [(name, path, triangles)]; raises if any cliff isn't sealed."""
    import os
    os.makedirs(out_dir, exist_ok=True)
    meshes, edges, folds = build_cliffs(island)
    checked, exposed, shallowest = check_sealed(island, edges)
    if exposed:
        raise RuntimeError(f"{island.name}'s cliffs are not sealed: {exposed} of {checked} edge points are above ground")
    if folds:
        raise RuntimeError(f"{island.name}'s cliffs have {folds} face quads turned inland (holes)")
    written = []
    for i, mesh in enumerate(meshes):
        name = "SM_%s_Cliff_%d" % (island.name, i)
        path = os.path.join(out_dir, name + ".obj")
        mesh.write_obj(path, comment="Riptide island cliff, generated by riptide_island_cliff.py")
        written.append((name, path, mesh.triangle_count()))
    return written


if __name__ == "__main__":
    import sys
    import riptide_island_shape as shape
    isl = shape.StartCay()
    built, edge_list, fold_count = build_cliffs(isl)
    print("cliffs:", [m.triangle_count() for m in built], "triangles")
    print("sealed check (points, exposed, shallowest burial m):", check_sealed(isl, edge_list))
    print("folded face quads (must be 0):", fold_count)
    if len(sys.argv) > 1:
        print(write_cliffs(isl, sys.argv[1]))
