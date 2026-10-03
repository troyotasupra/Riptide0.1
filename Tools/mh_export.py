"""Builds Riptide's crew bodies with MakeHuman's own libraries, without its window, and writes each as a glTF file
(metres, Y up, facing +Z, the game-engine rig with Unreal's bone names) that Content/Python/riptide_crew_mesh.py
reads like the old bodies. Run with MakeHuman's bundled Python from this folder:

    app\\Python\\python.exe mh_export.py <out_dir>

Everything MakeHuman makes or ships is CC0 (see app/makehuman/license.txt).
"""
import json
import math
import os
import struct
import sys

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "out")
MH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app", "makehuman")
os.chdir(MH)
for p in ["./", "./lib", "./apps", "./shared", "./apps/gui", "./core", "./plugins"]:
    sys.path.insert(0, os.path.abspath(p))
os.environ["PATH"] = os.path.abspath(".") + os.pathsep + os.environ["PATH"]

import numpy as np  # noqa: E402
import numpy.linalg as la  # noqa: E402

from core import G  # noqa: E402


class StubApp(object):
    """What the core reaches for on the application object when there is no window."""

    def __init__(self):
        self.selectedHuman = None
        self.loadHandlers = {}
        self.saveHandlers = []
        self.settings = {"realtimeUpdates": False, "realtimeNormalUpdates": False, "realtimeFitting": False, "cameraAutoZoom": False}
        self.modelCamera = None
        self.mhapi = None
        self.actions = None

    def getSetting(self, key):
        return self.settings.get(key)

    def setSetting(self, key, value):
        self.settings[key] = value

    def progress(self, *args, **kwargs):
        pass

    def callAsync(self, fn, *args, **kwargs):
        fn(*args, **kwargs)

    def do(self, action):
        return action.do()

    def redraw(self):
        pass

    def statusPersist(self, *a, **k):
        pass

    def status(self, *a, **k):
        pass

    def prompt(self, *a, **k):
        pass


G.app = StubApp()

import files3d  # noqa: E402
import human as human_mod  # noqa: E402
import humanmodifier  # noqa: E402
import skeleton as skeleton_mod  # noqa: E402
import proxy as proxy_mod  # noqa: E402
import material as material_mod  # noqa: E402
from getpath import getSysDataPath  # noqa: E402

# Unreal's names where MakeHuman's differ, and what our animation code expects.
RENAME = {"Root": "root", "head": "Head"}
UNRENAME = {v: k for k, v in RENAME.items()}

# The rig the crew's animation clips were made for (Quaternius's, a T-pose with its own bone rolls): the bodies are
# written standing in its bind pose with its bone orientations, keeping their own bone lengths, so the clips and
# the hand-made overlays in the animation code play on them unchanged.
REFERENCE_RIG = r"C:\src\Riptide-islands\SourceAssets\Characters\Quaternius\BaseCharacters\Superhero_Male_FullBody.gltf"
sys.path.insert(0, r"C:\src\Riptide-islands\Content\Python")


def reference_orientations():
    """{bone name (MakeHuman's): 3x3 global rotation (columns are the bone's axes), in Y-up metres} from the
    reference rig's bind pose."""
    import riptide_crew_mesh as cm
    rig = cm.Rig(cm.Gltf(REFERENCE_RIG))
    out, pos = {}, {}
    for name in rig.names:
        m = rig.matrix(name)
        out[UNRENAME.get(name, name)] = np.array([[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]], dtype=np.float64)
        pos[UNRENAME.get(name, name)] = np.array(rig.pos(name), dtype=np.float64) * 0.01   # cm to metres
    return out, pos


