#!/usr/bin/env python3
"""
M1 Tank Platoon II - Native Modding & Asset Toolkit (m1tp2_modtool.py)

Supports:
  - cat-list   : List entries in m1.cat / 3dz.cat with automatic filename recovery
  - cat-unpack : Unpack .cat archive entries to loose files ready for mods/<mod>/
  - cat-repack : Repack a directory tree into a sorted binary .cat archive
  - 3dz-info   : Inspect a .3dz 3D model (loose file or inside Game/3dz.cat)
  - 3dz-to-obj : Export a .3dz 3D model (+ UVs & .mtl) to Wavefront .obj for Blender
  - dump-units : Dump M1TP2's embedded unit & ordnance tables from M1TP2.exe
  - new-mod    : Scaffold a ready-to-use mod folder under mods/<name>/
"""

import argparse
import json
import os
import re
import struct
import sys
from pathlib import Path

GAME_ROOT = Path(__file__).resolve().parents[2]


# ---------------------------------------------------------------------------
# Exact M1TP2 Catalog.c Hash (0x427180)
# ---------------------------------------------------------------------------
def catalog_hash(name: str) -> int:
    """Exact 32-bit hash used by M1TP2's Catalog.c (0x427180)."""
    norm = name.replace("/", "\\").lower()
    b = norm.encode("latin1", errors="ignore")
    if not b:
        return 0
    b0 = b[0]
    b1 = b[1] if len(b) > 1 else 0
    top = ((b1 ^ b0) & 0xFF) << 24
    odd = 0
    even = 0
    w = 3
    for ch in b:
        c = ch if ch < 128 else ch - 256
        if w & 1:
            odd ^= (w * c) & 0xFFFFFFFF
        else:
            even ^= (w * c) & 0xFFFFFFFF
        w += 1
    return (top | (((odd << 8) ^ even) & 0x00FFFFFF)) & 0xFFFFFFFF


def build_filename_dictionary(game_root: Path) -> dict[int, str]:
    """Scan M1TP2.exe, 3dz.cat, and game text/dat files to recover hashed .cat names."""
    raw_strings: set[str] = set()

    exe_path = game_root / "M1TP2.exe"
    if exe_path.is_file():
        exe = exe_path.read_bytes()
        for m in re.finditer(rb"[A-Za-z0-9_\\/\.\-&+~]{3,80}", exe):
            raw_strings.add(m.group(0).decode("latin1"))

    cat3_path = game_root / "Game" / "3dz.cat"
    if cat3_path.is_file():
        cat3 = cat3_path.read_bytes()
        for m in re.finditer(rb"[A-Za-z0-9_\-&+]+\.(?:pcx|PCX|3dz|3DZ)", cat3):
            raw_strings.add(m.group(0).decode("latin1"))

    skip_dirs = {"recomp", "_gog_orig", "Boxedwine", "Dsg", "mods"}
    for dirpath, dirnames, fnames in os.walk(game_root):
        dirnames[:] = [d for d in dirnames if d not in skip_dirs]
        for fn in fnames:
            raw_strings.add(fn)
            if fn.lower().endswith((".txt", ".dat", ".h", ".cfg")):
                try:
                    data = Path(dirpath, fn).read_bytes()
                    for m in re.finditer(rb"[A-Za-z0-9_\\/\.\-&+]+", data):
                        raw_strings.add(m.group(0).decode("latin1"))
                except OSError:
                    pass

    # Also expand numbered tile/texture variants (e.g. sand0..7, s&glt0..7, m1a2_h/t/d)
    stems = set()
    for s in list(raw_strings):
        sl = s.lower()
        if sl.endswith(".pcx") and "\\" not in sl and "/" not in sl:
            stems.add(sl)

    prefixes = [
        "", "3dz\\", "pics\\", "vpics\\", "dpics\\", "thermal\\", "map\\",
        "cockpits\\", "cockpits\\commander\\", "cockpits\\gunner\\",
        "cockpits\\commander\\sight\\", "cockpits\\gunner\\sight\\",
        "cockpits\\map\\", "cockpits\\common\\", "game\\", "art\\pics\\",
    ]

    h2n: dict[int, str] = {}

    def register(cand: str) -> None:
        c = cand.strip().replace("/", "\\").lower()
        if not c or len(c) > 128:
            return
        h = catalog_hash(c)
        if h not in h2n:
            h2n[h] = c

    for s in raw_strings:
        register(s)
        sl = s.replace("/", "\\").lower()
        if sl.startswith("game\\"):
            register(sl[5:])
        if "." in sl and "\\" not in sl:
            for p in prefixes:
                register(p + sl)

    return h2n


