"""The shape of Riptide's hand-designed islands: where the coast runs, how the ground rises from it and how the
seabed falls away. Plain Python with no Unreal in it, so the same code shapes the island in the editor
(riptide_islands.py turns it into meshes) and draws previews and runs checks outside it (Tools/island_preview.py).

An island is designed, not generated: its coast is a list of points placed by hand (metres, x east, y north, from
the island's centre), each marked as sand or rock, and its hills and reefs are placed the same way. The only
noise is small-scale roughness on top. Height 0 is sea level; the coast as drawn is exactly the waterline.
"""
import math

# --- Small maths -------------------------------------------------------------------------------------------------


def smoothstep(a, b, x):
    if a == b:
        return 0.0 if x < a else 1.0
    t = (x - a) / (b - a)
    t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


class Curve:
    """A smooth curve through hand-placed (x, y) knots that never overshoots them (monotone cubic), flat beyond
    its ends. Used for the beach and seabed profiles, so a designed slope has no kinks and no surprise bumps."""

    def __init__(self, knots):
        self.x = [k[0] for k in knots]
        self.y = [k[1] for k in knots]
        n = len(knots)
        h = [self.x[i + 1] - self.x[i] for i in range(n - 1)]
        d = [(self.y[i + 1] - self.y[i]) / h[i] for i in range(n - 1)]
        m = [0.0] * n
        for i in range(1, n - 1):
            if d[i - 1] * d[i] > 0.0:
                w1, w2 = 2.0 * h[i] + h[i - 1], h[i] + 2.0 * h[i - 1]
                m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
        m[0], m[-1] = d[0], 0.0
        self.m, self.h = m, h

    def __call__(self, x):
        xs = self.x
        if x <= xs[0]:
            return self.y[0]
        if x >= xs[-1]:
            return self.y[-1]
        lo, hi = 0, len(xs) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if xs[mid] <= x:
                lo = mid
            else:
                hi = mid
        h = self.h[lo]
        t = (x - xs[lo]) / h
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1) * self.y[lo] + (t3 - 2 * t2 + t) * h * self.m[lo]
                + (-2 * t3 + 3 * t2) * self.y[lo + 1] + (t3 - t2) * h * self.m[lo + 1])


def _hash(ix, iy, seed):
    n = (ix * 374761393 + iy * 668265263 + seed * 1274126177) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1103515245) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def noise(x, y, seed=0):
    """Smooth value noise in 0..1, the same on every machine."""
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3.0 - 2.0 * fx), fy * fy * (3.0 - 2.0 * fy)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy


def fbm(x, y, seed=0, octaves=3):
    """Layered noise in about -1..1."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * (noise(x, y, seed + o * 17) * 2.0 - 1.0)
        norm += amp
        x, y, amp = x * 2.03 + 11.7, y * 2.03 - 5.3, amp * 0.5
    return total / norm


def ridged(x, y, seed=0, octaves=3):
    """Noise with sharp creases, 0..1: broken rock."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * (1.0 - abs(noise(x, y, seed + o * 31) * 2.0 - 1.0))
        norm += amp
        x, y, amp = x * 2.1 + 3.1, y * 2.1 + 7.9, amp * 0.5
    return total / norm


# --- The island ----------------------------------------------------------------------------------------------------

