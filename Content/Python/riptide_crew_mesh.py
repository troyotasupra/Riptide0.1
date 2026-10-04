"""Builds the crew's uniforms and gear in code and writes them as skinned glTF files that Unreal imports.

No third-party models beyond Quaternius's CC0 bodies and hair (SourceAssets/Characters/Quaternius): everything the
crew wears is generated here from the bodies themselves, so it fits each build exactly and deforms with it.
Pure Python (no Unreal), so it runs anywhere.

    build_all(source_dir, out_dir) -> {body: {asset name: file path}}

How things are made:
- Clothes that hug the body (uniform, boots, gloves, balaclava, shemagh) are the body's own triangles, pushed out
  along their normals and smoothed into cloth, keeping the body's skin weights: they bend exactly as the skin does.
- Hair (authored on the female body) is refitted to each head, with a "_Hat" variant pressed flat under headgear.
- Rigid gear (helmet, hats, glasses, plates, pouches, packs) is modelled around the body's measurements (the head's
  shape, the chest's surface) and bound to one bone, the same as attaching it to that bone, but fitted per body.
- Soft gear (cummerbund, straps) is laid on the uniform's surface and takes the weights of the surface under it.

Coordinates are glTF's: metres in the files, centimetres here; Y up, the character faces +Z, +X is its left. Unreal
imports them as X = x, Y = z (forward), Z = y (up). Every file carries the full body skeleton (same joints, same
bind pose as the body it's made for), so Unreal binds it to the crew's one shared skeleton.
"""

import json
import math
import os
import struct

# --- Small vector maths (tuples, centimetres) ----------------------------------------------------------------------


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def norm(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-9 else (0.0, 1.0, 0.0)


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def rotate(v, axis, angle):
    """v turned by angle (radians) about the unit axis (right-handed)."""
    c, s = math.cos(angle), math.sin(angle)
    return add(add(mul(v, c), mul(cross(axis, v), s)), mul(axis, dot(axis, v) * (1.0 - c)))


def mat_apply(m, p):
    """A column-major 4x4 (glTF's layout) applied to a point."""
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])


def mat_apply_dir(m, d):
    return (m[0] * d[0] + m[4] * d[1] + m[8] * d[2],
            m[1] * d[0] + m[5] * d[1] + m[9] * d[2],
            m[2] * d[0] + m[6] * d[1] + m[10] * d[2])


def mat_mul(a, b):
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return out


def mat_inverse_affine(m):
    """Inverse of an affine column-major 4x4 (rotation, scale and translation)."""
    a, b, c = m[0], m[4], m[8]
    d, e, f = m[1], m[5], m[9]
    g, h, i = m[2], m[6], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    inv = [(e * i - f * h) / det, -(b * i - c * h) / det, (b * f - c * e) / det,
           -(d * i - f * g) / det, (a * i - c * g) / det, -(a * f - c * d) / det,
           (d * h - e * g) / det, -(a * h - b * g) / det, (a * e - b * d) / det]
    t = (m[12], m[13], m[14])
    r = [inv[0], inv[3], inv[6], 0.0, inv[1], inv[4], inv[7], 0.0, inv[2], inv[5], inv[8], 0.0]
    tx = -(inv[0] * t[0] + inv[1] * t[1] + inv[2] * t[2])
    ty = -(inv[3] * t[0] + inv[4] * t[1] + inv[5] * t[2])
    tz = -(inv[6] * t[0] + inv[7] * t[1] + inv[8] * t[2])
    return r + [tx, ty, tz, 1.0]


# --- Reading glTF --------------------------------------------------------------------------------------------------

_COMPONENTS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Gltf:
    """A .gltf file and its buffers, with accessors decoded into lists of tuples."""

    def __init__(self, path):
        self.path = path
        with open(path, "r") as f:
            self.json = json.load(f)
        folder = os.path.dirname(path)
        self.buffers = []
        for b in self.json["buffers"]:
            with open(os.path.join(folder, b["uri"]), "rb") as f:
                self.buffers.append(f.read())

    def accessor(self, index):
        a = self.json["accessors"][index]
        view = self.json["bufferViews"][a["bufferView"]]
        fmt = _COMPONENTS[a["componentType"]]
        width = _WIDTH[a["type"]]
        size = struct.calcsize(fmt)
        stride = view.get("byteStride", size * width)
        data = self.buffers[view["buffer"]]
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        out = []
        for i in range(a["count"]):
            vals = struct.unpack_from("<" + fmt * width, data, start + i * stride)
            if a.get("normalized"):
                top = float((1 << (8 * size)) - 1) if fmt in "BHI" else float((1 << (8 * size - 1)) - 1)
                vals = tuple(v / top for v in vals)
            out.append(vals)
        return out


class Rig:
    """A body's skeleton as its glTF file has it: the joints (in skin order), their bind matrices, and the node
    list (written unchanged into every generated file, so the bind pose is identical)."""

    def __init__(self, gltf):
        self.gltf = gltf
        skin = gltf.json["skins"][0]
        self.joint_nodes = skin["joints"]
        self.names = [gltf.json["nodes"][n]["name"] for n in self.joint_nodes]
        self.index = {n: i for i, n in enumerate(self.names)}
        self.ibm = [list(m) for m in gltf.accessor(skin["inverseBindMatrices"])]
        # The bind pose in centimetres: each joint's matrix from its own space into the mesh's.
        self.bind = []
        for m in self.ibm:
            b = mat_inverse_affine(m)
            self.bind.append(b[:12] + [b[12] * 100.0, b[13] * 100.0, b[14] * 100.0, 1.0])

    def pos(self, name):
        b = self.bind[self.index[name]]
        return (b[12], b[13], b[14])

    def matrix(self, name):
        return self.bind[self.index[name]]


class Body:
    """A body mesh (one primitive of a glTF) in centimetres: positions, normals, UVs, skin, triangles."""

    def __init__(self, gltf, rig, mesh_name=None, mesh_index=None):
        meshes = gltf.json["meshes"]
        if mesh_index is None:
            mesh_index = max(range(len(meshes)), key=lambda i: gltf.json["accessors"][meshes[i]["primitives"][0]["attributes"]["POSITION"]]["count"]) \
                if mesh_name is None else next(i for i, m in enumerate(meshes) if m["name"] == mesh_name)
        prim = meshes[mesh_index]["primitives"][0]
        attr = prim["attributes"]
        self.rig = rig
        self.pos = [(p[0] * 100.0, p[1] * 100.0, p[2] * 100.0) for p in gltf.accessor(attr["POSITION"])]
        self.nrm = [norm(n) for n in gltf.accessor(attr["NORMAL"])]
        self.uv = gltf.accessor(attr["TEXCOORD_0"]) if "TEXCOORD_0" in attr else [(0.0, 0.0)] * len(self.pos)
        joints = gltf.accessor(attr["JOINTS_0"])
        weights = gltf.accessor(attr["WEIGHTS_0"])
        # The mesh's joint indices are into its own file's skin: renamed onto the rig's (they match for the bodies;
        # the hair files have the same joints).
        own = [gltf.json["nodes"][n]["name"] for n in gltf.json["skins"][0]["joints"]]
        self.skin = []
        for j, w in zip(joints, weights):
            pairs = {}
            for ji, wi in zip(j, w):
                if wi > 0.0:
                    name = own[ji]
                    pairs[rig.index[name]] = pairs.get(rig.index[name], 0.0) + wi
            self.skin.append(normalise_skin(pairs))
        idx = [i[0] for i in gltf.accessor(prim["indices"])]
        self.tris = [(idx[i], idx[i + 1], idx[i + 2]) for i in range(0, len(idx), 3)]

    def weight_on(self, v, bones):
        """How much of vertex v's weight is on these bones (names)."""
        ids = {self.rig.index[b] for b in bones}
        return sum(w for j, w in self.skin[v].items() if j in ids)

    def dominant(self, v):
        return self.rig.names[max(self.skin[v].items(), key=lambda kv: kv[1])[0]]


def normalise_skin(pairs, keep=4):
    """The strongest `keep` influences, summing to one, as {joint: weight}."""
    top = sorted(pairs.items(), key=lambda kv: -kv[1])[:keep]
    total = sum(w for _, w in top) or 1.0
    return {j: w / total for j, w in top}


def blend_skins(skins, factors):
    pairs = {}
    for s, f in zip(skins, factors):
        for j, w in s.items():
            pairs[j] = pairs.get(j, 0.0) + w * f
    return normalise_skin(pairs)


# --- The generated mesh --------------------------------------------------------------------------------------------


class SkinMesh:
    """Skinned triangles grouped by material slot: each vertex has a position, normal, UV, colour and skin."""

    def __init__(self):
        self.verts = []      # (pos, normal, uv, colour, skin)
        self.faces = {}      # material -> [(i, j, k)]

    def vert(self, p, n, skin, uv=(0.0, 0.0), colour=(1.0, 1.0, 1.0, 1.0)):
        self.verts.append((p, n, uv, colour, skin))
        return len(self.verts) - 1

    def tri(self, material, i, j, k):
        self.faces.setdefault(material, []).append((i, j, k))

    def part(self, points, tris, material, skin_of, outward_hint=None, colour=(1.0, 1.0, 1.0, 1.0), uv_scale=10.0, smooth=True):
        """One smooth-shaded part. skin_of(p) gives each point's skin. Triangles are turned to face away from
        outward_hint(p) (a point inside the part) when it's given."""
        fixed = []
        acc = [(0.0, 0.0, 0.0)] * len(points)
        for i, j, k in tris:
            n = cross(sub(points[j], points[i]), sub(points[k], points[i]))
            if outward_hint is not None:
                c = mul(add(add(points[i], points[j]), points[k]), 1.0 / 3.0)
                if dot(n, sub(c, outward_hint(c))) < 0.0:
                    j, k = k, j
                    n = mul(n, -1.0)
            fixed.append((i, j, k))
            for v in (i, j, k):
                acc[v] = add(acc[v], n)
        if not smooth:
            # Flat: each triangle gets its own corners.
            for i, j, k in fixed:
                n = norm(cross(sub(points[j], points[i]), sub(points[k], points[i])))
                ids = [self.vert(points[v], n, skin_of(points[v]), _box_uv(points[v], n, uv_scale), colour) for v in (i, j, k)]
                self.tri(material, *ids)
            return
        base = len(self.verts)
        for p, n in zip(points, acc):
            n = norm(n)
            self.vert(p, n, skin_of(p), _box_uv(p, n, uv_scale), colour)
        for i, j, k in fixed:
            self.tri(material, base + i, base + j, base + k)

    def grid(self, rows, material, skin_of, outward_hint=None, close_rows=False, close_cols=False, colour=(1.0, 1.0, 1.0, 1.0),
             uvs=None):
        """A surface through rows of points (equal-length lists), quads between neighbours. uvs, if given, has the
        same shape as rows."""
        w = len(rows[0])
        pts = [p for row in rows for p in row]
        tris = []
        nr = len(rows) if close_cols else len(rows) - 1
        for r in range(nr):
            r2 = (r + 1) % len(rows)
            for c in range(w if close_rows else w - 1):
                c2 = (c + 1) % w
                a, b, cc, d = r * w + c, r * w + c2, r2 * w + c2, r2 * w + c
                tris += [(a, b, cc), (a, cc, d)]
        base = len(self.verts)
        self.part(pts, tris, material, skin_of, outward_hint, colour)
        if uvs is not None:
            flat = [uv for row in uvs for uv in row]
            for i, uv in enumerate(flat):
                p, n, _, col, s = self.verts[base + i]
                self.verts[base + i] = (p, n, uv, col, s)

    def box(self, centre, axes, half, material, skin_of, colour=(1.0, 1.0, 1.0, 1.0), bevel=0.0):
        """A box (optionally bevelled) at centre with unit axes (ax, ay, az) and half sizes (hx, hy, hz)."""
        ax, ay, az = axes
        hx, hy, hz = half
        b = min(bevel, hx * 0.45, hy * 0.45, hz * 0.45)

        def at(x, y, z):
            return add(centre, add(add(mul(ax, x), mul(ay, y)), mul(az, z)))
        if b <= 0.0:
            corners = [at(sx * hx, sy * hy, sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
            faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
            for f in faces:
                self.part([corners[i] for i in f], [(0, 1, 2), (0, 2, 3)], material, skin_of, lambda p: centre, colour, smooth=False)
            return
        # Bevelled: a rounded-rectangle profile in X/Y, swept along Z with bevelled ends.
        prof = _rounded_rect(hx, hy, b, 3)
        rings = [(-hz, 1.0 - 1.0), (-hz + b * 0.3, 0.6), (-hz + b, 0.0), (hz - b, 0.0), (hz - b * 0.3, 0.6), (hz, 1.0)]
        rows = []
        for z, inset in rings:
            k = 1.0 - inset * b / max(hx, hy) * 1.0
            rows.append([at(x * (1.0 - inset * b / hx), y * (1.0 - inset * b / hy), z) for x, y in prof])
        self.grid(rows, material, skin_of, outward_hint=lambda p: centre, close_rows=True, colour=colour)
        for z, row in ((rings[0][0], rows[0]), (rings[-1][0], rows[-1])):
            self.fan(row, material, skin_of, lambda p: centre, colour)

    def fan(self, outline, material, skin_of, outward_hint=None, colour=(1.0, 1.0, 1.0, 1.0)):
        c = mul(sum_points(outline), 1.0 / len(outline))
        pts = [c] + list(outline)
        tris = [(0, i + 1, (i + 1) % len(outline) + 1) for i in range(len(outline))]
        self.part(pts, tris, material, skin_of, outward_hint, colour, smooth=False)

    def tube(self, path, radius, material, skin_of, sides=8, caps=True, colour=(1.0, 1.0, 1.0, 1.0)):
        """A round tube along a polyline, capped."""
        rows = []
        u = None
        for i, p in enumerate(path):
            d = norm(sub(path[min(i + 1, len(path) - 1)], path[max(i - 1, 0)]))
            if u is None:
                u = norm(cross(d, (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)))
            else:
                # Carried along the path (parallel transport), so the rings never twist against each other.
                u = norm(sub(u, mul(d, dot(u, d))))
            v = cross(d, u)
            rows.append([add(p, add(mul(u, radius * math.cos(a)), mul(v, radius * math.sin(a))))
                         for a in (2.0 * math.pi * s / sides for s in range(sides))])
        nearest = lambda q: min(path, key=lambda c: dot(sub(c, q), sub(c, q)))
        self.grid(rows, material, skin_of, outward_hint=nearest, close_rows=True, colour=colour)
        if caps:
            self.fan(rows[0], material, skin_of, lambda q: path[1], colour)
            self.fan(rows[-1], material, skin_of, lambda q: path[-2], colour)

    def strap(self, path, normals, width, thick, material, skin_of, colour=(1.0, 1.0, 1.0, 1.0)):
        """A flat strap (a band width x thick) along a path lying on a surface whose normals are given per point."""
        rows = []
        for i, (p, n) in enumerate(zip(path, normals)):
            d = norm(sub(path[min(i + 1, len(path) - 1)], path[max(i - 1, 0)]))
            side = norm(cross(n, d))
            h = width * 0.5
            rows.append([add(p, mul(side, -h)), add(add(p, mul(side, -h)), mul(n, thick)),
                         add(add(p, mul(side, h)), mul(n, thick)), add(p, mul(side, h))])
        mid = lambda q: min(((add(p, mul(n, thick * 0.5))) for p, n in zip(path, normals)), key=lambda c: dot(sub(c, q), sub(c, q)))
        self.grid(rows, material, skin_of, outward_hint=mid, close_rows=True, colour=colour)
        self.fan(rows[0], material, skin_of, lambda q: add(path[1], mul(normals[1], thick * 0.5)), colour)
        self.fan(rows[-1], material, skin_of, lambda q: add(path[-2], mul(normals[-2], thick * 0.5)), colour)

    def triangle_count(self):
        return sum(len(f) for f in self.faces.values())


def sum_points(points):
    s = (0.0, 0.0, 0.0)
    for p in points:
        s = add(s, p)
    return s


def _box_uv(p, n, scale):
    """Box-projected UVs, one unit per `scale` cm (the gear materials use them for the weave)."""
    ax = max(range(3), key=lambda a: abs(n[a]))
    u, v = [(p[2], p[1]), (p[0], p[2]), (p[0], p[1])][ax]
    return (u / scale, v / scale)


def _rounded_rect(hx, hy, r, steps):
    """A rounded rectangle's outline (counter-clockwise), corner radius r."""
    pts = []
    for cx, cy, a0 in ((hx - r, hy - r, 0.0), (-hx + r, hy - r, 90.0), (-hx + r, -hy + r, 180.0), (hx - r, -hy + r, 270.0)):
        for s in range(steps + 1):
            a = math.radians(a0 + 90.0 * s / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


# --- Writing glTF --------------------------------------------------------------------------------------------------


def write_gltf(path, rig, mesh, name):
    """Writes the mesh as a skinned glTF (.gltf + .bin) on the rig's skeleton, with the rig's own nodes and bind
    matrices, so Unreal binds it to the same skeleton in the same pose."""
    src = rig.gltf.json
    nodes = json.loads(json.dumps(src["nodes"]))
    joint_set = set(rig.joint_nodes)
    # Keep the skeleton's nodes; the source's mesh nodes become empty, and one of them carries this mesh.
    carrier = None
    for i, n in enumerate(nodes):
        if "mesh" in n:
            del n["mesh"]
            n.pop("skin", None)
            if carrier is None and i not in joint_set:
                carrier = i
    nodes[carrier]["mesh"] = 0
    nodes[carrier]["skin"] = 0
    nodes[carrier]["name"] = name

    blob = bytearray()
    views, accessors = [], []

    def push(data, fmt, count, kind, comp, target=None, minmax=None):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(struct.pack("<" + fmt * (len(data)), *data))
        view = {"buffer": 0, "byteOffset": off, "byteLength": len(blob) - off}
        if target:
            view["target"] = target
        views.append(view)
        acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": kind}
        if minmax:
            acc["min"], acc["max"] = minmax
        accessors.append(acc)
        return len(accessors) - 1

    verts = mesh.verts
    pos = [c / 100.0 for v in verts for c in v[0]]
    lo = [min(v[0][a] for v in verts) / 100.0 for a in range(3)]
    hi = [max(v[0][a] for v in verts) / 100.0 for a in range(3)]
    a_pos = push(pos, "f", len(verts), "VEC3", 5126, 34962, (lo, hi))
    a_nrm = push([c for v in verts for c in norm(v[1])], "f", len(verts), "VEC3", 5126, 34962)
    a_uv = push([c for v in verts for c in v[2]], "f", len(verts), "VEC2", 5126, 34962)
    a_col = push([c for v in verts for c in v[3]], "f", len(verts), "VEC4", 5126, 34962)
    joints, weights = [], []
    for v in verts:
        pairs = sorted(v[4].items(), key=lambda kv: -kv[1])[:4]
        while len(pairs) < 4:
            pairs.append((0, 0.0))
        joints += [j for j, _ in pairs]
        weights += [w for _, w in pairs]
    a_j = push(joints, "H", len(verts), "VEC4", 5123, 34962)
    a_w = push(weights, "f", len(verts), "VEC4", 5126, 34962)
    a_ibm = push([c for m in rig.ibm for c in m], "f", len(rig.ibm), "MAT4", 5126)
    materials, prims = [], []
    for material, faces in mesh.faces.items():
        idx = [i for f in faces for i in f]
        a_idx = push(idx, "I", len(idx), "SCALAR", 5125, 34963)
        materials.append({"name": material, "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 1.0], "metallicFactor": 0.0}})
        prims.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv, "COLOR_0": a_col,
                                     "JOINTS_0": a_j, "WEIGHTS_0": a_w},
                      "indices": a_idx, "material": len(materials) - 1})
    bin_name = os.path.splitext(os.path.basename(path))[0] + ".bin"
    doc = {
        "asset": {"version": "2.0", "generator": "Riptide riptide_crew_mesh.py"},
        "scene": 0, "scenes": src["scenes"], "nodes": nodes,
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
        "skins": [{"name": "Armature", "joints": rig.joint_nodes, "inverseBindMatrices": a_ibm}],
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"uri": bin_name, "byteLength": len(blob)}],
    }
    with open(os.path.join(os.path.dirname(path), bin_name), "wb") as f:
        f.write(bytes(blob))
    with open(path, "w") as f:
        json.dump(doc, f)