def read_cat(cat_path: Path) -> tuple[bytes, list[tuple[int, int, int]]]:
    data = cat_path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"Invalid .cat file: {cat_path}")
    (count,) = struct.unpack_from("<I", data, 0)
    entries = []
    for i in range(count):
        h, off, sz = struct.unpack_from("<III", data, 4 + i * 12)
        entries.append((h, off, sz))
    return data, entries


def detect_magic_ext(blob: bytes) -> str:
    if len(blob) >= 4 and blob[0] == 0x0A and blob[1] in (0, 2, 3, 5) and blob[2] == 1 and blob[3] in (1, 4, 8):
        return ".pcx"
    if len(blob) >= 16 and b".PCX" in blob[2:16].upper():
        return ".3dz"
    if blob[:4] == b"RIFF":
        return ".wav"
    return ".bin"


# ---------------------------------------------------------------------------
# .3DZ Binary Parser & Wavefront OBJ Exporter
# ---------------------------------------------------------------------------
def parse_3dz(data: bytes) -> dict:
    if len(data) < 22:
        raise ValueError("File too small to be a valid .3dz model")
    (radius,) = struct.unpack_from("<H", data, 0)
    tex_name = data[2:16].split(b"\x00")[0].decode("latin1", errors="replace").strip()
    n_ins, n_pts, n_norms = struct.unpack_from("<hhh", data, 16)
    pos = 22

    inserts = []
    if n_ins > 1:
        for _ in range(n_ins - 1):
            axis, var_idx, dx, dy, dz = struct.unpack_from("<bbhhh", data, pos)
            inserts.append({"axis": axis, "var": var_idx, "offset": [dx, dy, dz]})
            pos += 8

    normals = []
    for _ in range(n_norms):
        nx, ny, nz, dist, flags = struct.unpack_from("<hhhiH", data, pos)
        normals.append({"n": [nx, ny, nz], "dist": dist, "flags": flags})
        pos += 12

    # BSP root header (2 bytes) + BSP node links (n_norms * 2 bytes)
    pos += 2 + n_norms * 2

    vertices = []
    for _ in range(n_pts):
        x, y, z, ins_idx = struct.unpack_from("<hhhh", data, pos)
        vertices.append((x, y, z, ins_idx))
        pos += 8

    # Element offset table: n_norms * uint16 absolute offsets into the file
    elem_offsets = []
    for _ in range(n_norms):
        if pos + 2 <= len(data):
            (eo,) = struct.unpack_from("<H", data, pos)
            elem_offsets.append(eo)
            pos += 2

    faces = []
    seen_streams: set[int] = set()
    for norm_idx, start_off in enumerate(elem_offsets):
        if start_off in seen_streams or start_off >= len(data):
            continue
        seen_streams.add(start_off)
        ep = start_off
        while ep < len(data):
            b = data[ep]
            if b == 0xFF:
                break
            cond = None
            if b & 0x80:
                if ep + 2 >= len(data):
                    break
                cond = (b & 0x7F, data[ep + 1])
                ep += 2
            op = data[ep] & 0x7F
            ep += 1
            if op == 0:
                ep += 2
            elif op == 1:
                ep += 3
            elif op == 2:
                ep += 2
            elif op == 3:
                if ep >= len(data):
                    break
                cnt = data[ep]
                ep += 1
                idxs = list(data[ep : ep + cnt])
                color = data[ep + cnt] if ep + cnt < len(data) else 0
                ep += cnt + 1
                if cnt >= 3 and all(i < n_pts for i in idxs):
                    faces.append({"kind": "flat", "verts": idxs, "uvs": [], "color": color, "cond": cond})
            elif op == 4:
                if ep >= len(data):
                    break
                cnt = data[ep]
                ep += 1
                idxs = [data[ep + k * 2] for k in range(cnt) if ep + k * 2 < len(data)]
                ep += cnt * 2
                if len(idxs) >= 3 and all(i < n_pts for i in idxs):
                    faces.append({"kind": "gouraud", "verts": idxs, "uvs": [], "cond": cond})
            elif op in (5, 6):
                if ep >= len(data):
                    break
                cnt = data[ep]
                ep += 1
                idxs = []
                uvs = []
                for k in range(cnt):
                    if ep + 3 > len(data):
                        break
                    vi, u, v = data[ep], data[ep + 1], data[ep + 2]
                    idxs.append(vi)
                    uvs.append((u / 255.0, 1.0 - v / 255.0))
                    ep += 3
                ep += 2  # trailing shading/normal bytes
                if len(idxs) >= 3 and all(i < n_pts for i in idxs):
                    faces.append({
                        "kind": "textured" if op == 5 else "textured_alpha",
                        "verts": idxs,
                        "uvs": uvs,
                        "cond": cond,
                    })
            else:
                break

    return {
        "radius": radius,
        "texture": tex_name,
        "num_inserts": n_ins,
        "num_points": n_pts,
        "num_normals": n_norms,
        "inserts": inserts,
        "vertices": vertices,
        "faces": faces,
    }