class Repose(object):
    """The skeleton stood in the reference rig's bind pose: for each bone its new global matrix (metres) and the
    matrix that carries a point skinned to it from the MakeHuman rest pose into the new one."""

    def __init__(self, skel, ground):
        ref, ref_pos = reference_orientations()
        self.new_global = {}
        self.carry = {}
        bones = skel.getBones()
        by_name = {b.name: b for b in bones}
        # Parents first.
        order = []
        seen = set()

        def visit(b):
            if b.name in seen:
                return
            if b.parent is not None:
                visit(b.parent)
            seen.add(b.name)
            order.append(b)
        for b in bones:
            visit(b)
        for b in order:
            g = np.array(b.matRestGlobal, dtype=np.float64)
            g[:3, 3] *= 0.1
            g[1, 3] += ground
            r_old, p_old = g[:3, :3], g[:3, 3]
            if b.parent is None:
                r_new, p_new = ref.get(b.name, r_old), p_old
            else:
                pg_old = np.array(b.parent.matRestGlobal, dtype=np.float64)
                pg_old[:3, 3] *= 0.1
                pg_old[1, 3] += ground
                parent_new = self.new_global[b.parent.name]
                # The bone sits its own distance from its parent, along the reference rig's direction for it (the
                # two rigs' parent frames differ in roll, so the offset itself can't be carried across).
                offset = p_old - pg_old[:3, 3]
                turn = parent_new[:3, :3].dot(la.inv(pg_old[:3, :3]))
                ref_dir = ref_pos[b.name] - ref_pos[b.parent.name] if b.name in ref_pos and b.parent.name in ref_pos else None
                if ref_dir is not None and la.norm(ref_dir) > 1e-4:
                    p_new = parent_new[:3, 3] + ref_dir / la.norm(ref_dir) * la.norm(offset)
                else:
                    p_new = parent_new[:3, 3] + turn.dot(offset)
                r_new = ref[b.name] if b.name in ref else turn.dot(r_old)
            n = np.identity(4)
            n[:3, :3], n[:3, 3] = r_new, p_new
            self.new_global[b.name] = n
            self.carry[b.name] = n.dot(la.inv(g))

    def point(self, p, weights):
        """A rest-pose point (metres) in the new pose, by its (bone, weight) list."""
        out = np.zeros(3)
        total = 0.0
        for bone, w in weights:
            m = self.carry.get(bone)
            if m is None:
                continue
            out += w * (m[:3, :3].dot(p) + m[:3, 3])
            total += w
        return out / total if total > 1e-6 else p

    def direction(self, d, weights):
        out = np.zeros(3)
        for bone, w in weights:
            m = self.carry.get(bone)
            if m is not None:
                out += w * m[:3, :3].dot(d)
        n = la.norm(out)
        return out / n if n > 1e-9 else d


REPOSE = [None]

LOOKS = {
    "Male": {
        "modifiers": {"macrodetails/Gender": 1.0, "macrodetails/Age": 0.5, "macrodetails-universal/Muscle": 0.62,
                      "macrodetails-universal/Weight": 0.5, "macrodetails-height/Height": 0.52, "macrodetails-proportions/BodyProportions": 0.55},
        "skin": "skins/middleage_caucasian_male/middleage_caucasian_male.mhmat",
        "hair": "hair/short02/short02.mhpxy", "eyebrows": "eyebrows/eyebrow001/eyebrow001.mhpxy",
    },
    "Female": {
        "modifiers": {"macrodetails/Gender": 0.0, "macrodetails/Age": 0.45, "macrodetails-universal/Muscle": 0.55,
                      "macrodetails-universal/Weight": 0.5, "macrodetails-height/Height": 0.6, "macrodetails-proportions/BodyProportions": 0.6},
        "skin": "skins/middleage_caucasian_female/middleage_caucasian_female.mhmat",
        "hair": "hair/bob02/bob02.mhpxy", "eyebrows": "eyebrows/eyebrow006/eyebrow006.mhpxy",
    },
}
# Every hairstyle, written as its own file per body (riptide_crew_mesh.build_hair reads them).
HAIR = {"SK_Hair_Buzzed": "short04", "SK_Hair_Parted": "short01", "SK_Hair_Long": "long01", "SK_Hair_Buns": "braid01"}