# --- Surfaces: welding, neighbours, smoothing, and queries ---------------------------------------------------------


class Surface:
    """Triangles over a set of points (welded by position), for smoothing, ray casts and closest-point queries.
    Each point keeps a skin, so anything placed on the surface can take the skin of the spot it sits on."""

    def __init__(self, points, tris, skins, normals=None):
        self.points = list(points)
        self.tris = list(tris)
        self.skins = skins
        self.normals = normals if normals is not None else self._normals()
        self._grid = None

    def _normals(self):
        acc = [(0.0, 0.0, 0.0)] * len(self.points)
        for i, j, k in self.tris:
            n = cross(sub(self.points[j], self.points[i]), sub(self.points[k], self.points[i]))
            for v in (i, j, k):
                acc[v] = add(acc[v], n)
        return [norm(n) for n in acc]

    def _build_grid(self, cell=4.0):
        self._cell = cell
        self._grid = {}
        for t, (i, j, k) in enumerate(self.tris):
            a, b, c = self.points[i], self.points[j], self.points[k]
            lo = [int(math.floor(min(a[d], b[d], c[d]) / cell)) for d in range(3)]
            hi = [int(math.floor(max(a[d], b[d], c[d]) / cell)) for d in range(3)]
            for x in range(lo[0], hi[0] + 1):
                for y in range(lo[1], hi[1] + 1):
                    for z in range(lo[2], hi[2] + 1):
                        self._grid.setdefault((x, y, z), []).append(t)

    def raycast(self, origin, direction, max_dist=60.0):
        """The farthest hit along the ray within max_dist (the outer surface, seen from inside the body):
        (point, normal, skin) or None."""
        if self._grid is None:
            self._build_grid()
        d = norm(direction)
        seen = set()
        best = None
        step = self._cell * 0.5
        s = 0.0
        while s <= max_dist + step:
            p = add(origin, mul(d, s))
            key = tuple(int(math.floor(p[a] / self._cell)) for a in range(3))
            for t in self._grid.get(key, ()):
                if t in seen:
                    continue
                seen.add(t)
                hit = _ray_tri(origin, d, *(self.points[v] for v in self.tris[t]))
                if hit and hit[0] <= max_dist and (best is None or hit[0] > best[0]):
                    best = (hit[0], t, hit[1], hit[2])
            s += step
        if best is None:
            return None
        dist, t, u, v = best
        i, j, k = self.tris[t]
        w = (1.0 - u - v, u, v)
        p = add(origin, mul(d, dist))
        n = norm(add(add(mul(self.normals[i], w[0]), mul(self.normals[j], w[1])), mul(self.normals[k], w[2])))
        return p, n, blend_skins([self.skins[i], self.skins[j], self.skins[k]], w)

    def closest(self, q, radius=12.0):
        """The closest point of the surface to q within about radius: (point, normal, skin)."""
        if self._grid is None:
            self._build_grid()
        cell = self._cell
        r = int(math.ceil(radius / cell))
        key = tuple(int(math.floor(q[a] / cell)) for a in range(3))
        best = None
        seen = set()
        for x in range(key[0] - r, key[0] + r + 1):
            for y in range(key[1] - r, key[1] + r + 1):
                for z in range(key[2] - r, key[2] + r + 1):
                    for t in self._grid.get((x, y, z), ()):
                        if t in seen:
                            continue
                        seen.add(t)
                        i, j, k = self.tris[t]
                        p, w = _closest_on_tri(q, self.points[i], self.points[j], self.points[k])
                        d2 = dot(sub(p, q), sub(p, q))
                        if best is None or d2 < best[0]:
                            best = (d2, t, p, w)
        if best is None:
            # Nothing near: the nearest point outright.
            v = min(range(len(self.points)), key=lambda i: dot(sub(self.points[i], q), sub(self.points[i], q)))
            return self.points[v], self.normals[v], self.skins[v]
        _, t, p, w = best
        i, j, k = self.tris[t]
        n = norm(add(add(mul(self.normals[i], w[0]), mul(self.normals[j], w[1])), mul(self.normals[k], w[2])))
        return p, n, blend_skins([self.skins[i], self.skins[j], self.skins[k]], w)


def _ray_tri(o, d, a, b, c):
    """Möller-Trumbore: (distance, u, v) or None."""
    e1, e2 = sub(b, a), sub(c, a)
    p = cross(d, e2)
    det = dot(e1, p)
    if abs(det) < 1e-9:
        return None
    inv = 1.0 / det
    t = sub(o, a)
    u = dot(t, p) * inv
    if u < 0.0 or u > 1.0:
        return None
    q = cross(t, e1)
    v = dot(d, q) * inv
    if v < 0.0 or u + v > 1.0:
        return None
    dist = dot(e2, q) * inv
    return (dist, u, v) if dist > 0.0 else None