def load_3dz_source(spec: str, game_root: Path) -> tuple[str, bytes]:
    p = Path(spec)
    if p.is_file():
        return p.name, p.read_bytes()
    # Look inside Game/3dz.cat by name (e.g. "m1a2_h.3dz" or "3dz\m1a2_h.3dz")
    cat_path = game_root / "Game" / "3dz.cat"
    if not cat_path.is_file():
        raise FileNotFoundError(f"File not found: {spec}")
    key = spec.replace("/", "\\").lower()
    if not key.startswith("3dz\\"):
        key = "3dz\\" + key
    target_hash = catalog_hash(key)
    data, entries = read_cat(cat_path)
    for h, off, sz in entries:
        if h == target_hash:
            return Path(key).name, data[off : off + sz]
    raise FileNotFoundError(f"'{spec}' not found on disk or in {cat_path} (hash=0x{target_hash:08X})")


def export_3dz_to_obj(model: dict, out_obj: Path, scale: float = 0.01) -> None:
    out_obj.parent.mkdir(parents=True, exist_ok=True)
    mtl_path = out_obj.with_suffix(".mtl")
    tex_name = model["texture"] or "texture.pcx"

    with mtl_path.open("w", encoding="utf-8") as mf:
        mf.write("# M1 Tank Platoon II .3DZ Material\n")
        mf.write("newmtl m1tp2_tex\n")
        mf.write("Ka 1.0 1.0 1.0\nKd 1.0 1.0 1.0\nKs 0.0 0.0 0.0\n")
        mf.write(f"map_Kd {tex_name}\n\n")
        mf.write("newmtl m1tp2_flat\n")
        mf.write("Ka 0.7 0.7 0.7\nKd 0.7 0.7 0.7\n")

    with out_obj.open("w", encoding="utf-8") as f:
        f.write(f"# Exported by m1tp2_modtool.py | Texture: {tex_name}\n")
        f.write(f"# Radius: {model['radius']} | Inserts: {model['num_inserts']} | Verts: {model['num_points']} | Faces: {len(model['faces'])}\n")
        f.write(f"mtllib {mtl_path.name}\n\n")

        # Write vertices (apply insert pivot offset for sub-parts so turrets/barrels align)
        inserts = model["inserts"]
        for x, y, z, ins_idx in model["vertices"]:
            ox, oy, oz = x, y, z
            if 1 <= ins_idx <= len(inserts):
                dx, dy, dz = inserts[ins_idx - 1]["offset"]
                ox += dx
                oy += dy
                oz += dz
            # M1TP2 coords: X right, Y up, Z forward -> flip Z for right-handed OBJ
            f.write(f"v {ox * scale:.5f} {oy * scale:.5f} {-oz * scale:.5f}\n")

        f.write("\n")
        vt_index = 1
        cur_mtl = None
        for fi, face in enumerate(model["faces"]):
            want_mtl = "m1tp2_tex" if face["uvs"] else "m1tp2_flat"
            if want_mtl != cur_mtl:
                f.write(f"usemtl {want_mtl}\n")
                cur_mtl = want_mtl
            if face["uvs"]:
                refs = []
                for vi, (u, v) in zip(face["verts"], face["uvs"]):
                    f.write(f"vt {u:.5f} {v:.5f}\n")
                    refs.append(f"{vi + 1}/{vt_index}")
                    vt_index += 1
                f.write("f " + " ".join(refs) + "\n")
            else:
                refs = [str(vi + 1) for vi in face["verts"]]
                f.write("f " + " ".join(refs) + "\n")


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_cat_list(args: argparse.Namespace) -> None:
    cat_path = Path(args.catfile)
    if not cat_path.is_file():
        cat_path = GAME_ROOT / "Game" / args.catfile
    data, entries = read_cat(cat_path)
    h2n = build_filename_dictionary(GAME_ROOT)
    filt = (args.filter or "").lower()

    matched = 0
    shown = 0
    for i, (h, off, sz) in enumerate(entries):
        name = h2n.get(h)
        if name:
            matched += 1
        label = name or f"<unknown:0x{h:08X}>{detect_magic_ext(data[off:off+16])}"
        if filt and filt not in label.lower():
            continue
        if shown < args.limit:
            print(f"  [{i:4d}] 0x{h:08X}  {sz:8d} B  {label}")
        shown += 1

    print(f"\nArchive: {cat_path} ({len(entries)} entries, {matched}/{len(entries)} = {matched*100.0/max(1,len(entries)):.1f}% names recovered, {shown} matched filter)")