def log(msg):
    print("mh_export: " + msg)
    sys.stdout.flush()


def build_human(spec):
    h = human_mod.Human(files3d.loadMesh(getSysDataPath("3dobjs/base.obj"), maxFaces=5))
    G.app.selectedHuman = h
    humanmodifier.loadModifiers(getSysDataPath("modifiers/modeling_modifiers.json"), h)
    base = skeleton_mod.load(getSysDataPath("rigs/default.mhskel"), h.meshData)
    h.setBaseSkeleton(base)
    for name, value in spec["modifiers"].items():
        h.getModifier(name).setValue(value)
    h.applyAllTargets()
    h.setMaterial(material_mod.fromFile(getSysDataPath(spec["skin"])))
    skel = skeleton_mod.load(getSysDataPath("rigs/game_engine.mhskel"), h.meshData)
    h.setSkeleton(skel)
    # The parts that come with a face: eyes, eyebrows, eyelashes, teeth.
    h.setEyesProxy(load_proxy(h, "eyes/high-poly/high-poly.mhpxy", "Eyes"))
    h.getEyesProxy().object.material = material_mod.fromFile(getSysDataPath("eyes/materials/brown.mhmat"))
    h.setEyebrowsProxy(load_proxy(h, spec["eyebrows"], "Eyebrows"))
    h.setEyelashesProxy(load_proxy(h, "eyelashes/eyelashes01/eyelashes01.mhpxy", "Eyelashes"))
    h.setTeethProxy(load_proxy(h, "teeth/teeth_base/teeth_base.mhpxy", "Teeth"))
    return h


def load_proxy(h, rel, kind):
    """A proxy (eyes, hair...) with its mesh object made, as the library choosers do before fitting it."""
    p = proxy_mod.loadProxy(h, getSysDataPath(rel), type=kind)
    p.loadMeshAndObject(h)
    return p


GROUND = [0.0]   # how far up everything is lifted so the feet stand on y = 0 (set per body)


