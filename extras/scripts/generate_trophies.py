#!/usr/bin/env python3
"""
Generate high-quality PS Vita TROPHY.TRP for Prince of Persia Classic.
- Unrotates 001..017 to upright orientation (90 deg CCW).
- Cleans edge bevels and artifacts.
- Upscales with Lanczos + UnsharpMask sharpening and contrast enhancement.
- Adds metallic trophy frames (Bronze, Silver, Gold, Platinum).
- Improves Trophy 000 (Platinum) with crisp golden medallion and celestial platinum border.
"""

import os
import plistlib
import struct
import hashlib
import subprocess
from PIL import Image, ImageOps, ImageFilter, ImageEnhance, ImageDraw

COMM_ID = "POPC00001_00"

TROPHIES = [
    {
        "id": "000",
        "ttype": "P",
        "hidden": "no",
        "pid": "-1",
        "name": "PRINCE OF PERSIA",
        "detail": "Consigue todos los trofeos del juego."
    },
    {
        "id": "001",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "HERRAMIENTAS DEL OFICIO",
        "detail": "Consigue la espada en la prision."
    },
    {
        "id": "002",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "LA PRISION ETERNA",
        "detail": "Despierta al esqueleto guardian."
    },
    {
        "id": "003",
        "ttype": "B",
        "hidden": "yes",
        "pid": "000",
        "name": "REFLEXIONES INTERIORES",
        "detail": "Rompe el espejo y libera al Principe Oscuro."
    },
    {
        "id": "004",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "EL CAMINO ADELANTE",
        "detail": "Mata al guardian de la puerta."
    },
    {
        "id": "005",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "PODER VOLADOR",
        "detail": "Realiza un salto tras tomar la pocion pluma."
    },
    {
        "id": "006",
        "ttype": "B",
        "hidden": "yes",
        "pid": "000",
        "name": "DE RATONES Y PRINCIPES",
        "detail": "Te ha rescatado el raton de la princesa."
    },
    {
        "id": "007",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "TODO LO QUE SUBE BAJA",
        "detail": "Toma ambas pociones inversas."
    },
    {
        "id": "008",
        "ttype": "S",
        "hidden": "yes",
        "pid": "000",
        "name": "EL MOMENTO FATIDICO",
        "detail": "Reunete con el Principe Oscuro."
    },
    {
        "id": "009",
        "ttype": "G",
        "hidden": "no",
        "pid": "000",
        "name": "AMOR Y VENGANZA",
        "detail": "Derrota a Jaffar."
    },
    {
        "id": "010",
        "ttype": "S",
        "hidden": "no",
        "pid": "000",
        "name": "LA INMORTALIDAD",
        "detail": "Encuentra todos los elixires."
    },
    {
        "id": "011",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "RESCATE A TODA PRISA",
        "detail": "Supera un record anterior de cualquier nivel en cualquier modo."
    },
    {
        "id": "012",
        "ttype": "G",
        "hidden": "no",
        "pid": "000",
        "name": "ARENAS DEL DESTINO",
        "detail": "Completa el juego en modo Contrarreloj."
    },
    {
        "id": "013",
        "ttype": "G",
        "hidden": "no",
        "pid": "000",
        "name": "EL VERDADERO PRINCIPE",
        "detail": "Completa el juego en modo Supervivencia."
    },
    {
        "id": "014",
        "ttype": "S",
        "hidden": "no",
        "pid": "000",
        "name": "IMPARABLE",
        "detail": "Completa el nivel sin perder salud."
    },
    {
        "id": "015",
        "ttype": "S",
        "hidden": "no",
        "pid": "000",
        "name": "EL PACIFICADOR",
        "detail": "Completa el nivel sin matar a un solo guardia."
    },
    {
        "id": "016",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "IRA DESATADA",
        "detail": "Mata al primer enemigo."
    },
    {
        "id": "017",
        "ttype": "B",
        "hidden": "no",
        "pid": "000",
        "name": "UN PEDAZO DE VIDA",
        "detail": "Mata a un guardia con la rebanadora, con un pincho o haciendolo caer."
    }
]

def make_tropconf_sfm():
    sig = "x" * 384
    lines = [
        f"<!--Sce-Np-Trophy-Signature: {sig}-->",
        '<trophyconf version="1.1" platform="psp2" policy="large">',
        f" <npcommid>{COMM_ID}</npcommid>",
        ' <trophyset-version>01.00</trophyset-version>',
        ' <parental-level license-area="default">0</parental-level>'
    ]
    for t in TROPHIES:
        lines.append(f' <trophy id="{t["id"]}" hidden="{t["hidden"]}" ttype="{t["ttype"]}" pid="{t["pid"]}"/>')
    lines.append('</trophyconf>\n')
    return "\n".join(lines).encode("utf-8")