def _closest_on_tri(p, a, b, c):
    """The closest point on triangle abc to p, and its barycentric weights (for a, b, c)."""
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a, (1.0, 0.0, 0.0)
    bp = sub(p, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b, (0.0, 1.0, 0.0)
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return add(a, mul(ab, v)), (1.0 - v, v, 0.0)
    cp = sub(p, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c, (0.0, 0.0, 1.0)
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return add(a, mul(ac, w)), (1.0 - w, 0.0, w)
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return add(b, mul(sub(c, b), w)), (0.0, 1.0 - w, w)
    denom = 1.0 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return add(add(a, mul(ab, v)), mul(ac, w)), (1.0 - v - w, v, w)


class Welded:
    """A body's vertices welded by position (the glTF splits them along UV seams), with each welded point's
    neighbours and averaged normal: what the offset and smoothing work on, so the clothes have no cracks at seams."""

    def __init__(self, body):
        self.body = body
        key_of = {}
        self.group = []          # body vertex -> welded index
        self.members = []        # welded index -> body vertices
        for v, p in enumerate(body.pos):
            key = (round(p[0] * 50.0), round(p[1] * 50.0), round(p[2] * 50.0))   # 0.2 mm
            g = key_of.get(key)
            if g is None:
                g = key_of[key] = len(self.members)
                self.members.append([])
            self.group.append(g)
            self.members[g].append(v)
        self.points = [body.pos[m[0]] for m in self.members]
        self.skins = [body.skin[m[0]] for m in self.members]
        acc = [(0.0, 0.0, 0.0)] * len(self.points)
        for i, j, k in body.tris:
            a, b, c = self.group[i], self.group[j], self.group[k]
            n = cross(sub(self.points[b], self.points[a]), sub(self.points[c], self.points[a]))
            for g in (a, b, c):
                acc[g] = add(acc[g], n)
        self.normals = [norm(n) for n in acc]
        self.neighbours = [set() for _ in self.points]
        for i, j, k in body.tris:
            a, b, c = self.group[i], self.group[j], self.group[k]
            self.neighbours[a].update((b, c))
            self.neighbours[b].update((a, c))
            self.neighbours[c].update((a, b))

    def tris(self):
        return [(self.group[i], self.group[j], self.group[k]) for i, j, k in self.body.tris]


def inflate(welded, keep, offset_of, smooth_iters=0, min_offset=None, smooth_weight=0.5):
    """Points of the welded body pushed out along their normals by offset_of(g) (cm), then smoothed (Laplacian,
    over the kept points) while never going nearer the skin than min_offset(g). Returns {welded index: point}."""
    pts = {g: add(welded.points[g], mul(welded.normals[g], offset_of(g))) for g in keep}
    for _ in range(smooth_iters):
        new = {}
        for g, p in pts.items():
            nb = [pts[h] for h in welded.neighbours[g] if h in pts]
            if len(nb) < 3:
                new[g] = p
                continue
            avg = mul(sum_points(nb), 1.0 / len(nb))
            q = lerp(p, avg, smooth_weight)
            # Never closer to the skin than the minimum (along the skin's normal), so it never sinks in.
            lo = min_offset(g) if min_offset else offset_of(g) * 0.6
            out = dot(sub(q, welded.points[g]), welded.normals[g])
            if out < lo:
                q = add(q, mul(welded.normals[g], lo - out))
            new[g] = q
        pts = new
    return pts


def boundary_loops(tris):
    """The open edges of a triangle set, chained into loops (lists of point indices)."""
    count = {}
    for i, j, k in tris:
        for a, b in ((i, j), (j, k), (k, i)):
            key = (min(a, b), max(a, b))
            count[key] = count.get(key, 0) + 1
    nxt = {}
    for i, j, k in tris:
        for a, b in ((i, j), (j, k), (k, i)):
            if count[(min(a, b), max(a, b))] == 1:
                nxt.setdefault(a, []).append(b)
    loops = []
    used = set()
    for start in list(nxt):
        if start in used:
            continue
        loop = [start]
        used.add(start)
        cur = start
        while True:
            options = [n for n in nxt.get(cur, []) if n not in used or (n == start and len(loop) > 2)]
            if not options:
                break
            cur = options[0]
            if cur == start:
                break
            loop.append(cur)
            used.add(cur)
        if len(loop) >= 3:
            loops.append(loop)
    return loops


# --- A body's measurements -----------------------------------------------------------------------------------------

HAND_BONES = ("hand", "index", "middle", "ring", "pinky", "thumb")
FOOT_BONES = ("foot", "ball")


class Fit:
    """One body and the measurements the clothes and gear are cut to: joint positions, the head's shape (a map of
    the scalp's radius in every direction from the head's centre), where the eyes and chin are."""

    def __init__(self, path, name):
        self.name = name
        self.gltf = Gltf(path)
        self.rig = Rig(self.gltf)
        self.body = Body(self.gltf, self.rig)
        self.eyes_mesh = Body(self.gltf, self.rig, mesh_index=1)
        self.welded = Welded(self.body)
        j = self.rig.pos
        self.neck, self.head_joint, self.pelvis = j("neck_01"), j("Head"), j("pelvis")
        self.spine3, self.spine2, self.spine1 = j("spine_03"), j("spine_02"), j("spine_01")
        b = self.body
        head = [p for v, p in enumerate(b.pos) if b.weight_on(v, ["Head"]) > 0.6]
        self.head_lo = tuple(min(p[a] for p in head) for a in range(3))
        self.head_hi = tuple(max(p[a] for p in head) for a in range(3))
        eyes = self.eyes_mesh.pos
        self.eye = {}
        for s in (1, -1):
            half = [p for p in eyes if p[0] * s > 0]
            self.eye[s] = mul(sum_points(half), 1.0 / len(half))
        self.eye_y = self.eye[1][1]
        self.eye_front = max(p[2] for p in eyes)
        # The cranium's centre: midway between the eyes and the crown, the middle of the skull's depth.
        skull = [p for p in head if p[1] > self.eye_y - 4.0]
        self.head_c = (0.0, (self.eye_y + self.head_hi[1]) * 0.5 - 1.0,
                       (min(p[2] for p in skull) + max(p[2] for p in skull)) * 0.5)
        front = [p for p in head if abs(p[0]) < 1.5 and p[2] > self.head_c[2]]
        self.chin_y = min(p[1] for p in front)
        self.chin_z = max(p[2] for p in front if p[1] < self.chin_y + 2.5)
        nose = max(front, key=lambda p: p[2])
        self.nose_y, self.nose_z = nose[1], nose[2]
        self._scalp(head)
        self.height = max(p[1] for p in b.pos)
        self.scale = self.height / 181.0
        chest = [p for p in b.pos if abs(p[1] - self.spine2[1]) < 3.0 and abs(p[0]) < 25.0]
        self.chest_half_w = max(abs(p[0]) for p in chest)
        self.shoulder_y = max(p[1] for v, p in enumerate(b.pos) if abs(abs(p[0]) - 12.0) < 2.0 and abs(p[2]) < 6.0
                              and b.weight_on(v, ["Head", "neck_01"]) < 0.3)

    # The scalp map: the head's outer radius from head_c in each direction (azimuth 0 = forward, +90 = left;
    # elevation up), so hats and helmets can be shaped to any head.
    AZ, EL = 48, 24

    def _scalp(self, head):
        grid = [[0.0] * self.EL for _ in range(self.AZ)]
        for p in head:
            d = sub(p, self.head_c)
            az, el = self._dir_of(d)
            i, k = self._cell(az, el)
            grid[i][k] = max(grid[i][k], length(d))
        # Directions with no vertices (under the jaw, into the neck) are filled from their neighbours.
        for _ in range(12):
            for i in range(self.AZ):
                for k in range(self.EL):
                    if grid[i][k] == 0.0:
                        nb = [grid[(i + di) % self.AZ][min(self.EL - 1, max(0, k + dk))] for di, dk in ((1, 0), (-1, 0), (0, 1), (0, -1))]
                        nb = [x for x in nb if x > 0.0]
                        if nb:
                            grid[i][k] = sum(nb) / len(nb)
        self.scalp_grid = grid

    def _dir_of(self, d):
        r = length(d)
        return math.atan2(d[0], d[2]), (math.asin(max(-1.0, min(1.0, d[1] / r))) if r > 1e-6 else 0.0)

    def _cell(self, az, el):
        i = int((az + math.pi) / (2.0 * math.pi) * self.AZ) % self.AZ
        k = min(self.EL - 1, max(0, int((el + math.pi / 2) / math.pi * self.EL)))
        return i, k

    def scalp(self, az, el, grid=None):
        """The scalp's radius (cm from head_c) toward (az, el), interpolated (or a smoothed map's, given grid)."""
        g = grid or self.scalp_grid
        fi = (az + math.pi) / (2.0 * math.pi) * self.AZ - 0.5
        fk = (el + math.pi / 2) / math.pi * self.EL - 0.5
        i0, k0 = int(math.floor(fi)), int(math.floor(fk))
        ti, tk = fi - i0, fk - k0

        def at(i, k):
            return g[i % self.AZ][min(self.EL - 1, max(0, k))]
        return (at(i0, k0) * (1 - ti) * (1 - tk) + at(i0 + 1, k0) * ti * (1 - tk)
                + at(i0, k0 + 1) * (1 - ti) * tk + at(i0 + 1, k0 + 1) * ti * tk)

    def smoothed_scalp(self, passes, dilate=1):
        """The scalp map grown by `dilate` cells (the widest radius nearby) and smoothed: a shell over it clears the
        ears and reads as one smooth shape."""
        g = [row[:] for row in self.scalp_grid]
        for _ in range(dilate):
            g = [[max(g[(i + di) % self.AZ][min(self.EL - 1, max(0, k + dk))] for di in (-1, 0, 1) for dk in (-1, 0, 1))
                  for k in range(self.EL)] for i in range(self.AZ)]
        for _ in range(passes):
            g = [[(g[i][k] * 2 + g[(i + 1) % self.AZ][k] + g[(i - 1) % self.AZ][k] + g[i][min(self.EL - 1, k + 1)] + g[i][max(0, k - 1)]) / 6.0
                  for k in range(self.EL)] for i in range(self.AZ)]
        return g

    def head_dir(self, az, el):
        return (math.sin(az) * math.cos(el), math.sin(el), math.cos(az) * math.cos(el))

    def head_point(self, az, el, r):
        return add(self.head_c, mul(self.head_dir(az, el), r))

    def brim_y(self, az, front, side, back):
        """A hat line round the head through these heights at the front, sides and back (cm)."""
        b = (front - back) * 0.5
        c = ((front + back) * 0.5 - side) * 0.5
        a = side + c
        return a + b * math.cos(az) + c * math.cos(2.0 * az)

    def el_at_height(self, az, y, grid=None, extra=0.0):
        """The elevation where the head (or a shell given by grid, extra cm out) reaches height y at azimuth az."""
        lo, hi = -1.3, 1.55
        for _ in range(30):
            mid = (lo + hi) * 0.5
            r = self.scalp(az, mid, grid) + extra
            if self.head_c[1] + r * math.sin(mid) < y:
                lo = mid
            else:
                hi = mid
        return (lo + hi) * 0.5

    def rigid(self, bone):
        s = {self.rig.index[bone]: 1.0}
        return lambda p: s


# --- Clothes from the body ------------------------------------------------------------------------------------------


def smooth_edges(pts, tris, iterations=10):
    """The open edges of a garment cut from the body follow its triangles in steps: each edge point is drawn toward
    its neighbours along the edge until the edge runs smoothly (a straight cuff, a clean collar line)."""
    for loop in boundary_loops(tris):
        n = len(loop)
        for _ in range(iterations):
            moved = [lerp(pts[loop[i]], mul(add(pts[loop[i - 1]], pts[loop[(i + 1) % n]]), 0.5), 0.5) for i in range(n)]
            for i, g in enumerate(loop):
                pts[g] = moved[i]


def _mesh_from_points(m, welded, pts, keep_tris, material, colour_of=None, uv_of=None, edge_smoothing=10):
    """Adds welded triangles (all corners in pts) to the mesh, with normals from their new shape, its cut edges
    smoothed. Returns the Surface of what was added (for laying more on it) and {welded index: mesh vertex}."""
    tris = [t for t in keep_tris if t[0] in pts and t[1] in pts and t[2] in pts]
    if edge_smoothing:
        pts = dict(pts)
        smooth_edges(pts, tris, edge_smoothing)
    used = sorted({g for t in tris for g in t})
    local = {g: i for i, g in enumerate(used)}
    points = [pts[g] for g in used]
    lt = [(local[a], local[b], local[c]) for a, b, c in tris]
    surf = Surface(points, lt, [welded.skins[g] for g in used])
    ids = {}
    for g, p, n in zip(used, points, surf.normals):
        colour = colour_of(g, p, n) if colour_of else (1.0, 1.0, 1.0, 1.0)
        uv = uv_of(g, p, n) if uv_of else (p[0] / 10.0, p[1] / 10.0)
        ids[g] = m.vert(p, n, surf.skins[local[g]], uv, colour)
    for a, b, c in tris:
        m.tri(material, ids[a], ids[b], ids[c])
    return surf, ids


def hem(m, surf, loop, along, width, out, material, tuck=0.3, colour=(1.0, 1.0, 1.0, 1.0)):
    """A band round an open edge of a surface (a cuff, an eye slot, a boot top): from just past the edge, out from
    the surface, and back along it by width. along(p) is the unit direction from the edge into the garment."""
    rows = [[], [], [], []]
    for v in loop:
        p, n, a = surf.points[v], surf.normals[v], along(surf.points[v])
        rows[0].append(add(p, mul(a, -tuck)))
        rows[1].append(add(add(p, mul(a, -tuck)), mul(n, out)))
        rows[2].append(add(add(p, mul(a, width)), mul(n, out)))
        rows[3].append(add(p, mul(a, width + 0.4)))
    pts = [surf.points[v] for v in loop]

    def nearest(q):
        return min(range(len(pts)), key=lambda k: dot(sub(pts[k], q), sub(pts[k], q)))
    skin_of = lambda q: surf.skins[loop[nearest(q)]]
    # Facing away from the band's core, just under its outer face.
    core = lambda q: sub(q, mul(surf.normals[loop[nearest(q)]], out * 0.5 + 0.2))
    m.grid(rows, material, skin_of, outward_hint=core, close_rows=True, colour=colour)


def build_uniform(fit):
    """The combat uniform: the body from the neck to the wrists and ankles, pushed out about a centimetre and
    smoothed into cloth (the muscles under it soften away), keeping the body's skin weights. A collar and cuffs
    close its edges (the trouser hems are inside the boots); a belt, cargo pockets, sleeve pockets and reinforced
    knees and elbows are laid on it. Returns the mesh and its Surface (for the vests to sit on)."""
    w, rig = fit.welded, fit.rig
    hand_x = rig.pos("hand_l")[0]
    neck = fit.neck
    hem_y = 15.0 * fit.scale

    def keep_point(p):
        # Below the collar line (lower at the front, like a shirt's), short of the wrists, and down to the ankles.
        # The collar line only cuts round the neck: out on the shoulders everything is kept.
        collar = neck[1] + 1.5 - 4.0 * smoothstep(-2.0, 7.0, p[2] - neck[2]) + 20.0 * smoothstep(7.0, 12.0, abs(p[0]))
        return p[1] < collar and abs(p[0]) < hand_x - 1.2 and p[1] > hem_y

    keep = {g for g, p in enumerate(w.points) if keep_point(p)
            and not w.body.dominant(w.members[g][0]).startswith(FOOT_BONES + ("Head",))}

    def offset(g):
        p = w.points[g]
        if abs(p[0]) > rig.pos("upperarm_l")[0] - 2.0:
            return 0.85            # sleeves
        if p[1] < fit.pelvis[1] - 6.0:
            return 1.0             # trousers
        return 1.15                # the blouse over the torso
    pts = inflate(w, keep, offset, smooth_iters=7, min_offset=lambda g: 0.55, smooth_weight=0.55)
    m = SkinMesh()
    surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Uniform")
    for loop in boundary_loops(surf.tris):
        c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
        if c[1] > fit.shoulder_y - 8.0 and abs(c[0]) < 12.0:
            _collar(m, fit, surf, loop)
        elif abs(c[0]) > hand_x - 8.0:
            # The cuff runs on past the cut to just short of the wrist (the forearm's last ring of vertices can be
            # several centimetres up the arm).
            side = 1.0 if c[0] > 0 else -1.0
            hem(m, surf, loop, lambda p, s=side: (-s, 0.0, 0.0), 3.2, 0.45, "Uniform", tuck=max(0.3, hand_x - 1.8 - abs(c[0])))
    _uniform_details(m, fit, surf)
    return m, surf


def _collar(m, fit, surf, loop):
    """The collar stands up round the neck from the uniform's neck edge, leaning out a little."""
    up = norm(sub(fit.head_joint, fit.neck))
    axis_z = fit.neck[2] + 1.0
    rows = []
    for rise, out in ((-1.2, 0.0), (-0.6, 0.55), (2.6, 0.9), (3.2, 0.55), (3.0, 0.05)):
        row = []
        for v in loop:
            p = surf.points[v]
            radial = norm((p[0], 0.0, p[2] - axis_z))
            row.append(add(add(p, mul(up, rise)), mul(radial, out + 0.25 * max(0.0, rise) / 3.0)))
        rows.append(row)
    pts = [surf.points[v] for v in loop]
    skin_of = lambda q: surf.skins[loop[min(range(len(pts)), key=lambda k: dot(sub(pts[k], q), sub(pts[k], q)))]]
    m.grid(rows, "Uniform", skin_of, outward_hint=lambda q: (0.0, q[1], axis_z), close_rows=True)


def limb_patch(m, surf, a, b, centre_dir, half_angle, t0, t1, thick, material, rows=6, cols=8, lift=0.08,
               skin_of=None, colour=(1.0, 1.0, 1.0, 1.0), puff=0.65, uv_scale=None):
    """A pad laid on a surface round the axis a->b: from t0 to t1 along it, half_angle (radians) either side of
    centre_dir, thick cm proud of the surface with its edges rounded down (puff). Rays from the axis find the
    surface; each point takes the skin of the spot it sits on (or skin_of's). Returns the outer face's rows of
    points, or None if the surface wasn't found."""
    axis = norm(sub(b, a))
    cd = norm(sub(centre_dir, mul(axis, dot(centre_dir, axis))))
    hits = []
    for r in range(rows + 1):
        o = lerp(a, b, t0 + (t1 - t0) * r / rows)
        row = []
        for c in range(cols + 1):
            ang = -half_angle + 2.0 * half_angle * c / cols
            h = surf.raycast(o, rotate(cd, axis, ang), 40.0)
            if h is None:
                return None
            row.append(h)
        hits.append(row)
    outer, inner, skins = [], [], {}
    for r, row in enumerate(hits):
        orow, irow = [], []
        for c, (p, n, s) in enumerate(row):
            edge = r in (0, rows) or c in (0, cols)
            near = r in (1, rows - 1) or c in (1, cols - 1)
            t = thick * (puff if edge else (0.92 if near else 1.0))
            q, i = add(p, mul(n, lift + t)), add(p, mul(n, lift * 0.5))
            orow.append(q)
            irow.append(i)
            skins[q] = skins[i] = s
        outer.append(orow)
        inner.append(irow)

    def skin(q):
        if skin_of:
            return skin_of(q)
        return skins[q] if q in skins else skins[min(skins, key=lambda x: dot(sub(x, q), sub(x, q)))]
    hint = lambda q: sub(q, mul(norm(sub(q, lerp(a, b, (t0 + t1) * 0.5))), thick * 0.5 + 0.3))
    uvs = None
    if uv_scale:
        arc = 2.0 * half_angle * length(sub(hits[0][0][0], a))
        span = (t1 - t0) * length(sub(b, a))
        uvs = [[(arc * c / cols / uv_scale, span * r / rows / uv_scale) for c in range(cols + 1)] for r in range(rows + 1)]
    m.grid(outer, material, skin, outward_hint=hint, colour=colour, uvs=uvs)
    # Side walls down to the surface, all the way round.
    ring_o = outer[0] + [row[-1] for row in outer[1:]] + outer[-1][-2::-1] + [row[0] for row in outer[-2:0:-1]]
    ring_i = inner[0] + [row[-1] for row in inner[1:]] + inner[-1][-2::-1] + [row[0] for row in inner[-2:0:-1]]
    centre = lerp(lerp(outer[0][0], outer[-1][-1], 0.5), lerp(inner[0][0], inner[-1][-1], 0.5), 0.5)
    m.grid([ring_i, ring_o], material, skin, outward_hint=lambda q: centre, close_rows=True, colour=colour)
    return outer


def ring_band(m, surf, centre, axis_y0, axis_y1, out, material, skin_of=None, segments=40, colour=(1.0, 1.0, 1.0, 1.0),
              ang0=0.0, ang1=2.0 * math.pi, uv_scale=None):
    """A band round the body between two heights (a belt, a cummerbund): rays out from a vertical axis through
    centre find the surface, and the band stands out cm proud of it. A full circle unless ang0/ang1 are given."""
    full = abs(ang1 - ang0 - 2.0 * math.pi) < 1e-6
    count = segments if full else segments + 1
    angs = [ang0 + (ang1 - ang0) * k / (segments if not full else segments) for k in range(count)]
    rows_o, rows_i, skins = [], [], {}
    for y in (axis_y0, (axis_y0 + axis_y1) * 0.5, axis_y1):
        ro, ri = [], []
        for ang in angs:
            h = surf.raycast((centre[0], y, centre[2]), (math.sin(ang), 0.0, math.cos(ang)), 45.0)
            if h is None:
                return None
            p, n, s = h
            n = norm((n[0], 0.0, n[2]))
            o = add(p, mul(n, out * (1.0 if y not in (axis_y0, axis_y1) else 0.85)))
            i = add(p, mul(n, 0.1))
            ro.append(o)
            ri.append(i)
            skins[o] = skins[i] = s
        rows_o.append(ro)
        rows_i.append(ri)

    def skin(q):
        if skin_of:
            return skin_of(q)
        return skins[q] if q in skins else skins[min(skins, key=lambda x: dot(sub(x, q), sub(x, q)))]
    rows = [rows_i[0], rows_o[0], rows_o[1], rows_o[2], rows_i[2]]
    uvs = None
    if uv_scale:
        circ = [0.0]
        for k in range(1, count):
            circ.append(circ[-1] + length(sub(rows_o[1][k], rows_o[1][k - 1])))
        heights = [0.0, 0.0, (axis_y1 - axis_y0) * 0.5, axis_y1 - axis_y0, axis_y1 - axis_y0]
        uvs = [[(circ[k] / uv_scale, h / uv_scale) for k in range(count)] for h in heights]
    hint = lambda q: (centre[0], q[1], centre[2])
    m.grid(rows, material, skin, outward_hint=hint, close_rows=full, colour=colour, uvs=uvs)
    if not full:
        for k in (0, count - 1):
            m.fan([r[k] for r in rows], material, skin, outward_hint=lambda q, k=k: rows_o[1][min(count - 1, max(0, k + (1 if k == 0 else -1)))], colour=colour)
    return rows_o


def _uniform_details(m, fit, surf):
    rig = fit.rig
    for s in (1.0, -1.0):
        side = "l" if s > 0 else "r"
        thigh, calf, foot = rig.pos("thigh_" + side), rig.pos("calf_" + side), rig.pos("foot_" + side)
        out = (s, 0.0, 0.25)
        # A cargo pocket on the outside of each thigh, and its flap.
        limb_patch(m, surf, thigh, calf, out, math.radians(38), 0.36, 0.66, 0.9, "Uniform", rows=6, cols=7)
        limb_patch(m, surf, thigh, calf, out, math.radians(41), 0.31, 0.38, 1.25, "Uniform", rows=2, cols=7, puff=0.8)
        # Reinforced knees (red marks the panel for the material).
        limb_patch(m, surf, thigh, foot, (0.0, 0.0, 1.0), math.radians(55), 0.43, 0.56, 0.35, "Uniform", rows=4, cols=8,
                   colour=(1.0, 0.0, 0.0, 1.0))
        upper, lower, hand = rig.pos("upperarm_" + side), rig.pos("lowerarm_" + side), rig.pos("hand_" + side)
        # A pocket on each shoulder (facing out and up in the bind pose's T), and its flap.
        limb_patch(m, surf, upper, lower, (0.0, 1.0, 0.15), math.radians(40), 0.22, 0.6, 0.7, "Uniform", rows=5, cols=6)
        limb_patch(m, surf, upper, lower, (0.0, 1.0, 0.15), math.radians(43), 0.17, 0.24, 1.0, "Uniform", rows=2, cols=6, puff=0.8)
        limb_patch(m, surf, upper, hand, (0.0, 0.0, -1.0), math.radians(60), 0.42, 0.58, 0.3, "Uniform", rows=4, cols=8,
                   colour=(1.0, 0.0, 0.0, 1.0))
    # The belt round the waist, and its buckle.
    belt_y = fit.pelvis[1] + 7.0 * fit.scale
    centre = (0.0, belt_y, fit.pelvis[2] + 3.0)
    ring_band(m, surf, centre, belt_y - 2.2, belt_y + 2.2, 0.55, "Belt", skin_of=lambda q: surf.closest(q, 6.0)[2])
    front = surf.raycast(centre, (0.0, 0.0, 1.0), 40.0)
    if front:
        p, n, s = front
        m.box(add(p, mul(n, 1.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (3.0, 2.4, 0.45), "Metal",
              lambda q: s, bevel=0.3)


SOLE_H = 3.2           # the rubber sole's height at the forefoot (cm)
HEEL_H = 4.4           # and at the heel


def build_boots(fit):
    """Eight-inch tactical boots in the pattern of the standard issue hot-weather combat boot (a Belleville or
    Danner kind of boot), built on a last of their own rather than by inflating the body's feet: a closed shoe from
    a rounded toe cap over the instep to a tall heel counter, a straight shaft rising out of it round the ankle, a
    padded collar with a pull tab at the back, a tongue up the front with five eyelets and three speed hooks either
    side, laces criss-crossing and tied in a bow, and a lugged rubber outsole with a raised heel and toe spring.
    Every section is enlarged wherever the body's foot or calf would show through, so any body fits. Vertex colour
    marks the parts for the material: red the rubber, green the laces, blue the toe cap, heel counter, collar and
    tongue (a second leather)."""
    rig, body, s = fit.rig, fit.body, fit.scale
    floor = min(p[1] for p in body.pos)
    sole_h, heel_h = SOLE_H * s, HEEL_H * s
    # The last, in fractions of its length from the heel's back: (where along it, half width, height of the upper
    # over the sole); a closed shoe from the rounded toe to the heel counter.
    LAST = [(1.00, 1.3, 1.5), (0.96, 3.0, 2.6), (0.88, 4.2, 3.5), (0.74, 4.9, 4.2), (0.60, 4.7, 5.0), (0.47, 4.5, 6.1),
            (0.36, 4.3, 7.4), (0.24, 4.2, 8.4), (0.13, 3.8, 8.2), (0.05, 2.7, 6.4), (0.00, 0.9, 3.0)]
    # The shaft: (height over the floor, half width, half depth), rising out of the shoe round the ankle.
    SHAFT = [(5.5, 3.6, 4.4), (8.0, 4.2, 5.2), (10.5, 4.6, 5.7), (13.5, 4.6, 5.5), (17.0, 4.7, 5.5), (19.5, 4.8, 5.6), (21.6, 4.9, 5.8)]
    POWER = 2.6   # the cross-section: a superellipse, flat underneath and across the instep, rounded at the sides
    m = SkinMesh()
    blue, red, green = (0.0, 0.0, 1.0, 1.0), (1.0, 0.0, 0.0, 1.0), (0.0, 1.0, 0.0, 1.0)
    leather = (0.0, 0.0, 0.0, 1.0)

    for side in (1.0, -1.0):
        name = "l" if side > 0 else "r"
        jf, jb, jc = rig.index["foot_" + name], rig.index["ball_" + name], rig.index["calf_" + name]
        foot_pts = [p for v, p in enumerate(body.pos) if p[0] * side > 0 and body.weight_on(v, ["foot_" + name, "ball_" + name]) > 0.5]
        z_min, z_max = min(p[2] for p in foot_pts), max(p[2] for p in foot_pts)
        cx = sum(p[0] for p in foot_pts) / len(foot_pts)
        leg_pts = [p for p in body.pos if p[0] * side > 0 and floor + 11.0 * s < p[1] < floor + 26.0 * s and abs(p[0] - cx) < 10.0]
        last_len = max(29.0 * s, z_max - z_min + 2.4)
        z0 = z_min - 1.2
        ankle_z = sum(p[2] for p in leg_pts) / len(leg_pts) if leg_pts else z0 + 0.3 * last_len
        a = (1.0, 0.0, 0.0)

        def sole_top(z):
            # Thicker under the heel, and the toe turned up a little (toe spring).
            heel = 1.0 - smoothstep(z0 + 0.26 * last_len, z0 + 0.36 * last_len, z)
            return floor + sole_h + (heel_h - sole_h) * heel + 1.4 * s * smoothstep(z0 + 0.74 * last_len, z0 + last_len, z)

        def ground(z):
            return floor + 0.05 + 1.4 * s * smoothstep(z0 + 0.74 * last_len, z0 + last_len, z)

        # The sections: centre, side axis a, "top" axis b (up over the foot, forward up the shin), half sizes, skin, part.
        shoe = []
        for f, w, h in LAST:
            z = z0 + f * last_len
            w, h = w * s, h * s
            slab = [p for p in foot_pts if abs(p[2] - z) < 1.5]
            if slab:
                w = max(w, max(abs(p[0] - cx) for p in slab) + 0.6)
                h = max(h, max(p[1] for p in slab) - sole_top(z) + 0.6)
            skin = {jb: 0.7, jf: 0.3} if f >= 0.74 else {jf: 1.0}
            shoe.append(((cx, sole_top(z) + h, z), a, (0.0, 1.0, 0.0), w, h, skin, "cap" if f >= 0.8 else "heel" if f <= 0.3 else "vamp"))
        shaft = []
        top_y = floor + SHAFT[-1][0] * s
        ankle_y = floor + 10.5 * s
        for height, w, d in SHAFT:
            y = floor + height * s
            w, d = w * s, d * s
            slab = [p for p in leg_pts if abs(p[1] - y) < 1.2]
            if slab:
                w = max(w, max(abs(p[0] - cx) for p in slab) + 1.0)
                d = max(d, max(abs(p[2] - ankle_z) for p in slab) + 1.0)
            t = max(0.0, (y - ankle_y) / max(top_y - ankle_y, 1.0))
            skin = normalise_skin({jf: 1.0 - 0.75 * t, jc: 0.75 * t})
            shaft.append(((cx, y, ankle_z), a, (0.0, 0.0, 1.0), w, d, skin, "heel" if height < 13.0 else "shaft"))
        sections = shoe + shaft

        def ring_of(sec, grow=1.0, lift=0.0):
            c, ax, bx, w, h, _, _ = sec
            pts = []
            for k in range(28):
                th = 2.0 * math.pi * k / 28
                ct, st = math.cos(th), math.sin(th)
                x = w * grow * math.copysign(abs(ct) ** (2.0 / POWER), ct)
                y = h * grow * math.copysign(abs(st) ** (2.0 / POWER), st)
                pts.append(add(add(add(c, mul(ax, x)), mul(bx, y)), (0.0, lift, 0.0)))
            return pts

        centres = [sec[0] for sec in sections]

        def nearest(q):
            return min(range(len(sections)), key=lambda i: dot(sub(centres[i], q), sub(centres[i], q)))

        def skin_of(q):
            return sections[nearest(q)][5]

        def hint(q):
            return centres[nearest(q)]

        def shell(secs):
            """One tube through these sections, coloured by part (the heel counter only at the back)."""
            rows = [ring_of(sec) for sec in secs]
            base = len(m.verts)
            m.grid(rows, "Boot", skin_of, outward_hint=hint, close_rows=True)
            for i, sec in enumerate(secs):
                for k in range(28):
                    v = base + i * 28 + k
                    p, n, uv, col, sk = m.verts[v]
                    behind = p[2] < ankle_z - 1.5 * s
                    col = blue if sec[6] == "cap" or (sec[6] == "heel" and behind) else leather
                    m.verts[v] = (p, n, uv, col, sk)
            return rows

        rows = shell(shoe)
        m.fan(rows[0], "Boot", skin_of, lambda q: centres[1], blue)
        m.fan(rows[-1], "Boot", skin_of, lambda q: centres[len(shoe) - 2], blue)
        rows = shell(shaft)

        # The collar: a padded roll round the top, turned in.
        top = shaft[-1]
        collar = [rows[-1], ring_of(top, 1.14, 1.0 * s), ring_of(top, 1.08, 2.2 * s), ring_of(top, 0.84, 2.0 * s)]
        m.grid(collar, "Boot", skin_of, outward_hint=lambda q: add(top[0], (0.0, 1.5 * s, 0.0)), close_rows=True, colour=blue)
        # The pull tab at the back.
        tab = add(add(top[0], mul(top[2], -(top[4] + 0.6 * s))), (0.0, 2.6 * s, 0.0))
        m.box(tab, ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (1.1 * s, 1.5 * s, 0.25), "Boot", skin_of, colour=blue, bevel=0.2)

        # The sole: the last's footprint plus a welt, from the ground up to the upper, with lugs round its edge.
        def half_w(z):
            f = (z - z0) / last_len
            if f < 0.13:
                return 3.7 * s * math.sqrt(max(0.0, 1.0 - ((0.13 - f) / 0.13) ** 2))
            for (f1, w1, _), (f0, w0, _) in zip(LAST[:-1], LAST[1:]):
                if f0 <= f <= f1:
                    return (w0 + (w1 - w0) * (f - f0) / (f1 - f0)) * s
            return 0.9 * s
        outline = []
        n_steps = 36
        for k in range(n_steps + 1):
            z = z0 + last_len * k / n_steps
            outline.append((cx + half_w(z) + 0.3 * s, z))
        for k in range(n_steps, -1, -1):
            z = z0 + last_len * k / n_steps
            outline.append((cx - half_w(z) - 0.3 * s, z))
        outline = _densify(outline, 1.2)
        cz = z0 + 0.5 * last_len
        lugged = [(cx + (x - cx) * (1.0 if i % 2 == 0 else 0.982), cz + (z - cz) * (1.0 if i % 2 == 0 else 0.99)) for i, (x, z) in enumerate(outline)]
        sole_bottom = [(x, ground(z), z) for x, z in lugged]
        sole_mid = [(x, ground(z) + 1.1 * s, z) for x, z in lugged]
        sole_upper = [(x, sole_top(z) + 0.3, z) for x, z in outline]
        sole_skin = lambda q: {jb: 0.6, jf: 0.4} if q[2] > z0 + 0.7 * last_len else {jf: 1.0}
        m.grid([sole_bottom, sole_mid, sole_upper], "Sole", sole_skin, outward_hint=lambda q: (cx, q[1], cz), close_rows=True, colour=red)
        m.fan(sole_bottom, "Sole", sole_skin, lambda q: add(q, (0.0, 3.0, 0.0)), red)
        m.fan(sole_upper, "Sole", sole_skin, lambda q: add(q, (0.0, -3.0, 0.0)), red)

        # The lace line: over the instep and up the shin to the collar, one smooth curve. The tongue is a padded
        # panel along it standing a little proud; the eyelets and hooks sit either side of it.
        line_secs = [sec for sec in shoe if 0.3 < (sec[0][2] - z0) / last_len <= 0.6] + [sec for sec in shaft if sec[0][1] > floor + 9.0 * s]
        front = [add(sec[0], mul(sec[2], sec[4])) for sec in line_secs]
        line = catmull(front, 6)
        ups = [norm(sub(p, sections[nearest(p)][0])) for p in line]     # out of the boot there
        rows = []
        for p, up in zip(line, ups):
            half = 2.4 * s
            rows.append([add(p, mul(a, half)), add(add(p, mul(a, half * 0.85)), mul(up, 0.45 * s)),
                         add(add(p, mul(a, -half * 0.85)), mul(up, 0.45 * s)), add(p, mul(a, -half))])
        m.grid(rows, "Boot", skin_of, outward_hint=hint, close_rows=True, colour=blue)

        # Eyelets (five) and speed hooks (three) either side of the tongue, the laces crossing between them.
        arc = [0.0]
        for i in range(1, len(line)):
            arc.append(arc[-1] + length(sub(line[i], line[i - 1])))
        holes = {1.0: [], -1.0: []}
        count = 8
        for k in range(count):
            want = arc[-1] * (0.06 + 0.88 * k / (count - 1))
            i = min(range(len(arc)), key=lambda j: abs(arc[j] - want))
            p = line[i]
            sec = sections[nearest(p)]
            off = min(3.3 * s, sec[3] * 0.72)
            for s2 in (1.0, -1.0):
                # Slide across the boot and back onto its surface: the superellipse's radius in that direction.
                radial = norm(sub(add(p, mul(a, s2 * off)), sec[0]))
                rx, ry = dot(radial, a) / sec[3], dot(radial, sec[2]) / sec[4]
                rr = (abs(rx) ** POWER + abs(ry) ** POWER) ** (-1.0 / POWER)
                q = add(sec[0], mul(radial, rr))
                n = radial
                holes[s2].append((q, n))
                if k < 5:
                    ring_pts = [add(add(q, mul(n, 0.2)), add(mul(norm(cross(n, a)), 0.4 * s * math.cos(2 * math.pi * j / 8)),
                                                           mul(a, 0.4 * s * math.sin(2 * math.pi * j / 8)))) for j in range(8)]
                    m.tube(ring_pts + [ring_pts[0]], 0.12, "Sole", skin_of, sides=4, caps=False)
                else:
                    hook_up = norm(cross(a, n))
                    m.box(add(q, mul(n, 0.45)), (a, hook_up, n), (0.55 * s, 0.3 * s, 0.45), "Sole", skin_of, bevel=0.1)
        for k in range(count - 1):
            for s2 in (1.0, -1.0):
                p0, n0 = holes[s2][k]
                p1, n1 = holes[-s2][k + 1]
                mid = lerp(p0, p1, 0.5)
                path = catmull([add(p0, mul(n0, 0.3)), add(mid, mul(norm(add(n0, n1)), 1.0 * s)), add(p1, mul(n1, 0.3))], 4)
                m.tube(path, 0.2, "Lace", skin_of, sides=5, colour=green)
        # The bow, above the top hooks.
        (tl, nl), (tr, nr) = holes[1.0][-1], holes[-1.0][-1]
        kn = norm(add(nl, nr))
        knot = add(lerp(tl, tr, 0.5), mul(kn, 0.9 * s))
        out_dir = norm(sub(tl, tr))
        down = (0.0, -1.0, 0.0)
        m.box(knot, (out_dir, norm(cross(kn, out_dir)), kn), (0.45, 0.35, 0.3), "Lace", skin_of, colour=green, bevel=0.15)
        for s2 in (1.0, -1.0):
            o = mul(out_dir, s2)
            loop = [knot, add(add(knot, mul(o, 1.6)), add(mul(down, -0.6), mul(kn, 0.6))), add(add(knot, mul(o, 2.6)), mul(down, 0.3)),
                    add(add(knot, mul(o, 1.7)), add(mul(down, 1.0), mul(kn, 0.4))), knot]
            m.tube(catmull(loop, 4), 0.2, "Lace", skin_of, sides=5, caps=False, colour=green)
            end = [knot, add(add(knot, mul(o, 0.9)), add(mul(down, 1.6), mul(kn, 0.5))), add(add(knot, mul(o, 1.3)), add(mul(down, 3.8), mul(kn, 0.7)))]
            m.tube(catmull(end, 4), 0.2, "Lace", skin_of, sides=5, colour=green)
    return m


def _hull2d(points):
    """The convex hull of 2D points, counter-clockwise (Andrew's monotone chain)."""
    pts = sorted(set((round(x, 3), round(y, 3)) for x, y in points))
    if len(pts) < 3:
        return pts

    def turn(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and turn(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and turn(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def build_gloves(fit):
    """Tactical gloves: the hands and wrists pushed out a few millimetres. Vertex colour red marks the palm (a
    darker grippy panel); a hard knuckle guard sits on the back of each hand."""
    w, rig = fit.welded, fit.rig
    hand_x = rig.pos("hand_l")[0]
    keep = {g for g, p in enumerate(w.points) if abs(p[0]) > hand_x - 5.5}
    pts = inflate(w, keep, lambda g: 0.32, smooth_iters=2, min_offset=lambda g: 0.22, smooth_weight=0.3)
    m = SkinMesh()
    surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Glove", colour_of=lambda g, p, n: (smoothstep(-0.2, -0.5, n[1]), 0.0, 0.0, 1.0))
    for s in (1.0, -1.0):
        side = "l" if s > 0 else "r"
        knuckles = mul(sum_points([rig.pos(f + "_01_" + side) for f in ("index", "middle", "ring", "pinky")]), 0.25)
        h = surf.raycast(sub(knuckles, (s * 1.5, 1.0, 0.0)), (0.0, 1.0, 0.0), 10.0)
        if h:
            p, n, _ = h
            m.box(add(p, mul(n, 0.5)), ((1.0, 0.0, 0.0), n, norm(cross((1.0, 0.0, 0.0), n))), (1.5, 0.45, 3.8), "GloveGuard",
                  fit.rigid("hand_" + side), bevel=0.35)
    return m


def build_balaclava(fit):
    """A knitted balaclava over the head and neck, with a slot for the eyes, tucked into the collar."""
    w, b = fit.welded, fit.body
    collar = fit.neck[1] - 4.0

    def keep_point(g):
        p, v = w.points[g], w.members[g][0]
        return (b.weight_on(v, ["Head", "neck_01"]) > 0.35 or (p[1] > collar and abs(p[0]) < 11.0 and abs(p[2] - fit.neck[2]) < 10.0)) \
            and p[1] > collar - 2.0
    keep = {g for g in range(len(w.points)) if keep_point(g)}
    pts = inflate(w, keep, lambda g: 0.45, smooth_iters=4, min_offset=lambda g: 0.32, smooth_weight=0.45)
    half = abs(fit.eye[1][0]) + 2.7

    def in_slot(c):
        return abs(c[0]) < half and abs(c[1] - fit.eye_y - 0.4) < 2.3 and c[2] > fit.head_c[2] + 2.0
    tris = [t for t in fit.welded.tris() if all(g in pts for g in t)
            and not in_slot(mul(add(add(w.points[t[0]], w.points[t[1]]), w.points[t[2]]), 1.0 / 3.0))]
    m = SkinMesh()
    uv = lambda g, p, n: (math.atan2(p[0], p[2] - fit.head_c[2]) * 6.0, p[1] / 0.6)
    surf, _ = _mesh_from_points(m, w, pts, tris, "Knit", uv_of=uv)
    for loop in boundary_loops(surf.tris):
        c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
        if c[1] > fit.eye_y - 4.0:
            # The eye slot's rolled edge.
            hem(m, surf, loop, lambda p, c=c: norm((p[0] - c[0], p[1] - c[1], 0.0)), 0.8, 0.3, "Knit", tuck=0.1)
    return m


def build_shemagh(fit):
    """A shemagh wrapped round the neck and over the face to the nose, its folds bunched thicker toward the neck and
    shoulders. Its check pattern comes from the UVs."""
    w, b = fit.welded, fit.body
    top_front, top_back = fit.nose_y + 0.8, fit.head_c[1] - 5.0
    neck_base = fit.neck[1] - 7.0

    def keep_point(g):
        p, v = w.points[g], w.members[g][0]
        top = top_back + (top_front - top_back) * smoothstep(fit.head_c[2] - 2.0, fit.head_c[2] + 4.0, p[2])
        head_part = b.weight_on(v, ["Head", "neck_01"]) > 0.3 and p[1] < top
        shoulders = neck_base < p[1] < fit.neck[1] + 1.0 and length((p[0], 0.0, p[2] - fit.neck[2] - 1.0)) < 13.0 and abs(p[0]) < 13.0
        return head_part or shoulders
    keep = {g for g in range(len(w.points)) if keep_point(g)}

    def offset(g):
        p = w.points[g]
        ang = math.atan2(p[0], p[2] - fit.neck[2])
        bulk = 1.0 + 1.4 * (1.0 - smoothstep(fit.neck[1] - 6.0, fit.chin_y + 2.0, p[1]))
        return bulk + 0.55 * (0.5 + 0.5 * math.sin(ang * 9.0 + p[1] * 0.35))
    pts = inflate(w, keep, offset, smooth_iters=3, min_offset=lambda g: 0.7, smooth_weight=0.35)
    m = SkinMesh()
    uv = lambda g, p, n: (math.atan2(p[0], p[2] - fit.neck[2]) * 2.2, p[1] / 6.0)
    surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Shemagh", uv_of=uv)
    for loop in boundary_loops(surf.tris):
        c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
        down = c[1] > fit.chin_y
        hem(m, surf, loop, lambda p, d=down: (0.0, -1.0 if d else 1.0, 0.0), 1.2, 0.5, "Shemagh", tuck=0.2)
    return m


# --- Hair ------------------------------------------------------------------------------------------------------------

# The line headgear covers: everything of the hair above it is pressed onto the scalp in the "_Hat" variants.
def hat_line(fit, az):
    return fit.brim_y(az, fit.eye_y + 3.0, fit.head_c[1] - 1.5, fit.head_c[1] - 6.5)


def build_hair(fit, hair_path, female, hat=False):
    """A hairstyle (made for the head of `female`, a Fit; the body's own when it's MakeHuman's) refitted to this
    body's head: carried by the head joint, scaled to the head's size, and kept just outside the scalp. With hat,
    everything above the hat line is pressed down onto the scalp so headgear sits over it (long hair still hangs
    below)."""
    g = Gltf(hair_path)
    hr = Rig(g)
    hair = Body(g, fit.rig)
    # From the hair file's head joint to this body's (both in cm): the hairstyle moves as if glued to the head.
    to_head = mat_mul(fit.rig.matrix("Head"), mat_inverse_affine(hr.matrix("Head")))
    sx = (fit.head_hi[0] - fit.head_lo[0]) / (female.head_hi[0] - female.head_lo[0])
    sy = (fit.head_hi[1] - fit.eye_y) / (female.head_hi[1] - female.eye_y)
    sz = (fit.head_hi[2] - fit.head_lo[2]) / (female.head_hi[2] - female.head_lo[2])
    # Its material slot: MakeHuman hair carries its own ("Hair_<style>"); Quaternius's keeps its texture set,
    # "Hair1" or "Hair2" (MI_Hair_1 / MI_Hair_2).
    source = g.json["materials"][g.json["meshes"][0]["primitives"][0]["material"]]["name"]
    slot = source if source.startswith("Hair_") else "Hair2" if source.endswith("2") else "Hair1"
    m = SkinMesh()
    for p, n, uv, s in zip(hair.pos, hair.nrm, hair.uv, hair.skin):
        d = sub(mat_apply(to_head, p), fit.head_c)
        d = (d[0] * sx, d[1] * sy, d[2] * sz)
        az, el = fit._dir_of(d)
        r, rs = length(d), fit.scalp(az, el)
        if el > -0.35:
            # Kept just outside the scalp (only over the skull: below it the map is guessed from its neighbours).
            r = max(r, rs + 0.2)
        q = add(fit.head_c, mul(norm(d), r))
        if hat and q[1] > hat_line(fit, az):
            r = min(r, rs + 0.5)
            q = add(fit.head_c, mul(norm(d), r))
        m.vert(q, norm(mat_apply_dir(to_head, n)), s, uv)
    for i, j, k in hair.tris:
        m.tri(slot, i, j, k)
    return m


# --- Paths laid on a surface ---------------------------------------------------------------------------------------


def catmull(points, per_segment):
    """A smooth curve through the points (Catmull-Rom), per_segment points between each pair."""
    out = []
    ext = [points[0]] + list(points) + [points[-1]]
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for s in range(per_segment):
            t = s / per_segment
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[a] + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2
                                    + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3) for a in range(3)))
    out.append(points[-1])
    return out


def lay(surf, points, lift, per_segment=6, allowed=None, rig=None):
    """A smooth path through the points, pressed onto the surface and lifted off it: (points, normals, skins)."""
    pts, nrm, skins = [], [], []
    for q in catmull(points, per_segment):
        p, n, s = surf.closest(q, 15.0)
        pts.append(add(p, mul(n, lift)))
        nrm.append(n)
        skins.append(restrict(s, allowed, rig) if allowed else s)
    return pts, nrm, skins


def restrict(skin, allowed, rig):
    """A skin kept to the allowed bones (renormalised), or all on the first of them if none of its weight is."""
    ids = {rig.index[b] for b in allowed}
    pairs = {j: w for j, w in skin.items() if j in ids}
    return normalise_skin(pairs) if pairs else {rig.index[allowed[0]]: 1.0}


def surface_of(*meshes):
    """One Surface over several generated meshes (to lay straps over a vest as well as the uniform)."""
    points, tris, skins = [], [], []
    for m in meshes:
        if isinstance(m, Surface):
            base = len(points)
            points += m.points
            skins += m.skins
            tris += [(a + base, b + base, c + base) for a, b, c in m.tris]
            continue
        base = len(points)
        points += [v[0] for v in m.verts]
        skins += [v[4] for v in m.verts]
        for faces in m.faces.values():
            tris += [(a + base, b + base, c + base) for a, b, c in faces]
    return Surface(points, tris, skins)


def strap_on(m, path, width, thick, material, colour=(1.0, 1.0, 1.0, 1.0)):
    pts, nrm, skins = path

    def skin_of(q):
        i = min(range(len(pts)), key=lambda k: dot(sub(pts[k], q), sub(pts[k], q)))
        return skins[i]
    m.strap(pts, nrm, width, thick, material, skin_of, colour)


# --- Headgear ------------------------------------------------------------------------------------------------------


def _shell(m, fit, grid, extra, thick, bottom_y, material, skin, inner_material=None, rows=12, az_steps=48, top_cap=True,
           bulge=None, flatten_y=None):
    """A shell over the head (a helmet, a hat's crown): extra cm out from a smoothed scalp map, thick cm thick,
    from the line bottom_y(az) to the crown. Returns (outer rows, inner rows); row 0 is the bottom edge."""
    outer, inner = [], []
    els = []
    for i in range(az_steps):
        az = -math.pi + 2.0 * math.pi * i / az_steps
        els.append((az, fit.el_at_height(az, bottom_y(az), grid, extra)))
    for k in range(rows + 1):
        ro, ri = [], []
        t = k / rows
        for az, el0 in els:
            el = el0 + (1.50 - el0) * (1.0 - (1.0 - t) ** 1.15)
            r = fit.scalp(az, el, grid) + extra + (bulge(t) if bulge else 0.0)
            q = fit.head_point(az, el, r)
            if flatten_y is not None and q[1] > flatten_y:
                q = (q[0], flatten_y + (q[1] - flatten_y) * 0.35, q[2])
            ri.append(q)
            ro.append(add(q, mul(fit.head_dir(az, el), thick)))
        outer.append(ro)
        inner.append(ri)
    centre = fit.head_c
    m.grid(outer, material, skin, outward_hint=lambda q: centre, close_rows=True)
    m.grid(inner, inner_material or material, skin, outward_hint=lambda q: add(q, sub(q, centre)), close_rows=True)
    m.grid([inner[0], outer[0]], material, skin, outward_hint=lambda q: add(q, (0.0, 3.0, 0.0)), close_rows=True)
    if top_cap:
        m.fan(outer[-1], material, skin, lambda q: centre)
        m.fan(inner[-1], inner_material or material, skin, lambda q: add(q, (0.0, 5.0, 0.0)))
    return outer, inner, els


def _shell_patch(m, fit, grid, extra, az0, az1, el0, el1, lift, thick, material, skin, rows=4, cols=8):
    """A pad on a shell (velcro, a rail mount): its outer face lift+thick out from the shell, walls down to it."""
    outer, inner = [], []
    for r in range(rows + 1):
        el = el0 + (el1 - el0) * r / rows
        ro, ri = [], []
        for c in range(cols + 1):
            az = az0 + (az1 - az0) * c / cols
            base = fit.scalp(az, el, grid) + extra
            ro.append(fit.head_point(az, el, base + lift + thick))
            ri.append(fit.head_point(az, el, base + lift * 0.3))
        outer.append(ro)
        inner.append(ri)
    centre = fit.head_c
    m.grid(outer, material, skin, outward_hint=lambda q: centre)
    ring_o = outer[0] + [row[-1] for row in outer[1:]] + outer[-1][-2::-1] + [row[0] for row in outer[-2:0:-1]]
    ring_i = inner[0] + [row[-1] for row in inner[1:]] + inner[-1][-2::-1] + [row[0] for row in inner[-2:0:-1]]
    mid_el, mid_az = (el0 + el1) * 0.5, (az0 + az1) * 0.5
    mid = fit.head_point(mid_az, mid_el, fit.scalp(mid_az, mid_el, grid) + extra + lift + thick * 0.5)
    m.grid([ring_i, ring_o], material, skin, outward_hint=lambda q: mid, close_rows=True)


def head_surface(fit):
    if not hasattr(fit, "_head_surface"):
        w, b = fit.welded, fit.body
        heady = {g for g in range(len(w.points)) if b.weight_on(w.members[g][0], ["Head", "neck_01"]) > 0.3}
        tris = [t for t in w.tris() if all(g in heady for g in t)]
        fit._head_surface = Surface(w.points, tris, w.skins, w.normals)
    return fit._head_surface


def build_helmet(fit):
    """A high-cut ballistic helmet: a shell fitted over the head (on pads, clear of hair pressed under it), cut high
    over the ears, with a rubber edge trim, an NVG shroud at the front, accessory rails down both sides, velcro
    panels on the crown and back, and a chinstrap harness. Rigid on the head."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    grid = fit.smoothed_scalp(passes=8, dilate=2)
    extra, thick = 1.8, 0.9
    line = lambda az: fit.brim_y(az, fit.eye_y + 3.8, fit.head_c[1] + 0.8, fit.head_c[1] - 5.5)
    outer, inner, els = _shell(m, fit, grid, extra, thick, line, "Gear", skin, inner_material="Pad")
    el_of = dict(els)
    # Edge trim round the rim.
    rim = [lerp(o, i, 0.5) for o, i in zip(outer[0], inner[0])]
    m.tube(rim + rim[:2], thick * 0.7, "Trim", skin, sides=6, caps=False)
    shell = extra + thick

    def at(az, el, out=0.0):
        return fit.head_point(az, el, fit.scalp(az, el, grid) + shell + out), fit.head_dir(az, el)

    def el_bottom(az):
        return fit.el_at_height(az, line(az), grid, extra)
    # The NVG shroud: a plate on the brow with a socket in it.
    el = el_bottom(0.0) + 0.2
    p, n = at(0.0, el, 0.55)
    up = norm(sub(at(0.0, el + 0.05)[0], at(0.0, el - 0.05)[0]))
    side = norm(cross(up, n))
    m.box(p, (side, up, n), (3.6, 2.2, 0.55), "Mount", skin, bevel=0.35)
    m.box(add(p, mul(n, 0.75)), (side, up, n), (1.4, 0.9, 0.35), "Mount", skin, bevel=0.15)
    # Accessory rails along each side, just above the cut.
    for s in (1.0, -1.0):
        path, nrm = [], []
        for k in range(12):
            az = s * (0.95 + 1.4 * k / 11)
            q, d = at(az, el_bottom(az) + 0.17, 0.05)
            path.append(q)
            nrm.append(d)
        m.strap(path, nrm, 2.1, 0.75, "Mount", skin)
        # A bungee hook at each rail's back.
        q, d = at(s * 2.45, el_bottom(s * 2.45) + 0.28, 0.0)
        m.box(add(q, mul(d, 0.5)), (norm(cross((0.0, 1.0, 0.0), d)), (0.0, 1.0, 0.0), d), (0.8, 1.2, 0.45), "Mount", skin, bevel=0.2)
    # Velcro: a big panel on the crown, one on the back.
    _shell_patch(m, fit, grid, shell, -0.65, 0.65, 0.95, 1.32, 0.05, 0.12, "Velcro", skin, rows=3, cols=8)
    _shell_patch(m, fit, grid, shell, math.pi - 0.55, math.pi + 0.55, el_bottom(math.pi) + 0.18, el_bottom(math.pi) + 0.6, 0.05, 0.12,
                 "Velcro", skin, rows=3, cols=8)
    # Chinstrap harness: front and rear straps from inside the rim, meeting under the jaw, joined under the chin.
    hs = head_surface(fit)
    half_w = (fit.head_hi[0] - fit.head_lo[0]) * 0.5
    jaw = lambda s: (s * 4.6, fit.chin_y + 2.4, fit.chin_z - 3.6)
    for s in (1.0, -1.0):
        front = [at(s * 1.35, el_bottom(s * 1.35) + 0.05, -thick - 0.4)[0],
                 (s * (half_w + 0.2), fit.eye_y - 3.2, fit.head_c[2] + 3.2), jaw(s),
                 (s * 1.8, fit.chin_y - 0.3, fit.chin_z - 2.2), (0.0, fit.chin_y - 0.5, fit.chin_z - 2.0)]
        strap_on(m, lay(hs, front, 0.35, per_segment=5), 1.5, 0.22, "Strap")
        rear = [at(s * 2.1, el_bottom(s * 2.1) + 0.05, -thick - 0.4)[0],
                (s * (half_w + 0.1), fit.eye_y - 5.0, fit.head_c[2] - 2.0), jaw(s)]
        strap_on(m, lay(hs, rear, 0.35, per_segment=5), 1.5, 0.22, "Strap")
        p, n, _ = hs.closest(jaw(s), 10.0)
        m.box(add(p, mul(n, 0.6)), ((0.0, 1.0, 0.0), norm(cross(n, (0.0, 1.0, 0.0))), n), (1.3, 1.0, 0.35), "Strap", skin, bevel=0.2)
    return m


def build_boonie(fit):
    """A boonie hat in the uniform's camouflage: a soft crown, a band of foliage loops, and a wide floppy brim."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    grid = fit.smoothed_scalp(passes=12, dilate=2)
    line = lambda az: fit.brim_y(az, fit.eye_y + 4.6, fit.head_c[1] + 0.2, fit.head_c[1] - 3.2)
    top = fit.head_hi[1] + 2.0
    outer, inner, els = _shell(m, fit, grid, 1.2, 0.3, line, "Camo", skin, rows=10,
                               bulge=lambda t: 0.9 * math.sin(t * math.pi * 0.9), flatten_y=top)
    # The loop band round the crown.
    band = []
    for k, (rise, out) in enumerate(((0.3, 0.0), (0.6, 0.35), (2.6, 0.35), (2.9, 0.0))):
        row = []
        for (az, el0), p in zip(els, outer[0]):
            d = norm((p[0] - fit.head_c[0], 0.0, p[2] - fit.head_c[2]))
            row.append(add(add(p, (0.0, rise, 0.0)), mul(d, out + 0.06 * rise)))
        band.append(row)
    m.grid(band, "Camo", skin, outward_hint=lambda q: (fit.head_c[0], q[1], fit.head_c[2]), close_rows=True)
    # The brim, drooping and a little wavy, top and underside.
    top_rows, bottom_rows = [], []
    for t in (0.0, 0.3, 0.6, 0.85, 1.0):
        rt, rb = [], []
        for (az, el0), p in zip(els, outer[0]):
            d = norm((p[0] - fit.head_c[0], 0.0, p[2] - fit.head_c[2]))
            wave = 0.45 * math.sin(az * 5.0 + 0.7) * t
            q = add(add(p, mul(d, 6.8 * t)), (0.0, -2.4 * t * t + wave, 0.0))
            rt.append(q)
            rb.append(add(q, (0.0, -0.35, 0.0)))
        top_rows.append(rt)
        bottom_rows.append(rb)
    m.grid(top_rows, "Camo", skin, outward_hint=lambda q: add(q, (0.0, -1.0, 0.0)), close_rows=True)
    m.grid(bottom_rows, "Camo", skin, outward_hint=lambda q: add(q, (0.0, 1.0, 0.0)), close_rows=True)
    m.grid([top_rows[-1], bottom_rows[-1]], "Camo", skin, outward_hint=lambda q: (fit.head_c[0], q[1], fit.head_c[2]), close_rows=True)
    # The brim's stitching rings (darker thread), raised a hair.
    for t in (0.4, 0.75):
        ring = [add(add(add(p, mul(norm((p[0] - fit.head_c[0], 0.0, p[2] - fit.head_c[2])), 6.8 * t)),
                        (0.0, -2.4 * t * t + 0.45 * math.sin(az * 5.0 + 0.7) * t + 0.05, 0.0)), (0.0, 0.0, 0.0))
                for (az, el0), p in zip(els, outer[0])]
        m.tube(ring + ring[:2], 0.08, "Strap", skin, sides=4, caps=False)
    return m


def build_cap(fit):
    """A baseball-style patrol cap: six-panel crown, a top button, and a curved bill."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    grid = fit.smoothed_scalp(passes=8, dilate=1)
    line = lambda az: fit.brim_y(az, fit.eye_y + 4.2, fit.head_c[1] + 0.2, fit.head_c[1] - 3.0)
    outer, inner, els = _shell(m, fit, grid, 0.9, 0.25, line, "Gear", skin, rows=10, bulge=lambda t: 0.5 * math.sin(t * math.pi))
    # Panel seams: six ribs from the band to the crown.
    for i in range(6):
        idx = int(i * len(els) / 6 + len(els) / 12) % len(els)
        rib = [outer[k][idx] for k in range(len(outer) - 1)]
        m.tube(rib, 0.12, "Gear", skin, sides=4, caps=False)
    crown = mul(sum_points(outer[-1]), 1.0 / len(outer[-1]))
    m.box(add(crown, (0.0, 0.35, 0.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (0.9, 0.4, 0.9), "Gear", skin, bevel=0.35)
    # The bill: from the front of the band out 7.5 cm, curved across and tipped down.
    top_rows, bottom_rows = [], []
    span = [(az, p) for (az, el0), p in zip(els, outer[0]) if abs(az) <= 1.15]
    span.sort()
    for t in (0.0, 0.25, 0.5, 0.75, 1.0):
        rt, rb = [], []
        for az, p in span:
            reach = 7.6 * max(0.0, math.cos(az / 1.15 * math.pi / 2)) ** 0.55
            d = norm((p[0] - fit.head_c[0], 0.0, p[2] - fit.head_c[2]))
            q = add(add(p, mul(d, reach * t)), (0.0, -1.5 * t - 1.2 * (az / 1.15) ** 2 * t, 0.0))
            rt.append(q)
            rb.append(add(q, (0.0, -0.4, 0.0)))
        top_rows.append(rt)
        bottom_rows.append(rb)
    m.grid(top_rows, "Gear", skin, outward_hint=lambda q: add(q, (0.0, -1.0, 0.0)))
    m.grid(bottom_rows, "GearDark", skin, outward_hint=lambda q: add(q, (0.0, 1.0, 0.0)))
    m.grid([[r[-1] for r in top_rows], [r[-1] for r in bottom_rows]], "Gear", skin, outward_hint=lambda q: (0.0, q[1], fit.head_c[2]))
    m.grid([[r[0] for r in top_rows], [r[0] for r in bottom_rows]], "Gear", skin, outward_hint=lambda q: (0.0, q[1], fit.head_c[2]))
    m.grid([top_rows[-1], bottom_rows[-1]], "Gear", skin, outward_hint=lambda q: (fit.head_c[0], q[1] + 0.2, fit.head_c[2]))
    return m


def build_beanie(fit):
    """A ribbed knit beanie pulled down over the ear tops, with a folded cuff."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    grid = fit.smoothed_scalp(passes=6, dilate=1)
    line = lambda az: fit.brim_y(az, fit.eye_y + 3.2, fit.eye_y - 0.5, fit.head_c[1] - 7.0)
    outer, inner, els = _shell(m, fit, grid, 0.7, 0.3, line, "Knit", skin, rows=10,
                               bulge=lambda t: 0.5 * math.sin(t * math.pi * 0.8))
    # The rib pattern runs round the head (u) and up it (v): set the UVs from the angle and height.
    for i, v in enumerate(m.verts):
        p, n, uv, col, s = v
        m.verts[i] = (p, n, (math.atan2(p[0], p[2] - fit.head_c[2]) * 9.0, p[1] / 1.5), col, s)
    cuff = []
    for rise, out in ((-0.2, 0.0), (0.0, 0.65), (4.2, 0.6), (4.5, 0.0)):
        row = []
        for (az, el0), p in zip(els, outer[0]):
            d = norm(sub(p, fit.head_c))
            row.append(add(add(p, (0.0, rise, 0.0)), mul(d, out)))
        cuff.append(row)
    base = len(m.verts)
    m.grid(cuff, "Knit", skin, outward_hint=lambda q: (fit.head_c[0], q[1], fit.head_c[2]), close_rows=True)
    for i in range(base, len(m.verts)):
        p, n, uv, col, s = m.verts[i]
        m.verts[i] = (p, n, (math.atan2(p[0], p[2] - fit.head_c[2]) * 9.0, p[1] / 1.5), col, s)
    return m


def _temple(m, fit, start, side, skin, radius=0.22):
    """A glasses arm from the frame's edge back along the side of the head, over the ear and hooking down."""
    hs = head_surface(fit)
    ear_z = fit.head_c[2] - 1.5
    pts = []
    for k in range(9):
        t = k / 8
        z = start[2] + (ear_z - start[2]) * t
        y = start[1] + (fit.eye_y + 0.4 - start[1]) * t
        h = hs.raycast((0.0, y, z), (side, 0.0, 0.0), 25.0)
        x = (h[0][0] + side * 0.45) if h else start[0]
        pts.append((x if abs(x) > abs(start[0]) or k > 0 else start[0], y, z))
    pts[0] = start
    last = pts[-1]
    pts += [(last[0] - side * 0.2, last[1] - 1.2, last[2] - 1.6), (last[0] - side * 0.5, last[1] - 2.8, last[2] - 1.9)]
    m.tube(pts, radius, "Frame", skin, sides=6)


def _nose_bridge_z(fit):
    head = [p for v, p in enumerate(fit.body.pos) if fit.body.weight_on(v, ["Head"]) > 0.5]
    near = [p[2] for p in head if abs(p[0]) < 1.6 and fit.eye_y - 2.8 < p[1] < fit.eye_y + 1.0]
    return max(near) if near else fit.eye_front + 1.0


def build_sunglasses(fit):
    """Wraparound sunglasses: two curved dark lenses in a thin frame, a bridge over the nose, arms to the ears."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    z0 = max(fit.eye_front + 1.5, _nose_bridge_z(fit) + 0.4)
    wrap = 10.5

    def z_at(x):
        return z0 - x * x / (2.0 * wrap)
    for s in (1.0, -1.0):
        cx, cy = fit.eye[int(s)][0] + s * 0.35, fit.eye_y + 0.15
        outline = _rounded_rect(2.45, 1.75, 0.95, 4)
        rings_f, rings_b = [], []
        for sc in (1.0, 0.66, 0.33):
            rings_f.append([(cx + x * sc, cy + y * sc, z_at(cx + x * sc) + 0.12) for x, y in outline])
            rings_b.append([(cx + x * sc, cy + y * sc, z_at(cx + x * sc) - 0.12) for x, y in outline])
        m.grid(rings_f, "Lens", skin, outward_hint=lambda q: add(q, (0.0, 0.0, -1.0)), close_rows=True)
        m.fan(rings_f[-1], "Lens", skin, lambda q: add(q, (0.0, 0.0, -1.0)))
        m.grid(rings_b, "Lens", skin, outward_hint=lambda q: add(q, (0.0, 0.0, 1.0)), close_rows=True)
        m.fan(rings_b[-1], "Lens", skin, lambda q: add(q, (0.0, 0.0, 1.0)))
        rim = [lerp(a, b, 0.5) for a, b in zip(rings_f[0], rings_b[0])]
        m.tube(rim + rim[:2], 0.24, "Frame", skin, sides=5, caps=False)
        edge = max(rim, key=lambda p: s * p[0])
        _temple(m, fit, (edge[0], cy + 1.2, edge[2] - 0.2), s, skin)
    inner_l = (fit.eye[1][0] - 2.3, fit.eye_y + 1.2, z_at(fit.eye[1][0] - 2.3))
    bridge = [(inner_l[0], inner_l[1], inner_l[2]), (0.0, fit.eye_y + 1.7, z0 + 0.15), (-inner_l[0], inner_l[1], inner_l[2])]
    m.tube(catmull(bridge, 4), 0.26, "Frame", skin, sides=6)
    return m


def build_ballistic_glasses(fit):
    """Ballistic eye protection: one wrapped shield across both eyes with a nose notch, a brow bar and arms."""
    m = SkinMesh()
    skin = fit.rigid("Head")
    z0 = max(fit.eye_front + 1.7, _nose_bridge_z(fit) + 0.5)
    wrap, half = 8.0, abs(fit.eye[1][0]) + 3.9
    rows_f, rows_b = [], []
    cols = 24
    for r in range(5):
        t = r / 4
        rf, rb = [], []
        for c in range(cols + 1):
            u = -1.0 + 2.0 * c / cols
            x = u * half
            bottom = fit.eye_y - 2.1 + 1.7 * max(0.0, 1.0 - abs(x) / 1.6) ** 1.5 + 0.8 * abs(u) ** 3
            top = fit.eye_y + 2.3 - 0.5 * abs(u) ** 2
            y = bottom + (top - bottom) * t
            z = z0 - x * x / (2.0 * wrap)
            rf.append((x, y, z + 0.13))
            rb.append((x, y, z - 0.13))
        rows_f.append(rf)
        rows_b.append(rb)
    m.grid(rows_f, "ClearLens", skin, outward_hint=lambda q: (0.0, q[1], q[2] - 8.0))
    m.grid(rows_b, "ClearLens", skin, outward_hint=lambda q: (0.0, q[1], q[2] + 30.0))
    m.grid([rows_f[-1], rows_b[-1]], "ClearLens", skin, outward_hint=lambda q: add(q, (0.0, -1.0, 0.0)))
    m.grid([rows_f[0], rows_b[0]], "ClearLens", skin, outward_hint=lambda q: add(q, (0.0, 1.0, 0.0)))
    # The brow bar along the top edge.
    bar = [add(p, (0.0, 0.15, -0.1)) for p in rows_f[-1]]
    m.tube(bar, 0.3, "Frame", skin, sides=6)
    for s in (1.0, -1.0):
        end = bar[-1] if s > 0 else bar[0]
        _temple(m, fit, end, s, skin, radius=0.3)
    return m


# --- Vests ------------------------------------------------------------------------------------------------------------


class Torso:
    """The torso's surface (the uniform) seen from a vertical axis: where it is in front, behind, and around."""

    def __init__(self, fit, surf):
        self.fit, self.surf = fit, surf
        self.cz = (fit.spine2[2] + fit.pelvis[2]) * 0.5

    def front_z(self, x, y, direction=1.0):
        h = self.surf.raycast((x, y, self.cz), (0.0, 0.0, direction), 40.0)
        return h[0][2] if h else self.cz + direction * 12.0


def slab(m, outline, z_of, thick, direction, material, skin, profile=None, colour=(1.0, 1.0, 1.0, 1.0)):
    """A padded panel (a plate in its bag, a chest rig's front): the outline in x/y, its body-side face following
    z_of(x, y), standing thick cm out in the direction (+1 front, -1 back), its edges rounded off."""
    cx = sum(p[0] for p in outline) / len(outline)
    cy = sum(p[1] for p in outline) / len(outline)
    profile = profile or ((1.0, 0.0), (1.0, 0.5), (0.985, 0.85), (0.955, 1.0), (0.65, 1.0), (0.3, 1.0))
    rings = []
    for s, h in profile:
        ring = []
        for x, y in outline:
            px, py = cx + (x - cx) * s, cy + (y - cy) * s
            ring.append((px, py, z_of(px, py) + direction * thick * h))
        rings.append(ring)
    core = lambda q: (cx + (q[0] - cx) * 0.4, cy + (q[1] - cy) * 0.4, z_of(cx + (q[0] - cx) * 0.4, cy + (q[1] - cy) * 0.4) + direction * thick * 0.45)
    m.grid(rings, material, skin, outward_hint=core, close_rows=True, colour=colour)
    m.fan(rings[-1], material, skin, core, colour)
    m.fan(rings[0], material, skin, lambda q: add(q, (0.0, 0.0, direction * 2.0)), colour)
    return rings


def _shooters_cut(half_w, bottom, top, cut_w, cut_h, steps=3):
    """A plate's outline: a rectangle with its top corners cut away for the arms, corners softened."""
    pts = [(-half_w, bottom + 1.0), (-half_w + 1.0, bottom), (half_w - 1.0, bottom), (half_w, bottom + 1.0),
           (half_w, top - cut_h), (half_w - cut_w * 0.85, top - 0.6), (half_w - cut_w - 0.6, top),
           (-half_w + cut_w + 0.6, top), (-half_w + cut_w * 0.85, top - 0.6), (-half_w, top - cut_h)]
    return _densify(pts, 2.0)


def _densify(outline, step):
    out = []
    for i, a in enumerate(outline):
        b = outline[(i + 1) % len(outline)]
        n = max(1, int(math.ceil(math.hypot(b[0] - a[0], b[1] - a[1]) / step)))
        for k in range(n):
            out.append((a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n))
    return out


def _fit_panel(torso, half_w, bottom, top, direction, radius, clearance):
    """A gently curved panel surface z(x, y) standing clear of the torso over the footprint (curved across with the
    given radius, tilted to follow the torso's lean)."""
    xs = [-half_w + 2.0 * half_w * i / 6 for i in range(7)]
    ys = [bottom + (top - bottom) * j / 6 for j in range(7)]
    need = {}
    for y in ys:
        for x in xs:
            need[(x, y)] = torso.front_z(x, y, direction) * direction + x * x / (2.0 * radius)
    row_max = [max(need[(x, y)] for x in xs) for y in ys]
    lean = (row_max[-1] - row_max[0]) / (top - bottom)
    a = max(row_max[j] - lean * (ys[j] - bottom) for j in range(len(ys))) + clearance
    return lambda x, y: direction * (a + lean * (y - bottom) - x * x / (2.0 * radius))


def _area_skin(fit, surf, points, allowed=("spine_03", "spine_02", "spine_01")):
    """One skin for a rigid piece: the average of the body's skin under it (so a plate rides the chest as the chest
    moves, rather than one of its bones), kept to the spine."""
    skins = [restrict(surf.closest(p, 15.0)[2], list(allowed), fit.rig) for p in points]
    s = blend_skins(skins, [1.0 / len(skins)] * len(skins))
    return lambda q: s


def _vest_skin(fit, allowed=("spine_03", "spine_02", "spine_01", "clavicle_l", "clavicle_r")):
    return lambda surf: (lambda q: restrict(surf.closest(q, 12.0)[2], list(allowed), fit.rig))


def _mag_pouches(m, fit, panel_z, xs, bottom, w, h, d, skin):
    """Open-top magazine pouches on a panel, with the magazines standing in them."""
    for x in xs:
        cy = bottom + h * 0.5
        z = panel_z(x, cy) + d * 0.5 - 0.15
        m.box((x, cy, z), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (w * 0.5, h * 0.5, d * 0.5), "Gear", skin, bevel=0.8,
              colour=(1.0, 1.0, 1.0, 1.0))
        # A bungee pull tab across the front top, and the magazine's floorplate and body above the pouch.
        m.box((x, bottom + h - 1.2, z + d * 0.5 + 0.12), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (w * 0.38, 0.9, 0.18),
              "Strap", skin, bevel=0.1)
        m.box((x, bottom + h + 1.4, z - 0.2), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (w * 0.42, 1.9, d * 0.32),
              "Polymer", skin, bevel=0.35)


def build_plate_carrier(fit, usurf):
    """A plate carrier: front and back plates in padded bags, a cummerbund round the sides, padded shoulder straps,
    three rifle magazine pouches and an admin pouch on the front, a radio pouch and a tourniquet on the sides.
    Vertex colour green marks the MOLLE webbing (the material draws its rows). Returns (mesh, front panel, back
    panel z functions, bottom, top)."""
    m = SkinMesh()
    torso = Torso(fit, usurf)
    sc = fit.chest_half_w / 14.0
    half_w, height = 12.5 * sc, 30.0 * fit.scale
    top = fit.shoulder_y - 13.0 * fit.scale
    bottom = top - height
    front = _fit_panel(torso, half_w, bottom, top, 1.0, 36.0, 0.7)
    back_top = top + 2.0
    back = _fit_panel(torso, half_w, bottom, back_top, -1.0, 48.0, 0.7)
    grid = [(x, y) for x in (-half_w * 0.8, 0.0, half_w * 0.8) for y in (bottom + 3.0, (bottom + top) * 0.5, top - 3.0)]
    plate = _area_skin(fit, usurf, [(x, y, front(x, y)) for x, y in grid])
    back_plate = _area_skin(fit, usurf, [(x, y, back(x, y)) for x, y in grid])
    thick = 3.4
    slab(m, _shooters_cut(half_w, bottom, top, 4.8 * sc, 5.5), front, thick, 1.0, "Gear", plate)
    slab(m, _shooters_cut(half_w, bottom, back_top, 4.2 * sc, 5.0), back, thick, -1.0, "Gear", back_plate)
    front_out = lambda x, y: front(x, y) + thick
    back_out = lambda x, y: back(x, y) - thick
    # MOLLE webbing across the back plate: rows of strap laid on its face.
    for k in range(4):
        y = bottom + 3.0 + k * 3.8
        row = [(x, y, back_out(x, y) - 0.12) for x in [(-half_w + 1.5) + (2 * half_w - 3.0) * i / 10 for i in range(11)]]
        m.strap(row, [(0.0, 0.0, -1.0)] * len(row), 2.5, 0.18, "Strap", back_plate)
    # The cummerbund, from under the front plate's edge round to under the back plate's.
    vskin = _vest_skin(fit)(usurf)
    cum_bottom, cum_top = bottom + 0.8, bottom + 17.0 * fit.scale
    centre = (0.0, 0.0, torso.cz)
    a_front = math.atan2(half_w, front(half_w, bottom + 8.0) - torso.cz) - 0.12
    a_back = math.pi - math.atan2(half_w, torso.cz - back(half_w, bottom + 8.0)) + 0.12
    for s in (1.0, -1.0):
        a0, a1 = (a_front, a_back) if s > 0 else (-a_back, -a_front)
        ring_band(m, usurf, centre, cum_bottom, cum_top, 1.3, "Gear", skin_of=vskin, segments=14, ang0=a0, ang1=a1, uv_scale=10.0,
                  colour=(1.0, 1.0, 1.0, 1.0))
    # Shoulder straps over the top, from the front plate's top to the back's.
    for s in (1.0, -1.0):
        x = s * 7.6 * sc
        arc = []
        for k in range(1, 24):
            phi = math.pi * k / 24
            h = usurf.raycast((x, fit.shoulder_y - 10.0, torso.cz), (0.0, math.sin(phi), math.cos(phi)), 30.0)
            if h and h[0][1] > top - 0.5:
                arc.append(h[0])
        path = [(x, top - 1.5, front_out(x, top - 1.5) - 1.6), (x, top + 1.0, front_out(x, top) - 1.8)] + arc + \
               [(x, back_top + 1.0, back_out(x, back_top) + 1.8), (x, back_top - 1.5, back_out(x, back_top - 1.5) + 1.6)]
        strap_on(m, lay(usurf, path, 0.35, per_segment=2, allowed=["spine_03", "clavicle_l" if s > 0 else "clavicle_r"], rig=fit.rig),
                 5.2, 1.1, "Gear")
    # Front: three magazine pouches low on the plate, an admin pouch above them.
    pw = 7.6 * sc
    _mag_pouches(m, fit, front_out, [-pw * 1.05, 0.0, pw * 1.05], bottom + 0.6, pw, 12.5, 4.4, plate)
    ay = top - 8.5
    m.box((0.0, ay, front_out(0.0, ay) + 1.1), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (8.6 * sc, 4.2, 1.15), "Gear", plate, bevel=0.6)
    m.box((0.0, ay + 0.8, front_out(0.0, ay) + 2.32), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (4.6, 2.2, 0.08), "Velcro", plate)
    # A radio pouch on the left of the cummerbund (with its antenna), a tourniquet on the right.
    for s, size, antenna in ((1.0, (3.3, 7.5, 2.1), True), (-1.0, (2.4, 5.0, 1.9), False)):
        ang = s * 1.55
        y = cum_bottom + 8.5
        h = usurf.raycast((0.0, y, torso.cz), (math.sin(ang), 0.0, math.cos(ang)), 40.0)
        if not h:
            continue
        p, n, sk = h
        n = norm((n[0], 0.0, n[2]))
        skin = lambda q, sk=sk: restrict(sk, ["spine_02", "spine_03", "spine_01"], fit.rig)
        side = norm(cross((0.0, 1.0, 0.0), n))
        c = add(p, mul(n, 1.3 + size[2] + 0.1))
        m.box(c, (side, (0.0, 1.0, 0.0), n), size, "Gear" if antenna else "Strap", skin, bevel=0.6)
        if antenna:
            base = add(c, (0.0, size[1], 0.0))
            m.box(add(base, (0.0, 1.2, 0.0)), (side, (0.0, 1.0, 0.0), n), (1.4, 1.2, 1.3), "Polymer", skin, bevel=0.4)
            m.tube([add(base, (0.0, 2.0, 0.0)), add(base, (0.0, 16.0, -0.0))], 0.32, "Polymer", skin, sides=6)
    return m, front_out, back_out, bottom, top


def build_chest_rig(fit, usurf):
    """A chest rig: a padded panel across the lower chest carrying four magazine pouches, an H-harness over the
    shoulders crossing into an X on the back, and a waist strap round the back."""
    m = SkinMesh()
    torso = Torso(fit, usurf)
    sc = fit.chest_half_w / 14.0
    half_w = 15.0 * sc
    top = fit.shoulder_y - 27.0 * fit.scale
    bottom = top - 15.0 * fit.scale
    panel = _fit_panel(torso, half_w, bottom, top, 1.0, 24.0, 0.6)
    skin = _area_skin(fit, usurf, [(x, y, panel(x, y)) for x in (-half_w * 0.8, 0.0, half_w * 0.8) for y in (bottom + 2.0, top - 2.0)])
    outline = _densify([(-half_w, bottom + 1.5), (-half_w + 1.5, bottom), (half_w - 1.5, bottom), (half_w, bottom + 1.5),
                        (half_w, top - 2.5), (half_w - 2.5, top), (-half_w + 2.5, top), (-half_w, top - 2.5)], 2.0)
    slab(m, outline, panel, 1.4, 1.0, "Gear", skin)
    out = lambda x, y: panel(x, y) + 1.4
    pw = 7.0 * sc
    _mag_pouches(m, fit, out, [-pw * 1.6, -pw * 0.53, pw * 0.53, pw * 1.6], bottom + 0.8, pw, 11.5, 4.2, skin)
    allowed = ["spine_03", "spine_02", "clavicle_l", "clavicle_r"]
    back_y = bottom + 6.5
    for s in (1.0, -1.0):
        x = s * 8.2 * sc
        # Up the chest from the panel, over the shoulder, and down the back crossing to the other side's waist.
        path = [(x, top - 1.0, out(x, top - 1.0) - 0.9), (x, top + 9.0, torso.front_z(x, top + 9.0) + 0.5),
                (x * 0.95, fit.shoulder_y + 1.0, torso.cz), (x, fit.shoulder_y - 9.0, torso.front_z(x, fit.shoulder_y - 9.0, -1.0) - 0.5),
                (0.0, (fit.shoulder_y - 9.0 + back_y) * 0.5, torso.front_z(0.0, (fit.shoulder_y - 9.0 + back_y) * 0.5, -1.0) - 0.5),
                (-x * 1.5, back_y + 1.5, torso.front_z(-x * 1.5, back_y + 1.5, -1.0) - 0.5)]
        strap_on(m, lay(usurf, path, 0.3, per_segment=5, allowed=allowed, rig=fit.rig), 3.8, 0.45, "Strap")
    # The waist strap from the panel's ends round the back.
    vskin = _vest_skin(fit, ("spine_02", "spine_01", "spine_03"))(usurf)
    a_edge = math.atan2(half_w, panel(half_w, back_y) - torso.cz) - 0.1
    ring_band(m, usurf, (0.0, 0.0, torso.cz), back_y - 1.9, back_y + 1.9, 0.55, "Strap", skin_of=vskin, segments=30,
              ang0=a_edge, ang1=2.0 * math.pi - a_edge)
    return m


# --- Packs ------------------------------------------------------------------------------------------------------------


def build_pack(fit, usurf, kind, plate_back=None, vest_surface=None):
    """A pack on the back: the assault pack (a rounded box with a front pocket, a grab handle, compression straps
    and padded shoulder straps with a sternum strap) or the slim hydration pack (with its drinking tube over the
    right shoulder). Over a plate carrier (plate_back given) it sits on the back plate and its straps ride over the
    vest's."""
    m = SkinMesh()
    torso = Torso(fit, usurf)
    sc = fit.chest_half_w / 14.0
    assault = kind == "assault"
    w, h, d = (14.0 * sc, 22.0 * fit.scale, 8.5) if assault else (11.0 * sc, 20.0 * fit.scale, 3.3)
    top = fit.shoulder_y - 4.0
    bottom = top - 2.0 * h
    cy = (top + bottom) * 0.5

    # What the pack rests on, behind the body (-Z): the back itself, or the plate carrier's back plate. The pack's
    # back panel follows it (padded panels bend to the back), so it sits against it all over, not on one point;
    # round toward the ribs it follows no more than a few cm, as a framed panel would.
    def rest(x, y):
        under = plate_back if plate_back else (lambda a, b: torso.front_z(a, b, -1.0))
        return min(under(x, y), under(0.0, y) + 4.0) - 0.4
    back = rest(0.0, cy)
    front = rest(0.0, top - 1.5)
    cz = back - d
    skin = _area_skin(fit, usurf, [(x, y, torso.front_z(x, y, -1.0)) for x in (-w * 0.7, 0.0, w * 0.7) for y in (bottom + 4.0, cy, top - 4.0)],
                      allowed=("spine_03", "spine_02"))
    X, Y, Z = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
    # The body: a padded bag, its outer face pillowed out (deepest in the middle, 70% of that at its rounded sides),
    # on a grid mapped to a rounded rectangle.
    n = 14
    side_depth = 0.7

    def outline(u, v):
        # The unit square pulled toward a disc at the corners: a rectangle with well-rounded corners.
        du, dv = u * math.sqrt(max(0.0, 1.0 - v * v / 2.0)), v * math.sqrt(max(0.0, 1.0 - u * u / 2.0))
        k = 0.45
        return (u + (du - u) * k) * w, cy + (v + (dv - v) * k) * h

    def depth(u, v):
        return 2.0 * d * (side_depth + (1.0 - side_depth) * math.sqrt(max(0.0, 1.0 - u * u)) * math.sqrt(max(0.0, 1.0 - v * v)))
    grid_uv = [[(-1.0 + 2.0 * j / n, -1.0 + 2.0 * i / n) for j in range(n + 1)] for i in range(n + 1)]
    back_face, front_face = [], []
    for row in grid_uv:
        brow, frow = [], []
        for u, v in row:
            x, y = outline(u, v)
            zb = rest(x, y)
            brow.append((x, y, zb))
            frow.append((x, y, zb - depth(u, v)))
        back_face.append(brow)
        front_face.append(frow)
    m.grid(back_face, "Gear", skin, outward_hint=lambda q: (q[0], q[1], q[2] - 10.0))
    m.grid(front_face, "Gear", skin, outward_hint=lambda q: (q[0], q[1], q[2] + 10.0))
    # The rounded sides round its edge, bulging out a little between the two faces.
    edge = [(i, 0) for i in range(n)] + [(n, j) for j in range(n)] + [(i, n) for i in range(n, 0, -1)] + [(0, j) for j in range(n, 0, -1)]
    cen = (0.0, cy, back - d)
    rings = [[], [], []]
    for j, i in edge:
        b, f = back_face[i][j], front_face[i][j]
        out = norm((b[0], b[1] - cy, 0.0))
        rings[0].append(b)
        rings[1].append(add(lerp(b, f, 0.5), mul(out, 0.9)))
        rings[2].append(f)
    m.grid(rings, "Gear", skin, outward_hint=lambda q: (cen[0], cen[1], q[2]), close_rows=True)
    # A zip line round the back panel.
    if assault:
        zip_ring = []
        for x, y in _rounded_rect(1.0, 1.0, 0.25, 3):
            u, v = x * 0.9, y * 0.9
            px, py = outline(u, v)
            zip_ring.append((px, py, rest(px, py) - depth(u, v) - 0.05))
        m.tube(zip_ring + [zip_ring[0]], 0.18, "Strap", skin, sides=4, caps=False)
        # Front pocket with MOLLE, a grab handle on top, compression straps down the sides.
        m.box((0.0, bottom + 10.0, cz - d - 2.0), (X, Y, Z), (w * 0.78, 8.5, 2.3), "Gear", skin, bevel=1.4,
              colour=(1.0, 1.0, 1.0, 1.0))
        for k in range(3):
            y = bottom + 13.5 + k * 3.2
            m.box((0.0, y, cz - d - 4.35), (X, Y, Z), (w * 0.7, 1.2, 0.12), "Strap", skin)
        handle = [(-3.5, top - 0.6, cz + 1.0), (-2.5, top + 2.2, cz + 0.6), (2.5, top + 2.2, cz + 0.6), (3.5, top - 0.6, cz + 1.0)]
        m.tube(catmull(handle, 3), 0.55, "Strap", skin, sides=6)
        for s in (1.0, -1.0):
            for y in (cy + h * 0.45, cy - h * 0.4):
                m.box((s * (w + 0.1), y, cz - 1.0), (X, Y, Z), (0.2, 1.3, d * 0.9), "Strap", skin)
                m.box((s * (w + 0.35), y, cz - d * 0.45), (X, Y, Z), (0.35, 1.6, 1.1), "Polymer", skin, bevel=0.15)
    # Shoulder straps: from the top of the pack over the shoulders, down the chest and back under the arms.
    surf = vest_surface or usurf
    lift = 0.6
    allowed = ["spine_03", "spine_02", "clavicle_l", "clavicle_r"]
    sw, st = (6.0, 1.4) if assault else (4.4, 0.9)
    strap_paths = {}
    for s in (1.0, -1.0):
        x = s * 8.0 * sc
        chest_y = fit.shoulder_y - 19.0
        path = [(x * 0.9, top - 1.5, front + 1.0), (x, fit.shoulder_y - 2.0, torso.cz - 6.0), (x * 1.05, fit.shoulder_y + 1.5, torso.cz),
                (x * 1.15, fit.shoulder_y - 8.0, torso.front_z(x * 1.15, fit.shoulder_y - 8.0) + 1.0),
                (x * 1.35, chest_y, torso.front_z(x * 1.35, chest_y) + 1.0),
                (s * (fit.chest_half_w + 1.0), chest_y - 9.0, torso.cz + 2.0),
                (s * (w - 2.5), bottom + 5.0, front - 1.0)]
        laid = lay(surf, path, lift, per_segment=4, allowed=allowed, rig=fit.rig)
        strap_paths[s] = laid
        strap_on(m, laid, sw, st, "Gear" if assault else "Strap")
    # The sternum strap between them, with its buckle.
    lp, rp = strap_paths[1.0], strap_paths[-1.0]
    target_y = fit.shoulder_y - 15.0
    li = min(range(len(lp[0])), key=lambda i: abs(lp[0][i][1] - target_y) + (100.0 if lp[0][i][2] < torso.cz else 0.0))
    ri = min(range(len(rp[0])), key=lambda i: abs(rp[0][i][1] - target_y) + (100.0 if rp[0][i][2] < torso.cz else 0.0))
    a, b = add(lp[0][li], mul(lp[1][li], st)), add(rp[0][ri], mul(rp[1][ri], st))
    mid = lerp(a, b, 0.5)
    sternum = [a, add(mid, (0.0, 0.0, 0.6)), b]
    m.strap(catmull(sternum, 4), [(0.0, 0.0, 1.0)] * len(catmull(sternum, 4)), 2.0, 0.3, "Strap", lambda q: restrict(lp[2][li], allowed, fit.rig))
    m.box(add(mid, (0.0, 0.0, 0.9)), (X, Y, Z), (1.6, 1.2, 0.4), "Polymer", lambda q: restrict(lp[2][li], allowed, fit.rig), bevel=0.2)
    if not assault:
        # The drinking tube: out of the top, over the right shoulder and down the strap, ending in a bite valve.
        rpts = rp[0]
        tube = [(0.0, top + 0.5, cz), (-2.0, top + 3.0, cz + 2.0)]
        tube += [add(rpts[i], mul(rp[1][i], st + 0.9)) for i in range(2, len(rpts) // 2 + 2, 2)]
        m.tube(catmull(tube, 3), 0.55, "Polymer", lambda q: restrict(rp[2][ri], allowed, fit.rig), sides=6)
        end = tube[-1]
        m.box(add(end, (0.0, -1.5, 0.2)), (X, Y, Z), (0.7, 1.4, 0.7), "Strap", lambda q: restrict(rp[2][ri], allowed, fit.rig), bevel=0.3)
    return m


# --- The rifle (a static mesh, held in the menu pose) ----------------------------------------------------------------


def build_rifle():
    """An M4-style carbine as an OBJ-ready riptide_boat_mesh.Mesh (Unreal's axes: X along the barrel, Y right, Z up;
    centimetres), its origin where the right hand holds the pistol grip. Slots: GunMetal (black anodised), Furniture
    (in the gear colour) and Lens."""
    import riptide_boat_mesh as boat
    m = boat.Mesh()
    gx = 4.0     # the grip's hand position sits this far behind the magazine well, at the receiver's underside
    def bx(x0, x1, y, z0, z1, mat="GunMetal"):
        m.box((x0 + gx, -y, z0), (x1 + gx, y, z1), mat)
    # Receivers and the top rail with its slots.
    bx(-7.0, 9.5, 1.1, -0.8, 3.0)
    bx(-8.5, 12.0, 1.3, 3.0, 6.0)
    bx(-8.0, 12.0, 1.05, 6.0, 6.6)
    for i in range(20):
        x = -7.6 + i * 1.0
        bx(x, x + 0.55, 1.05, 6.6, 7.0)
    # Magazine well, magazine (curved), trigger guard and trigger.
    bx(2.5, 9.0, 1.55, -3.2, -0.8)
    m.prism([(2.9 + gx, -3.2), (8.8 + gx, -3.2), (10.6 + gx, -18.0), (4.6 + gx, -19.0)], -1.15, 1.15, "Furniture")
    bx(-4.0, 3.0, 0.35, -3.6, -3.2)
    bx(-0.6, 0.0, 0.25, -3.2, -1.2)
    # The pistol grip, angled back.
    m.prism([(-3.0 + gx, -0.8), (0.5 + gx, -0.8), (-2.5 + gx, -11.5), (-6.0 + gx, -11.0)], -1.35, 1.35, "Furniture")
    # Handguard: an octagonal free-float rail, with a top rail continuing the receiver's.
    m.tube([(12.0 + gx, 0.0, 3.6), (37.0 + gx, 0.0, 3.6)], 2.25, "GunMetal", sides=8, caps=True)
    bx(12.0, 37.0, 1.05, 5.6, 6.6)
    for i in range(24):
        x = 12.6 + i * 1.0
        bx(x, x + 0.55, 1.05, 6.6, 7.0)
    # Barrel, gas block, muzzle brake.
    m.tube([(37.0 + gx, 0.0, 3.8), (51.0 + gx, 0.0, 3.8)], 0.75, "GunMetal", sides=10, caps=True)
    bx(37.0, 39.5, 1.1, 2.9, 5.0)
    m.tube([(51.0 + gx, 0.0, 3.8), (56.0 + gx, 0.0, 3.8)], 1.1, "GunMetal", sides=8, caps=True)
    # Buffer tube and collapsible stock, charging handle, ejection port cover.
    m.tube([(-8.5 + gx, 0.0, 4.4), (-26.0 + gx, 0.0, 4.4)], 1.5, "GunMetal", sides=10, caps=True)
    m.prism([(-17.0 + gx, 6.6), (-31.5 + gx, 6.8), (-32.5 + gx, -3.5), (-28.0 + gx, -3.8), (-17.0 + gx, 2.6)], -1.9, 1.9, "Furniture")
    bx(-10.2, -8.5, 2.1, 5.0, 5.9)
    m.box((-1.0 + gx, 1.3, 3.4), (6.0 + gx, 1.45, 5.4), "GunMetal")
    # A red-dot sight on a riser, with lenses at both ends.
    bx(1.5, 8.0, 1.2, 7.0, 8.6)
    m.tube([(1.0 + gx, 0.0, 10.4), (9.0 + gx, 0.0, 10.4)], 1.9, "GunMetal", sides=14, caps=False)
    for x in (1.15, 8.85):
        m.tube([(x + gx - 0.1, 0.0, 10.4), (x + gx + 0.1, 0.0, 10.4)], 1.65, "Lens", sides=14, caps=True)
    # An angled foregrip under the handguard.
    m.prism([(25.0 + gx, 1.4), (31.0 + gx, 1.4), (29.5 + gx, -4.0), (26.5 + gx, -4.0)], -1.2, 1.2, "Furniture")
    # Sling swivel loops.
    m.tube([(-31.0 + gx, 0.0, 6.9), (-30.0 + gx, 0.0, 7.6), (-28.5 + gx, 0.0, 6.9)], 0.25, "GunMetal", sides=5)
    return m


# --- Castaway clothes: a t-shirt, shorts and beach footwear ---------------------------------------------------------
# What the crew start in. Cut from the body like the uniform (pushed out and smoothed into cloth), or built on the
# feet. Material slots: "Shirt" and "Shorts" (cloth in the chosen colour), "Strap" (webbing), "Boot" (leather),
# "Sole" (rubber), "Clog" (wood).


def body_surface(fit):
    """The body's own skin as a Surface, for pressing straps onto it."""
    w = fit.welded
    return Surface(w.points, w.tris(), w.skins, w.normals)


def build_tshirt(fit):
    """A t-shirt: the torso from a round neck to just below the waist, sleeves to the middle of the upper arm,
    hemmed at the neck, sleeves and bottom."""
    w, rig = fit.welded, fit.rig
    neck = fit.neck
    shoulder_x = rig.pos("upperarm_l")[0]
    sleeve_x = shoulder_x + 0.42 * (rig.pos("lowerarm_l")[0] - shoulder_x)
    hem_y = fit.pelvis[1] - 7.0 * fit.scale

    def keep_point(p):
        # A round neck, lower at the front; the sleeves end on the upper arm; the hem sits on the hips.
        collar = neck[1] - 0.5 - 4.5 * smoothstep(-2.0, 7.0, p[2] - neck[2]) + 20.0 * smoothstep(7.0, 12.0, abs(p[0]))
        return p[1] < collar and abs(p[0]) < sleeve_x and p[1] > hem_y
    keep = {g for g, p in enumerate(w.points) if keep_point(p)
            and not w.body.dominant(w.members[g][0]).startswith(FOOT_BONES + HAND_BONES + ("Head", "lowerarm", "calf", "thigh"))}
    pts = inflate(w, keep, lambda g: 1.0, smooth_iters=7, min_offset=lambda g: 0.5, smooth_weight=0.55)
    m = SkinMesh()
    surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Shirt")
    for loop in boundary_loops(surf.tris):
        c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
        if c[1] > fit.shoulder_y - 8.0 and abs(c[0]) < 12.0:
            hem(m, surf, loop, lambda p: norm((0.0, -1.0, 0.0)), 1.4, 0.3, "Shirt", tuck=0.2)       # the neck band
        elif abs(c[0]) > shoulder_x + 2.0:
            side = 1.0 if c[0] > 0 else -1.0
            hem(m, surf, loop, lambda p, s=side: (-s, 0.0, 0.0), 2.0, 0.35, "Shirt", tuck=0.3)      # the sleeve hems
        else:
            hem(m, surf, loop, lambda p: (0.0, 1.0, 0.0), 2.2, 0.3, "Shirt", tuck=0.3)              # the bottom hem
    return m


def build_shorts(fit):
    """Shorts: from the waist to just above the knee, with a waistband and hemmed legs."""
    w, rig = fit.welded, fit.rig
    knee_y = rig.pos("calf_l")[1]
    waist_y = fit.pelvis[1] + 7.0 * fit.scale
    leg_y = knee_y + 9.0 * fit.scale

    def keep_point(g, p):
        bone = w.body.dominant(w.members[g][0])
        return leg_y < p[1] < waist_y and (bone.startswith(("pelvis", "thigh", "spine_01")) or (p[1] < fit.pelvis[1] + 2.0 and abs(p[0]) < 24.0))
    keep = {g for g, p in enumerate(w.points) if keep_point(g, p)}
    pts = inflate(w, keep, lambda g: 1.1, smooth_iters=7, min_offset=lambda g: 0.6, smooth_weight=0.55)
    m = SkinMesh()
    surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Shorts")
    for loop in boundary_loops(surf.tris):
        c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
        if c[1] > fit.pelvis[1]:
            hem(m, surf, loop, lambda p: (0.0, -1.0, 0.0), 3.0, 0.5, "Shorts", tuck=0.2)     # the waistband
        else:
            hem(m, surf, loop, lambda p: (0.0, 1.0, 0.0), 2.0, 0.3, "Shorts", tuck=0.3)      # the leg hems
    return m


def _foot_outline(fit, side, welt):
    """A foot's footprint on the floor, widened by welt cm: densified outline, its centre, and the foot's bones."""
    rig, b = fit.rig, fit.body
    name = "l" if side > 0 else "r"
    floor = min(p[1] for p in b.pos)
    foot_pts = [p for v, p in enumerate(b.pos) if p[0] * side > 0 and b.weight_on(v, ["foot_" + name, "ball_" + name]) > 0.5]
    hull = _hull2d([(p[0], p[2]) for p in foot_pts if p[1] < floor + 3.5])
    cx = sum(x for x, _ in hull) / len(hull)
    cz = sum(z for _, z in hull) / len(hull)
    ring = _densify([(cx + (x - cx) * (1.0 + welt / 5.0) + (welt * 0.6 if x > cx else -welt * 0.6), cz + (z - cz) * (1.0 + welt / 12.0)) for x, z in hull], 1.0)
    return ring, (cx, cz), floor, foot_pts, name


def _sole(m, fit, side, thick, welt=0.5, material="Sole", heel=0.0):
    """A flat sole under one foot, thick cm, with a raised heel if heel > 0. Returns (outline, centre, floor, foot
    points, side name, top height function)."""
    ring, (cx, cz), floor, foot_pts, name = _foot_outline(fit, side, welt)
    jf, jb = fit.rig.index["foot_" + name], fit.rig.index["ball_" + name]
    ball_z = fit.rig.pos("ball_" + name)[2]
    skin = lambda q: {jb: 0.6, jf: 0.4} if q[2] > ball_z - 2.0 else {jf: 1.0}

    def top(z):
        return floor + thick + heel * (1.0 - smoothstep(ball_z - 9.0, ball_z - 3.0, z))
    bottom = [(x, floor + 0.05, z) for x, z in ring]
    upper = [(x, top(z), z) for x, z in ring]
    m.grid([bottom, upper], material, skin, outward_hint=lambda q: (cx, q[1], cz), close_rows=True)
    m.fan(bottom, material, skin, lambda q: add(q, (0.0, 3.0, 0.0)))
    m.fan(upper, material, skin, lambda q: add(q, (0.0, -3.0, 0.0)))
    return ring, (cx, cz), floor, foot_pts, name, top


def _strap_over(m, surf, fit, side, z, width, thick, material, lift=0.5, span=1.0):
    """A strap across the foot at depth z, from the sole's edge on one side over the foot to the other, pressed to
    the skin and lifted off it."""
    name = "l" if side > 0 else "r"
    foot_x = fit.rig.pos("foot_" + name)[0]
    b = fit.body
    near = [p for p in b.pos if abs(p[2] - z) < 2.0 and abs(p[0] - foot_x) < 9.0 and p[1] < 14.0]
    if not near:
        return
    lo_x, hi_x = min(p[0] for p in near) - 0.3, max(p[0] for p in near) + 0.3
    path, normals = [], []
    for k in range(13):
        t = k / 12.0
        x = lo_x + (hi_x - lo_x) * t
        column = [p for p in near if abs(p[0] - x) < 0.9]
        y = max(p[1] for p in column) if column else 1.0
        # Down the sides the strap wraps round to the sole.
        edge = 1.0 - smoothstep(0.0, 0.12, min(t, 1.0 - t))
        p, n, _ = surf.closest((x, y * (1.0 - edge) + 1.2 * edge, z), 6.0)
        path.append(add(p, mul(n, lift)))
        normals.append(n)
    skin_of = lambda q: surf.closest(q, 8.0)[2]
    # Vertex colour black: the Boot material's plain leather (white would read as its rubber sole: black).
    m.strap(path, normals, width, thick, material, skin_of, colour=(0.0, 0.0, 0.0, 1.0))


def build_slides(fit):
    """Slides: a thick flat sole with one broad strap over the instep."""
    m = SkinMesh()
    surf = body_surface(fit)
    for side in (1.0, -1.0):
        ring, centre, floor, foot_pts, name, top = _sole(m, fit, side, 2.2, welt=0.6)
        ball_z = fit.rig.pos("ball_" + name)[2]
        _strap_over(m, surf, fit, side, ball_z - 1.0, 4.5, 0.5, "Strap", lift=0.55)
    return m


def build_sandals(fit):
    """Sandals: a thinner sole, a toe strap, an instep strap and a strap round the heel."""
    m = SkinMesh()
    surf = body_surface(fit)
    for side in (1.0, -1.0):
        ring, (cx, cz), floor, foot_pts, name, top = _sole(m, fit, side, 1.6, welt=0.4)
        rig = fit.rig
        ball_z = rig.pos("ball_" + name)[2]
        ankle = rig.pos("foot_" + name)
        _strap_over(m, surf, fit, side, ball_z + 3.0, 2.2, 0.35, "Boot", lift=0.45)
        _strap_over(m, surf, fit, side, ball_z - 4.5, 2.4, 0.35, "Boot", lift=0.45)
        # Round the heel at ankle-bone height, from one side of the sole to the other.
        y = floor + 5.5 * fit.scale
        path, normals = [], []
        for k in range(15):
            ang = -0.5 * math.pi + math.pi * k / 14.0      # from one side, round the back, to the other
            x, z = ankle[0] + 6.0 * math.sin(ang), ankle[2] - 7.0 * math.cos(ang) + 1.0
            p, n, _ = surf.closest((x, y, z), 8.0)
            path.append(add(p, mul(n, 0.45)))
            normals.append(n)
        m.strap(path, normals, 1.8, 0.35, "Boot", lambda q: surf.closest(q, 8.0)[2], colour=(0.0, 0.0, 0.0, 1.0))
    return m


def build_clogs(fit):
    """Clogs: a thick wooden sole, a little higher at the heel, with a closed toe box over the front of the foot."""
    w, rig = fit.welded, fit.rig
    m = SkinMesh()
    for side in (1.0, -1.0):
        ring, (cx, cz), floor, foot_pts, name, top = _sole(m, fit, side, 2.6, welt=0.7, material="Clog", heel=1.2)
        ball_z = rig.pos("ball_" + name)[2]
        ankle_z = rig.pos("foot_" + name)[2]
        # The toe box: the front of the foot pushed out and smoothed, open behind the instep.
        keep = {g for g, p in enumerate(w.points) if p[0] * side > 0 and p[2] > ankle_z - 1.0 and p[1] < floor + 10.5 * fit.scale
                and w.body.dominant(w.members[g][0]).startswith(FOOT_BONES)}
        pts = inflate(w, keep, lambda g: 1.1, smooth_iters=16, min_offset=lambda g: 0.6, smooth_weight=0.65)
        for g, p in list(pts.items()):
            pts[g] = (p[0], max(p[1], top(p[2]) - 0.2), p[2])
        surf, _ = _mesh_from_points(m, w, pts, w.tris(), "Clog")
        for loop in boundary_loops(surf.tris):
            c = mul(sum_points([surf.points[v] for v in loop]), 1.0 / len(loop))
            if c[1] > floor + 2.0:
                hem(m, surf, loop, lambda p: (0.0, 0.0, 1.0), 1.2, 0.4, "Clog", tuck=0.2)
    return m


# --- Everything --------------------------------------------------------------------------------------------------------

# The hairstyles: each body has its own fitted file of each (Tools/mh_export.py writes them).
HAIRSTYLES = ("SK_Hair_Buzzed", "SK_Hair_Parted", "SK_Hair_Long", "SK_Hair_Buns")


def build_all(source_dir, out_dir, log=print):
    """Generates every crew mesh for both bodies into out_dir/<Body>/. The bodies and hair are MakeHuman's
    (source_dir/MakeHuman, see Tools/mh_export.py); the beard is Quaternius's, refitted. Returns
    {body: {asset name: .gltf path}} and the rifle's OBJ path."""
    base = os.path.join(source_dir, "MakeHuman")
    female = Fit(os.path.join(base, "Female", "Female_FullBody.gltf"), "Female")
    male = Fit(os.path.join(base, "Male", "Male_FullBody.gltf"), "Male")
    beard_file = os.path.join(source_dir, "Quaternius", "Hair", "Hair_Beard.gltf")
    beard_fit = Fit(os.path.join(source_dir, "Quaternius", "BaseCharacters", "Superhero_Female_FullBody.gltf"), "BeardFit")
    result = {}
    for fit in (male, female):
        folder = os.path.join(out_dir, fit.name)
        os.makedirs(folder, exist_ok=True)
        meshes = {}
        uniform, usurf = build_uniform(fit)
        meshes["SK_Uniform"] = uniform
        meshes["SK_Boots"] = build_boots(fit)
        meshes["SK_Gloves"] = build_gloves(fit)
        meshes["SK_TShirt"] = build_tshirt(fit)
        meshes["SK_Shorts"] = build_shorts(fit)
        meshes["SK_Sandals"] = build_sandals(fit)
        meshes["SK_Slides"] = build_slides(fit)
        meshes["SK_Clogs"] = build_clogs(fit)
        meshes["SK_Balaclava"] = build_balaclava(fit)
        meshes["SK_Shemagh"] = build_shemagh(fit)
        meshes["SK_Helmet"] = build_helmet(fit)
        meshes["SK_Boonie"] = build_boonie(fit)
        meshes["SK_Cap"] = build_cap(fit)
        meshes["SK_Beanie"] = build_beanie(fit)
        meshes["SK_Sunglasses"] = build_sunglasses(fit)
        meshes["SK_BallisticGlasses"] = build_ballistic_glasses(fit)
        pc, front_out, back_out, _, _ = build_plate_carrier(fit, usurf)
        meshes["SK_PlateCarrier"] = pc
        meshes["SK_ChestRig"] = build_chest_rig(fit, usurf)
        over = surface_of(usurf, pc)
        meshes["SK_AssaultPack"] = build_pack(fit, usurf, "assault")
        meshes["SK_AssaultPack_OverPlate"] = build_pack(fit, usurf, "assault", back_out, over)
        meshes["SK_HydrationPack"] = build_pack(fit, usurf, "hydration")
        meshes["SK_HydrationPack_OverPlate"] = build_pack(fit, usurf, "hydration", back_out, over)
        for name in HAIRSTYLES:
            path = os.path.join(base, fit.name, name + ".gltf")
            meshes[name] = build_hair(fit, path, fit)
            meshes[name + "_Hat"] = build_hair(fit, path, fit, hat=True)
        meshes["SK_Beard"] = build_hair(fit, beard_file, beard_fit)
        result[fit.name] = {}
        for name, mesh in meshes.items():
            path = os.path.join(folder, name + ".gltf")
            write_gltf(path, fit.rig, mesh, name)
            result[fit.name][name] = path
            log("Riptide: %s %s: %d triangles" % (fit.name, name, mesh.triangle_count()))
    rifle = os.path.join(out_dir, "SM_Rifle.obj")
    build_rifle().write_obj(rifle)
    return result, rifle