def cmd_cat_unpack(args: argparse.Namespace) -> None:
    cat_path = Path(args.catfile)
    if not cat_path.is_file():
        cat_path = GAME_ROOT / "Game" / args.catfile
    out_dir = Path(args.outdir)
    data, entries = read_cat(cat_path)
    h2n = build_filename_dictionary(GAME_ROOT)
    filt = (args.filter or "").lower()

    extracted = 0
    manifest = []
    for h, off, sz in entries:
        name = h2n.get(h)
        blob = data[off : off + sz]
        rel = name if name else f"_unknown\\0x{h:08X}{detect_magic_ext(blob)}"
        if filt and filt not in rel.lower():
            continue
        dest = out_dir / rel.replace("\\", os.sep)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
        manifest.append({"hash": f"0x{h:08X}", "path": rel, "size": sz})
        extracted += 1

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Unpacked {extracted} files from {cat_path.name} -> {out_dir}")


def cmd_cat_repack(args: argparse.Namespace) -> None:
    in_dir = Path(args.indir)
    out_cat = Path(args.outcat)
    records = []
    for dirpath, _, fnames in os.walk(in_dir):
        for fn in fnames:
            if fn == "_manifest.json":
                continue
            full = Path(dirpath, fn)
            rel = str(full.relative_to(in_dir)).replace("/", "\\")
            if rel.lower().startswith("_unknown\\0x"):
                h = int(Path(fn).stem, 16)
            else:
                h = catalog_hash(rel)
            records.append((h, rel, full.read_bytes()))

    # M1TP2 CatalogFindEntry uses signed-int bsearch on hash (see 0x4270A0: cmp edx, ecx; jge)!
    records.sort(key=lambda r: struct.unpack("<i", struct.pack("<I", r[0]))[0])

    header_size = 4 + len(records) * 12
    # Align data payloads to 2048-byte sector boundaries like stock .cat files
    cur_off = (header_size + 2047) & ~2047
    table = bytearray()
    table += struct.pack("<I", len(records))
    body = bytearray(cur_off - header_size)
    for h, rel, blob in records:
        table += struct.pack("<III", h, cur_off, len(blob))
        body += blob
        pad = ((cur_off + len(blob) + 2047) & ~2047) - (cur_off + len(blob))
        body += b"\x00" * pad
        cur_off += len(blob) + pad

    out_cat.parent.mkdir(parents=True, exist_ok=True)
    out_cat.write_bytes(table + body)
    print(f"Repacked {len(records)} files -> {out_cat} ({out_cat.stat().st_size:,} bytes)")


def cmd_3dz_info(args: argparse.Namespace) -> None:
    name, blob = load_3dz_source(args.model, GAME_ROOT)
    m = parse_3dz(blob)
    kinds: dict[str, int] = {}
    for fc in m["faces"]:
        kinds[fc["kind"]] = kinds.get(fc["kind"], 0) + 1
    print(f"Model:    {name} ({len(blob)} bytes)")
    print(f"Texture:  {m['texture']}")
    print(f"Radius:   {m['radius']}")
    print(f"Inserts:  {m['num_inserts']} {m['inserts']}")
    print(f"Vertices: {m['num_points']}")
    print(f"Normals:  {m['num_normals']}")
    print(f"Faces:    {len(m['faces'])} {kinds}")


def cmd_3dz_to_obj(args: argparse.Namespace) -> None:
    name, blob = load_3dz_source(args.model, GAME_ROOT)
    m = parse_3dz(blob)
    out_obj = Path(args.out_obj)
    export_3dz_to_obj(m, out_obj, scale=args.scale)
    print(f"Exported {name} -> {out_obj} ({m['num_points']} verts, {len(m['faces'])} faces, tex={m['texture']})")


