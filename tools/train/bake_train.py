"""train_car.glb -> 게임용 파일 (Cube/Cube/assets/train/)
  train_car.mesh : 'CUBM', 정점 수(u32), 인덱스 수(u32), 정점(위치3 법선3 UV2, float32), 인덱스(u32)
  train_color.jpg, train_orm.jpg, train_normal.png : TEX 크기로 줄인 텍스처
실행: python tools/train/bake_train.py   (numpy, Pillow 필요)
"""
import io, json, struct
from pathlib import Path
import numpy as np
from PIL import Image

TEX = 1024
DIR = Path(__file__).resolve().parents[2] / "Cube" / "Cube" / "assets" / "train"

f = (DIR / "train_car.glb").read_bytes()
jl = struct.unpack("<I", f[12:16])[0]
g = json.loads(f[20:20 + jl])
b = f[20 + jl + 8:]

def view(i):
    v = g["bufferViews"][i]
    assert "byteStride" not in v
    o = v.get("byteOffset", 0)
    return b[o:o + v["byteLength"]]

def acc(i, n):
    a = g["accessors"][i]
    t = {5125: "<u4", 5123: "<u2", 5126: "<f4"}[a["componentType"]]
    return np.frombuffer(view(a["bufferView"]), t, a["count"] * n, a.get("byteOffset", 0)).reshape(-1, n)

p = g["meshes"][0]["primitives"][0]
at = p["attributes"]
pos, nor, uv = acc(at["POSITION"], 3), acc(at["NORMAL"], 3), acc(at["TEXCOORD_0"], 2)
idx = acc(p["indices"], 1).astype("<u4")
verts = np.hstack([pos, nor, uv]).astype("<f4")
assert idx.max() < len(verts) and np.isfinite(verts).all()
(DIR / "train_car.mesh").write_bytes(b"CUBM" + struct.pack("<II", len(verts), idx.size) + verts.tobytes() + idx.tobytes())

m = g["materials"][p["material"]]
pbr = m["pbrMetallicRoughness"]
for name, t in (("train_color.jpg", pbr["baseColorTexture"]), ("train_orm.jpg", pbr["metallicRoughnessTexture"]), ("train_normal.png", m["normalTexture"])):
    im = Image.open(io.BytesIO(view(g["images"][g["textures"][t["index"]]["source"]]["bufferView"]))).convert("RGB")
    im.resize((TEX, TEX), Image.LANCZOS).save(DIR / name, **({"quality": 90} if name.endswith("jpg") else {"optimize": True}))
print(f"bake_train OK verts {len(verts)} tris {idx.size // 3} tex {TEX}")