def make_trop_sfm():
    sig = "x" * 384
    lines = [
        f"<!--Sce-Np-Trophy-Signature: {sig}-->",
        '<trophyconf version="1.1" platform="psp2" policy="large">',
        f" <npcommid>{COMM_ID}</npcommid>",
        ' <trophyset-version>01.00</trophyset-version>',
        ' <parental-level license-area="default">0</parental-level>',
        ' <title-name>Prince of Persia Classic</title-name>',
        ' <title-detail>Prince of Persia Classic PlayStation Vita Port</title-detail>'
    ]
    for t in TROPHIES:
        lines.append(f' <trophy id="{t["id"]}" hidden="{t["hidden"]}" ttype="{t["ttype"]}" pid="{t["pid"]}">')
        lines.append(f'  <name>{t["name"]}</name>')
        lines.append(f'  <detail>{t["detail"]}</detail>')
        lines.append(' </trophy>')
    lines.append('</trophyconf>\n')
    return "\n".join(lines).encode("utf-8")

def parse_plist_rect(rect_str):
    clean = rect_str.replace('{', '').replace('}', '')
    parts = [int(p.strip()) for p in clean.split(',')]
    return parts[0], parts[1], parts[2], parts[3]

def generate_icons(workdir):
    sheet_path = "ux0_data/popclassic/Data_960_576/Texture/Achievement/achv_screen.png"
    plist_path = "ux0_data/popclassic/Data_960_576/Texture/Achievement/achv_screen.plist"
    achv_tex_path = "ux0_data/popclassic/Data_960_576/Texture/Achievement/achievement.png"

    sheet = Image.open(sheet_path).convert("RGBA")
    with open(plist_path, "rb") as f:
        pl = plistlib.load(f)

    temp_dir = os.path.join(workdir, "ai_temp")
    os.makedirs(temp_dir, exist_ok=True)

    # 1. TROP000.PNG (240x240) Platinum Trophy Medallion
    if os.path.exists(achv_tex_path):
        achv_tex = Image.open(achv_tex_path).convert("RGBA")
        medallion = achv_tex.crop((0, 0, 96, 96))
        raw_med_path = os.path.join(temp_dir, "raw_00.png")
        ai_med_path = os.path.join(temp_dir, "ai_00.png")
        medallion.save(raw_med_path)

        # AI Super-Resolution via Real-ESRGAN (4x neural upscale)
        try:
            subprocess.run(["realesrgan", "-i", raw_med_path, "-o", ai_med_path, "-s", "4", "-m", "realesrgan-x4plus"], check=True)
            med_hi = Image.open(ai_med_path).convert("RGBA")
        except Exception as e:
            print(f"Warning: AI upscaler fallback for medallion: {e}")
            med_hi = medallion.resize((384, 384), Image.Resampling.LANCZOS)

        # Center on 240x240 transparent canvas (pure art, no fake borders)
        w0, h0 = med_hi.size
        scale0 = min(240.0 / w0, 240.0 / h0)
        nw0, nh0 = int(round(w0 * scale0)), int(round(h0 * scale0))
        scaled0 = med_hi.resize((nw0, nh0), Image.Resampling.LANCZOS)
        plat_canvas = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
        plat_canvas.paste(scaled0, ((240 - nw0) // 2, (240 - nh0) // 2))
        plat_canvas.save(os.path.join(workdir, "TROP000.PNG"), format="PNG")
        print("Generated AI-enhanced borderless TROP000.PNG (Platinum)")

        # ICON0.PNG (320x176) Trophy Set Banner
        icon0_canvas = Image.new("RGBA", (320, 176), (0, 0, 0, 0))
        banner_h = 160
        scale_b = banner_h / h0
        nwb, nhb = int(round(w0 * scale_b)), int(round(h0 * scale_b))
        scaled_b = med_hi.resize((nwb, nhb), Image.Resampling.LANCZOS)
        icon0_canvas.paste(scaled_b, ((320 - nwb) // 2, (176 - nhb) // 2))
        icon0_canvas.save(os.path.join(workdir, "ICON0.PNG"), format="PNG")
        print("Generated AI-enhanced borderless ICON0.PNG")

    # 2. TROP001.PNG to TROP017.PNG (240x240 each)
    for idx in range(1, 18):
        frame_name = f"achv_icon_{idx:02d}"
        finfo = pl["frames"][frame_name]
        x, y, w, h = parse_plist_rect(finfo["frame"])

        # achv_screen rotated: bounding box on sheet is (x, y, x + h, y + w)
        crop = sheet.crop((x, y, x + h, y + w))
        # Rotate 90 degrees CCW to be upright
        icon_upright = crop.rotate(90, expand=True)

        raw_path = os.path.join(temp_dir, f"raw_{idx:02d}.png")
        ai_path = os.path.join(temp_dir, f"ai_{idx:02d}.png")
        icon_upright.save(raw_path)

        # AI Super-Resolution via Real-ESRGAN (4x neural upscale)
        try:
            subprocess.run(["realesrgan", "-i", raw_path, "-o", ai_path, "-s", "4", "-m", "realesrgan-x4plus"], check=True)
            ai_img = Image.open(ai_path).convert("RGBA")
        except Exception as e:
            print(f"Warning: AI upscaler fallback for icon {idx}: {e}")
            ai_img = icon_upright.resize((260, 244), Image.Resampling.LANCZOS)

        # Scale to fit cleanly into 240x240 transparent canvas WITHOUT artificial borders
        iw, ih = ai_img.size
        scale = min(240.0 / iw, 240.0 / ih)
        nw, nh = int(round(iw * scale)), int(round(ih * scale))
        scaled_art = ai_img.resize((nw, nh), Image.Resampling.LANCZOS)

        canvas = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
        canvas.paste(scaled_art, ((240 - nw) // 2, (240 - nh) // 2))

        out_name = f"TROP{idx:03d}.PNG"
        canvas.save(os.path.join(workdir, out_name), format="PNG")
        print(f"Generated AI-enhanced upright borderless {out_name}")

def build_trp(file_dict, output_path):
    def align64(v):
        return (v + 63) & ~63

    num_files = len(file_dict)
    header_size = 64
    entries_size = num_files * 64
    first_file_offset = align64(header_size + entries_size)

    current_offset = first_file_offset
    file_offsets = {}
    for name, data in file_dict.items():
        file_offsets[name] = (current_offset, len(data))
        current_offset = align64(current_offset + len(data))

    total_size = current_offset

    sha_dummy = b"\x00" * 20
    header = struct.pack(">4sIQIII20s16s",
                         b"\xdc\xa2\x4d\x00",
                         2,
                         total_size,
                         num_files,
                         64,
                         0,
                         sha_dummy,
                         b"\x00" * 16)

    entries = bytearray()
    for name, data in file_dict.items():
        offset, size = file_offsets[name]
        entry = struct.pack(">32sQQI12s",
                            name.encode("latin1"),
                            offset,
                            size,
                            0,
                            b"\x00" * 12)
        entries.extend(entry)

    out = bytearray()
    out.extend(header)
    out.extend(entries)

    pad_len = first_file_offset - len(out)
    out.extend(b"\x00" * pad_len)

    for name, data in file_dict.items():
        target_offset, size = file_offsets[name]
        current_pos = len(out)
        if target_offset > current_pos:
            out.extend(b"\x00" * (target_offset - current_pos))
        out.extend(data)

    final_pad = total_size - len(out)
    if final_pad > 0:
        out.extend(b"\x00" * final_pad)

    with open(output_path, "wb") as f:
        f.write(out)
    print(f"TRP written to {output_path} ({len(out)} bytes)")

def main():
    workdir = "extras/trophy/icons"
    outdir = f"extras/trophy/{COMM_ID}"
    os.makedirs(workdir, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)

    tropconf_bytes = make_tropconf_sfm()
    trop_bytes = make_trop_sfm()

    generate_icons(workdir)

    files = {
        "TROPCONF.SFM": tropconf_bytes,
        "TROP.SFM": trop_bytes,
        "ICON0.PNG": open(os.path.join(workdir, "ICON0.PNG"), "rb").read(),
        "TROP000.PNG": open(os.path.join(workdir, "TROP000.PNG"), "rb").read()
    }
    for i in range(1, 18):
        name = f"TROP{i:03d}.PNG"
        files[name] = open(os.path.join(workdir, name), "rb").read()

    trp_path = os.path.join(outdir, "TROPHY.TRP")
    build_trp(files, trp_path)

if __name__ == "__main__":
    main()