def mesh_arrays(mesh, weights, drop_bones=(), cap_bone=None, default_bone="head"):
    """A mesh (a clone with hidden faces filtered out) as flat arrays for glTF: one vertex per (vertex, uv) pair,
    triangles, and per-vertex joints/weights from a VertexBoneWeights. With drop_bones, faces whose vertices mostly
    hang on those bones are left out (the head, for the body seen from one's own eyes) and the hole is closed with
    a fan on cap_bone."""
    coord = np.asarray(mesh.coord, dtype=np.float32)
    vnorm = np.asarray(mesh.vnorm, dtype=np.float32)
    texco = np.asarray(mesh.texco, dtype=np.float32) if mesh.texco is not None and len(mesh.texco) else np.zeros((len(coord), 2), np.float32)
    fvert = np.asarray(mesh.fvert)
    fuvs = np.asarray(mesh.fuvs) if mesh.fuvs is not None and len(mesh.fuvs) else np.zeros_like(fvert)
    # Weights per original vertex: up to four (joint, weight).
    per_vert = [[] for _ in range(len(coord))]
    if weights is not None:
        for bone, (verts, wts) in weights.data.items():
            for v, w in zip(verts, wts):
                per_vert[int(v)].append((bone, float(w)))
    # MakeHuman hands vertices it has no weights for (the eyes, teeth and the like hang off helper vertices) to the
    # root bone: those take the nearest properly weighted vertex's weights instead.
    def real(pv):
        return [(b, w) for b, w in pv if b != "Root"]
    unweighted = [v for v, pv in enumerate(per_vert) if not real(pv)]
    weighted = [v for v, pv in enumerate(per_vert) if real(pv)]
    for v in weighted:
        per_vert[v] = real(per_vert[v])
    if unweighted and weighted:
        wpos = coord[weighted]
        for v in unweighted:
            d = np.sum((wpos - coord[v]) ** 2, axis=1)
            per_vert[v] = list(per_vert[weighted[int(np.argmin(d))]])
    elif unweighted:
        # Nothing on this mesh is weighted (the eyes): it rides the bone it belongs to.
        for v in unweighted:
            per_vert[v] = [(default_bone, 1.0)]
    def on_dropped(v):
        return sum(w for b, w in per_vert[v] if b in drop_bones) > 0.5
    pairs = {}
    positions, normals, uvs, joints, wts, tris = [], [], [], [], [], []
    edge_count = {}
    for face, face_uv in zip(fvert, fuvs):
        if drop_bones and sum(on_dropped(int(v)) for v in face) >= 2:
            continue
        corners = []
        for v, uv in zip(face, face_uv):
            key = (int(v), int(uv))
            if key not in pairs:
                pairs[key] = len(positions)
                p = coord[v] * 0.1 + np.array([0.0, GROUND[0], 0.0], np.float32)   # decimetres to metres, feet on the ground
                n = vnorm[v]
                if REPOSE[0] is not None:
                    p = REPOSE[0].point(p, per_vert[v]).astype(np.float32)
                    n = REPOSE[0].direction(n, per_vert[v]).astype(np.float32)
                positions.append(p)
                normals.append(n)
                t = texco[uv] if uv < len(texco) else (0.0, 0.0)
                uvs.append((float(t[0]), 1.0 - float(t[1])))
                top = sorted(per_vert[v], key=lambda bw: -bw[1])[:4]
                total = sum(w for _, w in top) or 1.0
                joints.append([b for b, _ in top] + [None] * (4 - len(top)))
                wts.append([w / total for _, w in top] + [0.0] * (4 - len(top)))
            corners.append(pairs[key])
        # Quads (and triangles, whose last corner repeats one of the others).
        if len(corners) == 4 and corners[3] != corners[2] and corners[3] != corners[0]:
            tris += [(corners[0], corners[1], corners[2]), (corners[0], corners[2], corners[3])]
        else:
            tris.append((corners[0], corners[1], corners[2]))
    if drop_bones:
        # The open edge where the head was: closed with a fan to its centre, skinned to the cap bone.
        for a, b, c in tris:
            for e in ((a, b), (b, c), (c, a)):
                k = (min(e), max(e))
                edge_count[k] = edge_count.get(k, 0) + 1
        rim = sorted({v for e, n in edge_count.items() if n == 1 for v in e},
                     key=lambda v: math.atan2(positions[v][2] - 0.0, positions[v][0] - 0.0))
        rim = [v for v in rim if positions[v][1] > 1.0]      # the neck's rim, not the soles' (closed anyway)
        if len(rim) >= 3:
            centre = np.mean([positions[v] for v in rim], axis=0)
            ci = len(positions)
            positions.append(centre.astype(np.float32)); normals.append(np.array([0.0, 1.0, 0.0], np.float32)); uvs.append((0.5, 0.5))
            joints.append([cap_bone, None, None, None]); wts.append([1.0, 0.0, 0.0, 0.0])
            for i in range(len(rim)):
                tris.append((ci, rim[i], rim[(i + 1) % len(rim)]))
                tris.append((ci, rim[(i + 1) % len(rim)], rim[i]))
    return positions, normals, uvs, joints, wts, tris