def cmd_dump_units(args: argparse.Namespace) -> None:
    exe = (GAME_ROOT / "M1TP2.exe").read_bytes()
    # Read all 94 UnitType structs at VA 0x479A80 (stride 0x80) using PE section mapping
    # Locate PE .data section containing VA 0x479A80
    pe_off, = struct.unpack_from("<I", exe, 0x3C)
    num_sec, = struct.unpack_from("<H", exe, pe_off + 6)
    opt_sz, = struct.unpack_from("<H", exe, pe_off + 20)
    image_base, = struct.unpack_from("<I", exe, pe_off + 52)
    sec_off = pe_off + 24 + opt_sz

    sections = []
    for i in range(num_sec):
        s_va, s_raw_sz, s_raw_ptr = struct.unpack_from("<III", exe, sec_off + i * 40 + 12)
        sections.append((image_base + s_va, s_raw_ptr, s_raw_sz))

    def va_to_off(va: int) -> int:
        for base_va, raw_ptr, raw_sz in sections:
            if base_va <= va < base_va + raw_sz:
                return raw_ptr + (va - base_va)
        return -1

    units = []
    for idx in range(94):
        off = va_to_off(0x479A80 + idx * 0x80)
        if off < 0:
            break
        name_va, = struct.unpack_from("<I", exe, off)
        noff = va_to_off(name_va)
        name = exe[noff : noff + 32].split(b"\x00")[0].decode("latin1", errors="replace") if noff >= 0 else f"UNIT_{idx}"
        f = list(struct.unpack_from("<31i", exe, off + 4))
        units.append({
            "index": idx,
            "name": name,
            "class_id": f[0],
            "top_speed": f[19],
            "weapons": f[4:10],
            "ammo": f[10:16],
            "fields": {f"f{k}": f[k] for k in range(31)},
        })

    result = {
        "_comment": "Drop into mods/<your_mod>/units.json to override any unit's stats live at runtime!",
        "unit_count": len(units),
        "units": units,
    }
    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(f"Wrote {len(units)} structured UnitType records -> {args.out}")
    else:
        print(json.dumps(result, indent=2))


def cmd_new_mod(args: argparse.Namespace) -> None:
    mod_dir = GAME_ROOT / "mods" / args.name
    for sub in ["3dz", "pics", "vpics", "cockpits\\commander", "BATTLES", "World"]:
        (mod_dir / sub).mkdir(parents=True, exist_ok=True)
    (mod_dir / "mod.txt").write_text(
        f"name = {args.name}\nversion = 1.0\nauthor = Modder\ndescription = Custom M1TP2 Native Recomp Mod\n",
        encoding="utf-8",
    )
    print(f"Created mod scaffold at: {mod_dir}")
    print("Drop modified .pcx, .3dz, .dat, .txt, or .wav files inside and launch m1tp2_native.exe!")


def main() -> None:
    ap = argparse.ArgumentParser(description="M1 Tank Platoon II Native Modding Toolkit")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("cat-list", help="List entries in a .cat archive with recovered filenames")
    p.add_argument("catfile", help="Path to .cat file (e.g. m1.cat or 3dz.cat)")
    p.add_argument("--filter", default="", help="Substring filter")
    p.add_argument("--limit", type=int, default=60, help="Max entries to display")
    p.set_defaults(func=cmd_cat_list)

    p = sub.add_parser("cat-unpack", help="Unpack a .cat archive into a folder")
    p.add_argument("catfile", help="Path to .cat file (e.g. m1.cat or 3dz.cat)")
    p.add_argument("outdir", help="Output directory")
    p.add_argument("--filter", default="", help="Optional substring filter (e.g. m1a2)")
    p.set_defaults(func=cmd_cat_unpack)

    p = sub.add_parser("cat-repack", help="Repack a directory into a sorted .cat archive")
    p.add_argument("indir", help="Input directory")
    p.add_argument("outcat", help="Output .cat file path")
    p.set_defaults(func=cmd_cat_repack)

    p = sub.add_parser("3dz-info", help="Inspect a .3dz 3D model (file or name in 3dz.cat)")
    p.add_argument("model", help="Path to .3dz file or name inside 3dz.cat (e.g. m1a2_h.3dz)")
    p.set_defaults(func=cmd_3dz_info)

    p = sub.add_parser("3dz-to-obj", help="Export a .3dz 3D model + UVs to Wavefront .obj/.mtl")
    p.add_argument("model", help="Path to .3dz file or name inside 3dz.cat (e.g. m1a2_h.3dz)")
    p.add_argument("out_obj", help="Destination .obj file path")
    p.add_argument("--scale", type=float, default=0.01, help="Coordinate scale (default 0.01)")
    p.set_defaults(func=cmd_3dz_to_obj)

    p = sub.add_parser("dump-units", help="Dump embedded vehicle & ordnance tables from M1TP2.exe")
    p.add_argument("--out", default="", help="Optional output JSON path")
    p.set_defaults(func=cmd_dump_units)

    p = sub.add_parser("new-mod", help="Create a ready-to-use mod skeleton under mods/<name>/")
    p.add_argument("name", help="Mod folder name")
    p.set_defaults(func=cmd_new_mod)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