class Island:
    """One designed island. Subclasses give the coast and the ground; this class works out distance to the coast
    and answers height(x, y) anywhere within `extent` metres of the centre."""

    name = "Island"
    extent = 384.0          # the island's patch of seabed reaches this far each way from its centre, metres
    deep = -30.0            # the seabed at the patch's edge
    grid = 2.0              # spacing of the distance-to-coast grid, metres
    turn = 0.0              # the whole design turned clockwise by this many degrees
    coast_points = []       # (x, y, rock): the coast, clockwise, rock 0 (sand beach) to 1 (rock shore)

    def __init__(self):
        self._build_coast()
        self._build_distance_grid()

    # Design coordinates are turned onto the map once, here.
    def place(self, x, y):
        a = math.radians(self.turn)
        c, s = math.cos(a), math.sin(a)
        return (x * c + y * s, -x * s + y * c)

    def _build_coast(self):
        pts = [self.place(x, y) + (r,) for x, y, r in self.coast_points]
        n = len(pts)
        xs, ys, rock = [], [], []
        for i in range(n):
            p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
            steps = max(2, int(math.hypot(p2[0] - p1[0], p2[1] - p1[1])))
            for k in range(steps):
                t = k / steps
                t2, t3 = t * t, t * t * t
                # Catmull-Rom: a smooth line through every placed point.
                xs.append(0.5 * (2 * p1[0] + (p2[0] - p0[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                                 + (3 * p1[0] - p0[0] - 3 * p2[0] + p3[0]) * t3))
                ys.append(0.5 * (2 * p1[1] + (p2[1] - p0[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                                 + (3 * p1[1] - p0[1] - 3 * p2[1] + p3[1]) * t3))
                rock.append(lerp(p1[2], p2[2], smoothstep(0.0, 1.0, t)))
        self.cx, self.cy, self.crock = xs, ys, rock
        self.cn = len(xs)

    def _build_distance_grid(self):
        g, ext = self.grid, self.extent
        n = int(round(2 * ext / g)) + 1
        self.gn = n
        cx, cy, cn = self.cx, self.cy, self.cn
        INF = 1e18
        best = [INF] * (n * n)       # squared distance from each grid point to its nearest coast point
        near = [-1] * (n * n)        # which coast point that is
        for i in range(cn):
            gx, gy = int(round((cx[i] + ext) / g)), int(round((cy[i] + ext) / g))
            for oy in (-1, 0, 1):
                for ox in (-1, 0, 1):
                    x, y = gx + ox, gy + oy
                    if 0 <= x < n and 0 <= y < n:
                        d = (x * g - ext - cx[i]) ** 2 + (y * g - ext - cy[i]) ** 2
                        if d < best[y * n + x]:
                            best[y * n + x], near[y * n + x] = d, i

        def sweep(order_y, order_x, offsets):
            for y in order_y:
                py = y * g - ext
                row = y * n
                for x in order_x:
                    k = row + x
                    px = x * g - ext
                    b, who = best[k], near[k]
                    for ox, oy in offsets:
                        x2, y2 = x + ox, y + oy
                        if 0 <= x2 < n and 0 <= y2 < n:
                            i = near[y2 * n + x2]
                            if i >= 0 and i != who:
                                d = (px - cx[i]) ** 2 + (py - cy[i]) ** 2
                                if d < b:
                                    b, who = d, i
                    best[k], near[k] = b, who

        fwd, back = range(n), range(n - 1, -1, -1)
        for _ in range(2):
            sweep(fwd, fwd, ((-1, 0), (0, -1), (-1, -1), (1, -1)))
            sweep(back, back, ((1, 0), (0, 1), (1, 1), (-1, 1)))

        # Which grid points are on land: fill between the coast's crossings of each grid row.
        inside = [False] * (n * n)
        for y in range(n):
            py = y * g - ext
            xings = []
            for i in range(cn):
                j = (i + 1) % cn
                y0, y1 = cy[i], cy[j]
                if (y0 <= py) != (y1 <= py):
                    xings.append(cx[i] + (py - y0) / (y1 - y0) * (cx[j] - cx[i]))
            xings.sort()
            for a in range(0, len(xings) - 1, 2):
                x0 = max(0, int(math.ceil((xings[a] + ext) / g)))
                x1 = min(n - 1, int(math.floor((xings[a + 1] + ext) / g)))
                for x in range(x0, x1 + 1):
                    inside[y * n + x] = True

        self.gdist = [math.sqrt(best[k]) * (1.0 if inside[k] else -1.0) for k in range(n * n)]
        self.gnear = near
        # Sand-or-rock as a smooth field: each point takes its nearest stretch of coast's, then blurred so the two
        # never meet in a line (a line would show as a step in the ground).
        field = [self.crock[i] for i in near]
        for _ in range(3):
            field = _blur(field, n, 3)
        self.grock = field

    def _grid_sample(self, values, x, y):
        g, ext, n = self.grid, self.extent, self.gn
        fx, fy = (x + ext) / g, (y + ext) / g
        ix = 0 if fx < 0 else n - 2 if fx > n - 2 else int(fx)
        iy = 0 if fy < 0 else n - 2 if fy > n - 2 else int(fy)
        tx, ty = min(max(fx - ix, 0.0), 1.0), min(max(fy - iy, 0.0), 1.0)
        k = iy * n + ix
        a, b, c, d = values[k], values[k + 1], values[k + n], values[k + n + 1]
        return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty

    def coast_distance(self, x, y):
        """Metres to the coast: positive on land, negative at sea. Exact close to the coast."""
        d = self._grid_sample(self.gdist, x, y)
        if abs(d) > 9.0:
            return d
        g, ext, n = self.grid, self.extent, self.gn
        ix = min(max(int((x + ext) / g), 0), n - 2)
        iy = min(max(int((y + ext) / g), 0), n - 2)
        cx, cy, cn = self.cx, self.cy, self.cn
        best, side = 1e18, 1.0
        seen = set()
        for k in (iy * n + ix, iy * n + ix + 1, (iy + 1) * n + ix, (iy + 1) * n + ix + 1):
            i0 = self.gnear[k]
            if i0 in seen:
                continue
            seen.add(i0)
            for i in range(i0 - 8, i0 + 8):
                a, b = i % cn, (i + 1) % cn
                ex, ey = cx[b] - cx[a], cy[b] - cy[a]
                wx, wy = x - cx[a], y - cy[a]
                t = (wx * ex + wy * ey) / (ex * ex + ey * ey + 1e-12)
                t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
                dx, dy = wx - ex * t, wy - ey * t
                dd = dx * dx + dy * dy
                if dd < best:
                    # The coast runs clockwise, so land is on the right of each stretch.
                    best, side = dd, (1.0 if ex * wy - ey * wx < 0.0 else -1.0)
        return math.sqrt(best) * side

    def rock(self, x, y):
        return self._grid_sample(self.grock, x, y)

    def height(self, x, y):
        raise NotImplementedError

    def ground(self, x, y, h=None, slope=None):
        """What the ground is at a point, for previews and (later) its surfaces: 'deep', 'reef', 'shallows',
        'wet_sand', 'sand', 'rock' or 'grove'."""
        raise NotImplementedError

    def slope_deg(self, x, y, e=0.5):
        dx = (self.height(x + e, y) - self.height(x - e, y)) / (2 * e)
        dy = (self.height(x, y + e) - self.height(x, y - e)) / (2 * e)
        return math.degrees(math.atan(math.hypot(dx, dy)))


def _blur(values, n, radius):
    """A box blur over an n x n grid."""
    out = [0.0] * (n * n)
    tmp = [0.0] * (n * n)
    w = 2 * radius + 1
    for y in range(n):
        row = values[y * n:(y + 1) * n]
        acc = row[0] * (radius + 1) + sum(row[1:radius + 1])
        for x in range(n):
            tmp[y * n + x] = acc / w
            acc += row[min(n - 1, x + radius + 1)] - row[max(0, x - radius)]
    for x in range(n):
        col = tmp[x::n]
        acc = col[0] * (radius + 1) + sum(col[1:radius + 1])
        for y in range(n):
            out[y * n + x] = acc / w
            acc += col[min(n - 1, y + radius + 1)] - col[max(0, y - radius)]
    return out


class StartCay(Island):
    """Island 1 on the chart: where the crew washes up. A low limestone cay about 230 m long.

    - A crescent of sand beach in a bay on the side facing the big island: the wash-up beach, sheltered between a
      low rocky point and the headland, shelving so gently a boat noses in and the crew wades ashore.
    - A rock knoll on the headland at the bay's far end, the cay's high point (about 8 m): the lookout, and flint.
    - A flat, sandy grove behind the beach's low dune, 2.5-3.5 m above the sea: palms, fibre, driftwood.
    - A low broken rock shore down the seaward side, dropping into deeper water, with patches of reef off it.
    - A sand spit trailing off the far end into the shallows.
    """

    name = "StartCay"
    turn = 50.0     # the bay faces north-east, toward the big island

    # Clockwise from the tip of the sand spit. Before the turn: +x runs along the cay toward the knoll, +y is the
    # bay side.
    coast_points = [
        (-122, 2, 0.0), (-100, 14, 0.0), (-72, 24, 0.0), (-48, 36, 0.3),
        (-28, 50, 1.0), (-12, 52, 1.0),                                       # the low rocky point
        (0, 42, 0.5), (10, 28, 0.0), (30, 19, 0.0), (52, 20, 0.0), (68, 30, 0.0),   # the bay and its beach
        (82, 40, 0.8), (97, 34, 1.0), (104, 16, 1.0), (100, -6, 1.0),         # the headland under the knoll
        (88, -24, 1.0), (66, -36, 1.0), (40, -46, 1.0), (10, -50, 0.9),       # the seaward rock shore
        (-22, -46, 0.6), (-52, -36, 0.2), (-82, -24, 0.0), (-106, -12, 0.0),  # easing back to sand, to the spit
    ]

    # Height up the shore from the waterline (metres inland, metres up).
    # Sand: the beach face, its berm, then a dune ridge with a swale behind it before the grove.
    SAND_SHORE = Curve([(0, 0.0), (4, 0.5), (10, 1.25), (13, 1.5), (16, 1.45), (23, 2.5), (27, 2.6), (34, 2.1), (48, 2.7), (70, 3.0)])
    # Rock: a low broken cliff straight out of the sea, then a rough bench.
    ROCK_SHORE = Curve([(0, 0.0), (1.0, 1.3), (2.5, 2.3), (6, 2.7), (14, 2.9), (30, 3.0), (70, 3.2)])
    # Depth out from the waterline (metres out, metres down). Off the sand the bottom stays wadeable for a long
    # way, then shelves to the reef edge and drops off; off the rock it's deep at once.
    SAND_SEABED = Curve([(0, 0.0), (6, -0.45), (15, -0.8), (35, -1.3), (60, -2.0), (100, -3.5), (140, -6.0),
                         (200, -14.0), (260, -30.0), (300, -30.0)])
    ROCK_SEABED = Curve([(0, 0.0), (1.5, -1.2), (6, -2.2), (20, -3.2), (60, -5.0), (100, -6.5), (140, -9.0),
                         (200, -17.0), (260, -30.0), (300, -30.0)])

    KNOLL = (84.0, 2.0)         # centre, before the turn: hard against the seaward shore, so it drops to the sea
    KNOLL_HEIGHT = 8.5
    KNOLL_RADIUS = 30.0         # a steep-sided, flat-topped bluff, not a rounded hill
    POINT = (-20.0, 41.0)       # the rise on the rocky point at the bay's other end
    POINT_HEIGHT = 2.4
    POINT_RADIUS = 15.0
    HOLLOW = (8.0, -8.0)        # a shallow pan in the middle of the grove, where rain collects
    HOLLOW_DEPTH = 1.0
    HOLLOW_RADIUS = 16.0

    def __init__(self):
        super().__init__()
        self.knoll = self.place(*self.KNOLL)
        self.point = self.place(*self.POINT)
        self.hollow = self.place(*self.HOLLOW)
        # Places on the island that other code and the checks refer to.
        self.beach = self.place(40.0, 24.0)             # the middle of the wash-up beach, just above the waterline
        self.beach_out = self.place(40.0, 100.0)        # straight out to sea from it
        self.summit = self.knoll
        self.bay_side = self.place(0.0, 1.0)             # the way the bay faces; the rock shore is the other way
        self.spit_tip = self.place(-122.0, 2.0)
        self.spit_run = self.place(-1.0, -0.12)         # the way the spit points, on out to sea
        length = math.hypot(*self.spit_run)
        self.spit_run = (self.spit_run[0] / length, self.spit_run[1] / length)

    def height(self, x, y):
        d = self.coast_distance(x, y)
        r = self.rock(x, y)
        if d >= 0.0:
            h = lerp(self.SAND_SHORE(d), self.ROCK_SHORE(d), r)
        else:
            h = lerp(self.SAND_SEABED(-d), self.ROCK_SEABED(-d), r)

        inland = smoothstep(0.0, 7.0, d)
        if inland > 0.0:
            knoll = self._knoll(x, y)
            point = self._mound(x, y, self.point, self.POINT_RADIUS)
            h += inland * (self.KNOLL_HEIGHT * knoll + self.POINT_HEIGHT * point)
            h -= self.HOLLOW_DEPTH * self._mound(x, y, self.hollow, self.HOLLOW_RADIUS)
            # The grove floor rolls a little; the bluff and the point break into rock outcrop.
            h += smoothstep(4.0, 22.0, d) * 0.35 * fbm(x / 28.0, y / 28.0, 5)
            h += inland * (knoll * 1.3 + point * 0.6) * (ridged(x / 8.0, y / 8.0, 9) - 0.5)

        # Rock shores are broken and uneven for a few metres either side of the waterline.
        if r > 0.02:
            band = smoothstep(-14.0, -2.0, d) * (1.0 - smoothstep(6.0, 22.0, d))
            h += r * band * 0.55 * (ridged(x / 5.0, y / 5.0, 21) - 0.55)

        if d < -4.0:
            s = -d
            # Sand bars and hollows across the shallows.
            h += smoothstep(4.0, 20.0, s) * (1.0 - smoothstep(120.0, 200.0, s)) * 0.16 * fbm(x / 22.0, y / 22.0, 31)
            # Patches of reef off the rock shore: heads rising toward the surface, never drying out.
            if r > 0.25 and 45.0 < s < 150.0:
                patch = smoothstep(45.0, 70.0, s) * (1.0 - smoothstep(115.0, 150.0, s)) * smoothstep(0.25, 0.7, r)
                patch *= 1.0 - self._bay_approach(x, y)
                heads = smoothstep(0.52, 0.78, noise(x / 16.0, y / 16.0, 41)) * (0.75 + 0.25 * noise(x / 4.0, y / 4.0, 43))
                h = min(h + patch * heads * 3.2, max(h, -1.2))
            # The spit carries on under water as a sand bar you can wade out along.
            ax, ay = x - self.spit_tip[0], y - self.spit_tip[1]
            along = ax * self.spit_run[0] + ay * self.spit_run[1]
            if -10.0 < along < 80.0:
                across = abs(ax * self.spit_run[1] - ay * self.spit_run[0])
                bar = (1.0 - smoothstep(5.0, 18.0, across)) * (1.0 - smoothstep(45.0, 80.0, along))
                h = max(h, lerp(h, -0.35 - 0.006 * max(along, 0.0), bar))
        return h

    def _knoll(self, x, y):
        """0..1: the bluff's shape. Its edge wanders, its sides are steep and its top nearly flat, tilted a
        little so the highest ground is on the seaward lip."""
        kx, ky = x - self.knoll[0], y - self.knoll[1]
        dist = math.hypot(kx, ky) * (1.0 + 0.22 * fbm(x / 18.0, y / 18.0, 13))
        side = 1.0 - smoothstep(self.KNOLL_RADIUS * 0.42, self.KNOLL_RADIUS, dist)
        return side * (0.86 + 0.14 * smoothstep(-20.0, 20.0, -(kx * self.bay_side[0] + ky * self.bay_side[1])))

    @staticmethod
    def _mound(x, y, centre, radius):
        """0..1: a smooth rise (or hollow) of the given radius."""
        return 1.0 - smoothstep(0.0, radius, math.hypot(x - centre[0], y - centre[1]))

    def _bay_approach(self, x, y):
        """1 in the open water straight out from the wash-up beach, 0 elsewhere: the way in is kept clear of reef."""
        vx, vy = x - self.beach[0], y - self.beach[1]
        ox, oy = self.beach_out[0] - self.beach[0], self.beach_out[1] - self.beach[1]
        along = (vx * ox + vy * oy) / (math.hypot(vx, vy) * math.hypot(ox, oy) + 1e-9)
        return smoothstep(0.5, 0.8, along)

    def ground(self, x, y, h=None, slope=None):
        h = self.height(x, y) if h is None else h
        slope = self.slope_deg(x, y) if slope is None else slope
        d = self.coast_distance(x, y)
        r = self.rock(x, y)
        if h < -0.25:
            if h < -9.0:
                return "deep"
            if r > 0.25 and 45.0 < -d < 150.0 and h > lerp(self.SAND_SEABED(-d), self.ROCK_SEABED(-d), r) + 0.5:
                return "reef"
            return "rock" if (r > 0.6 and -d < 14.0) else "shallows"
        on_knoll = self._knoll(x, y) > 0.3 or self._mound(x, y, self.point, self.POINT_RADIUS) > 0.45
        if slope > 32.0 or (r > 0.6 and d < 12.0) or (on_knoll and slope > 16.0):
            return "rock"
        if h < 0.35:
            return "wet_sand"
        if d > 21.0 and h > 1.8:
            return "grove"
        return "sand"


# --- Checks ------------------------------------------------------------------------------------------------------

WALKABLE_SLOPE = 40.0   # degrees; the crew's movement gives up a little steeper than this (44.7)


def walk_check(island, start, step=1.0, limit=None):
    """Floods outward on foot from `start` (x, y) across ground no steeper than the crew can walk and no deeper
    than they can wade. Returns (reached, land): sets of grid cells, `land` being every cell above the waterline."""
    limit = limit or 200.0
    n = int(limit * 2 / step) + 1
    heights = {}

    def h(ix, iy):
        k = (ix, iy)
        if k not in heights:
            heights[k] = island.height(ix * step - limit, iy * step - limit)
        return heights[k]

    sx, sy = int(round((start[0] + limit) / step)), int(round((start[1] + limit) / step))
    reached, frontier = {(sx, sy)}, [(sx, sy)]
    rise = math.tan(math.radians(WALKABLE_SLOPE)) * step
    while frontier:
        ix, iy = frontier.pop()
        h0 = h(ix, iy)
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            jx, jy = ix + ox, iy + oy
            if (jx, jy) in reached or not (0 <= jx < n and 0 <= jy < n):
                continue
            h1 = h(jx, jy)
            if h1 < -1.2 or abs(h1 - h0) > rise:
                continue
            reached.add((jx, jy))
            frontier.append((jx, jy))
    land = {(ix, iy) for ix in range(n) for iy in range(n) if h(ix, iy) > 0.0}
    return reached, land