def rig_nodes(skel):
    """glTF nodes for the bones, in skeleton order, with local matrices in metres, and each bone's global
    inverse bind matrix."""
    bones = skel.getBones()
    index = {b.name: i for i, b in enumerate(bones)}
    nodes, ibms = [], []
    for b in bones:
        if REPOSE[0] is not None:
            g = REPOSE[0].new_global[b.name]
            local = la.inv(REPOSE[0].new_global[b.parent.name]).dot(g) if b.parent is not None else g
        else:
            g = np.array(b.matRestGlobal, dtype=np.float64)
            g[:3, 3] *= 0.1
            g[1, 3] += GROUND[0]
            if b.parent is not None:
                pg = np.array(b.parent.matRestGlobal, dtype=np.float64)
                pg[:3, 3] *= 0.1
                pg[1, 3] += GROUND[0]
                local = la.inv(pg).dot(g)
            else:
                local = g
        node = {"name": RENAME.get(b.name, b.name), "matrix": [float(x) for x in local.T.flatten()]}
        kids = [index[c.name] for c in b.children]
        if kids:
            node["children"] = kids
        nodes.append(node)
        ibms.append(la.inv(g))
    return nodes, ibms, index


def write_gltf(path, skel, meshes):
    """meshes: list of (name, positions, normals, uvs, joints, weights, tris, texture_path)."""
    nodes, ibms, bone_index = rig_nodes(skel)
    buf = bytearray()
    views, accessors = [], []

    def put(data, fmt, count, kind, ctype, target=None, minmax=False):
        start = len(buf)
        buf.extend(struct.pack("<%d%s" % (len(data), fmt), *data))
        while len(buf) % 4:
            buf.append(0)
        view = {"buffer": 0, "byteOffset": start, "byteLength": len(buf) - start}
        if target:
            view["target"] = target
        views.append(view)
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if minmax:
            width = {"VEC3": 3, "VEC2": 2, "VEC4": 4, "SCALAR": 1, "MAT4": 16}[kind]
            cols = [data[i::width] for i in range(width)]
            acc["min"] = [float(min(c)) for c in cols]
            acc["max"] = [float(max(c)) for c in cols]
        accessors.append(acc)
        return len(accessors) - 1

    ibm_acc = put([float(x) for m in ibms for x in m.T.flatten()], "f", len(ibms), "MAT4", 5126)
    gltf_meshes, images, textures, materials, scene_nodes = [], [], [], [], []
    for name, positions, normals, uvs, joints, weights, tris, texture, slot_name in meshes:
        pos_acc = put([float(x) for p in positions for x in p], "f", len(positions), "VEC3", 5126, 34962, True)
        nrm_acc = put([float(x) for n in normals for x in n], "f", len(normals), "VEC3", 5126, 34962)
        uv_acc = put([float(x) for t in uvs for x in t], "f", len(uvs), "VEC2", 5126, 34962)
        j_acc = put([bone_index[j] if j else 0 for js in joints for j in js], "H", len(joints), "VEC4", 5123, 34962)
        w_acc = put([float(w) for ws in weights for w in ws], "f", len(weights), "VEC4", 5126, 34962)
        idx_acc = put([i for t in tris for i in t], "I", len(tris) * 3, "SCALAR", 5125, 34963)
        mat = {"name": slot_name, "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.6}, "doubleSided": True}
        if texture and os.path.exists(texture):
            images.append({"uri": os.path.basename(texture)})
            textures.append({"source": len(images) - 1})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
            mat["alphaMode"] = "BLEND" if name != "Body" else "OPAQUE"
            target = os.path.join(os.path.dirname(path), os.path.basename(texture))
            if not os.path.exists(target):
                with open(texture, "rb") as src, open(target, "wb") as dst:
                    dst.write(src.read())
        materials.append(mat)
        gltf_meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc, "TEXCOORD_0": uv_acc,
                                                                            "JOINTS_0": j_acc, "WEIGHTS_0": w_acc},
                                                              "indices": idx_acc, "material": len(materials) - 1}]})
        nodes.append({"name": name, "mesh": len(gltf_meshes) - 1, "skin": 0})
        scene_nodes.append(len(nodes) - 1)
    bin_path = os.path.splitext(path)[0] + ".bin"
    with open(bin_path, "wb") as f:
        f.write(bytes(buf))
    doc = {
        "asset": {"version": "2.0", "generator": "Riptide mh_export.py (MakeHuman 1.2.0, CC0)"},
        "scene": 0, "scenes": [{"nodes": [0] + scene_nodes}],
        "nodes": nodes, "skins": [{"joints": list(range(len(ibms))), "inverseBindMatrices": ibm_acc, "skeleton": 0}],
        "meshes": gltf_meshes, "materials": materials, "images": images, "textures": textures,
        "buffers": [{"uri": os.path.basename(bin_path), "byteLength": len(buf)}], "bufferViews": views, "accessors": accessors,
    }
    if not images:
        doc.pop("images"), doc.pop("textures")
    with open(path, "w") as f:
        json.dump(doc, f)


def collect(h, objects, slots, drop_bones=(), cap_bone=None):
    """The human's objects as (name, arrays..., texture, material slot name) with weights on the game-engine rig."""
    skel = h.getSkeleton()
    raw = h.getVertexWeights(skel)
    out = []
    for name, obj in objects:
        if obj.proxy:
            obj.proxy.update(obj.mesh)      # fitted to this body (the window's change events would have done it)
        mesh = obj.mesh.clone(1.0, filterMaskedVerts=True)
        parent = obj.proxy.getVertexWeights(raw, skel) if obj.proxy else raw
        weights = mesh.getVertexWeights(parent)
        texture = obj.material.diffuseTexture
        arrays = mesh_arrays(mesh, weights, drop_bones, cap_bone)
        log("%s: %d vertices, %d triangles, texture %s" % (name, len(arrays[0]), len(arrays[5]), texture))
        out.append((name,) + arrays + (texture, slots[name]))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for body, spec in LOOKS.items():
        log("building " + body)
        h = build_human(spec)
        folder = os.path.join(OUT, body)
        os.makedirs(folder, exist_ok=True)
        # Hide the helper geometry (joint markers, hair and clothes helpers) that the base mesh carries.
        mesh = h.meshData
        groups = mesh.getFaceGroups() if hasattr(mesh, "getFaceGroups") else []
        hidden = [str(g if isinstance(g, str) else g.name) for g in groups]
        hidden = [g for g in hidden if g.startswith("helper") or g.startswith("joint")]
        if hidden:
            mesh.changeFaceMask(np.logical_not(mesh.getFaceMaskForGroups(hidden)))
        GROUND[0] = -float(np.min(np.asarray(mesh.coord)[:, 1])) * 0.1
        log("%s stands %.3f m tall" % (body, float(np.max(np.asarray(mesh.coord)[:, 1])) * 0.1 + GROUND[0]))
        REPOSE[0] = Repose(h.getSkeleton(), GROUND[0])
        parts = [("Body", h), ("Eyes", h.getEyesProxy().object), ("Eyebrows", h.getEyebrowsProxy().object),
                 ("Eyelashes", h.getEyelashesProxy().object), ("Teeth", h.getTeethProxy().object)]
        slots = {"Body": "Skin_" + body, "Eyes": "Eyes", "Eyebrows": "Brows_" + body, "Eyelashes": "Lashes", "Teeth": "Teeth"}
        write_gltf(os.path.join(folder, body + "_FullBody.gltf"), h.getSkeleton(), collect(h, parts, slots))
        # The body seen from one's own eyes: no head, the neck closed, so looking down shows a chest, not a funnel.
        write_gltf(os.path.join(folder, body + "_FullBody_FP.gltf"), h.getSkeleton(),
                   collect(h, [("Body", h)], slots, drop_bones=("head",), cap_bone="neck_01"))
        for asset, style in HAIR.items():
            h.setHairProxy(load_proxy(h, "hair/%s/%s.mhpxy" % (style, style), "Hair"))
            write_gltf(os.path.join(folder, asset + ".gltf"), h.getSkeleton(), collect(h, [("Hair", h.getHairProxy().object)], {"Hair": "Hair_" + style}))
        log("wrote " + folder)
    log("done")


main()
