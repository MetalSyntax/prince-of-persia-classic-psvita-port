#!/usr/bin/env python3
"""
PS Vita LiveArea Image Converter & Adapter Tool (with Interactive TUI)
======================================================================
Adapts any standard PNG images (Gemini, Midjourney, DALL-E, Photoshop, etc.)
to the strict PS Vita LiveArea specifications:
  - Exact dimensions (bg0: 840x500, pic0: 960x544, icon0: 128x128, startup: 280x158)
  - 8-bit indexed colormap (256 colors palette, non-interlaced)
  - Strict size limit checks (e.g., bg0 <= 128 KB for LiveArea engine compatibility)
  - Smart scaling: crop (center-fill), fit (letterbox), or stretch
  - Interactive TUI menu with Finder Drag & Drop support and LiveArea status inspection

Author: Antigravity Assistant for PS Vita Development
"""

import os
import sys
import shutil
import readline
import glob
import argparse
from pathlib import Path
from PIL import Image, ImageOps

# ANSI Color Codes for TUI
class Colors:
    HEADER = "\033[95m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    UNDERLINE = "\033[4m"
    RESET = "\033[0m"

# Standard PS Vita LiveArea image specifications
VITA_SPECS = {
    "bg0": {
        "filename": "bg0.png",
        "width": 840,
        "height": 500,
        "max_kb": 128,  # SceShell LiveArea limit
        "name": "LiveArea Background",
        "desc": "Fondo principal de la LiveArea (840x500, máx 128 KB)"
    },
    "pic0": {
        "filename": "pic0.png",
        "width": 960,
        "height": 544,
        "max_kb": 1024,
        "name": "Splash Screen / Lockscreen",
        "desc": "Pantalla de inicio / desbloqueo (960x544)"
    },
    "icon0": {
        "filename": "icon0.png",
        "width": 128,
        "height": 128,
        "max_kb": 128,
        "name": "App Icon",
        "desc": "Icono de la burbuja en el menú Home (128x128)"
    },
    "startup": {
        "filename": "startup.png",
        "width": 280,
        "height": 158,
        "max_kb": 128,
        "name": "Gate Startup Banner",
        "desc": "Banner de la compuerta de arranque (280x158)"
    }
}

DEFAULT_DEST_DIR = Path("/Volumes/Seagate/PSVITA Develop/Prince of Persia /extras/livearea")


def clean_input_path(raw_path: str) -> Path:
    """
    Cleans raw path string from terminal (handles drag & drop quotes, escaped spaces, etc.).
    """
    p = raw_path.strip()
    if (p.startswith('"') and p.endswith('"')) or (p.startswith("'") and p.endswith("'")):
        p = p[1:-1]
    # Replace escaped spaces from macOS terminal drag & drop
    p = p.replace("\\ ", " ")
    p = os.path.expanduser(p)
    return Path(p).resolve()


def setup_path_completer():
    """Enables Tab completion for file paths in input()."""
    def complete(text, state):
        text = os.path.expanduser(text)
        pattern = text + "*"
        matches = glob.glob(pattern)
        # Add trailing slash for directories
        matches = [m + "/" if os.path.isdir(m) else m for m in matches]
        try:
            return matches[state]
        except IndexError:
            return None

    readline.set_completer_delims(" \t\n;")
    readline.parse_and_bind("tab: complete")
    readline.set_completer(complete)


def resize_image(img: Image.Image, target_w: int, target_h: int, mode: str = "crop", bg_color=(0, 0, 0, 0)) -> Image.Image:
    """
    Resizes image according to chosen mode: 'crop' (center cover), 'fit' (contain/pad), 'stretch'.
    """
    if mode == "stretch":
        return img.resize((target_w, target_h), Image.Resampling.LANCZOS)

    src_w, src_h = img.size

    if mode == "fit":
        img_ratio = src_w / src_h
        target_ratio = target_w / target_h

        if img_ratio > target_ratio:
            new_w = target_w
            new_h = int(round(target_w / img_ratio))
        else:
            new_h = target_h
            new_w = int(round(target_h * img_ratio))

        resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
        
        canvas_mode = "RGBA" if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info) else "RGB"
        if canvas_mode == "RGB" and len(bg_color) == 4:
            bg_color = bg_color[:3]

        canvas = Image.new(canvas_mode, (target_w, target_h), bg_color)
        paste_x = (target_w - new_w) // 2
        paste_y = (target_h - new_h) // 2

        if resized.mode == "RGBA":
            canvas.paste(resized, (paste_x, paste_y), mask=resized.split()[3])
        else:
            canvas.paste(resized, (paste_x, paste_y))
        return canvas

    # Default: 'crop' (cover target dimensions, center crop)
    return ImageOps.fit(img, (target_w, target_h), method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))


def convert_to_8bit_indexed(img: Image.Image, dither: bool = True) -> Image.Image:
    """
    Converts RGB/RGBA image to 8-bit indexed colormap (256 colors maximum) for PS Vita.
    """
    dither_val = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE

    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        rgba_img = img.convert("RGBA")
        indexed_img = rgba_img.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=dither_val)
    else:
        rgb_img = img.convert("RGB")
        indexed_img = rgb_img.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=dither_val)

    return indexed_img


def process_livearea_asset(
    input_path: Path,
    asset_type: str,
    output_dir: Path,
    mode: str = "crop",
    dither: bool = True,
    backup: bool = True
) -> Path:
    """
    Converts and saves the asset according to PS Vita specifications.
    """
    if asset_type not in VITA_SPECS:
        raise ValueError(f"Tipo de asset inválido '{asset_type}'.")

    spec = VITA_SPECS[asset_type]
    target_filename = spec["filename"]
    target_w = spec["width"]
    target_h = spec["height"]
    max_kb = spec["max_kb"]

    if not input_path.exists():
        raise FileNotFoundError(f"No se encontró el archivo: {input_path}")

    output_dir.mkdir(parents=True, exist_ok=True)
    out_file = output_dir / target_filename

    # Create backup
    if out_file.exists() and backup:
        bak_file = out_file.with_suffix(".png.bak")
        shutil.copy2(out_file, bak_file)
        print(f"  {Colors.DIM}[BACKUP] Respaldo guardado en: {bak_file.name}{Colors.RESET}")

    print(f"\n⚙️  {Colors.BOLD}Procesando:{Colors.RESET} '{Colors.CYAN}{input_path.name}{Colors.RESET}' -> '{Colors.GREEN}{target_filename}{Colors.RESET}' ({spec['name']})")

    # Load source image
    with Image.open(input_path) as src_img:
        print(f"  📐 {Colors.DIM}Entrada:{Colors.RESET} {src_img.size[0]}x{src_img.size[1]} ({src_img.mode})")

        # 1. Resize
        resized = resize_image(src_img, target_w, target_h, mode=mode)
        print(f"  ✂️  {Colors.DIM}Ajuste ({mode}):{Colors.RESET} {target_w}x{target_h}")

        # 2. Convert to 8-bit colormap
        indexed = convert_to_8bit_indexed(resized, dither=dither)

        # 3. Save
        indexed.save(out_file, format="PNG", optimize=True)

    # 4. Check file size
    file_size_kb = out_file.stat().st_size / 1024.0
    status_color = Colors.GREEN if file_size_kb <= max_kb else Colors.YELLOW
    status_icon = "✅" if file_size_kb <= max_kb else "⚠️ "

    print(f"  {status_icon} {Colors.BOLD}Guardado en:{Colors.RESET} {out_file}")
    print(f"  📊 {status_color}Tamaño: {file_size_kb:.2f} KB (Límite: {max_kb} KB, 8-bit colormap){Colors.RESET}")

    if file_size_kb > max_kb:
        print(f"  {Colors.YELLOW}⚠️  ADVERTENCIA: El archivo supera los {max_kb} KB. LiveArea podría no cargarlo.{Colors.RESET}")

    return out_file


def render_tui_header(dest_dir: Path):
    """Draws the TUI banner and current LiveArea status."""
    print("\033[H\033[J", end="")  # Clear screen
    print(f"{Colors.CYAN}{Colors.BOLD}╔══════════════════════════════════════════════════════════════════════════════╗{Colors.RESET}")
    print(f"{Colors.CYAN}{Colors.BOLD}║                   🎮  PS VITA LIVEAREA ASSET ADAPTER TUI  🎮                 ║{Colors.RESET}")
    print(f"{Colors.CYAN}{Colors.BOLD}╚══════════════════════════════════════════════════════════════════════════════╝{Colors.RESET}")
    print(f"{Colors.DIM}📁 Directorio destino:{Colors.RESET} {Colors.BOLD}{dest_dir}{Colors.RESET}\n")

    print(f"{Colors.BOLD}📋 Estado actual de los assets en LiveArea:{Colors.RESET}")
    print(f"  {'Asset':<12} {'Dimensiones':<14} {'Estado':<10} {'Tamaño':<12} {'Descripción'}")
    print(f"  {'-'*12} {'-'*14} {'-'*10} {'-'*12} {'-'*26}")

    for k, spec in VITA_SPECS.items():
        fpath = dest_dir / spec["filename"]
        if fpath.exists():
            kb = fpath.stat().st_size / 1024.0
            try:
                with Image.open(fpath) as im:
                    dim_str = f"{im.size[0]}x{im.size[1]}"
                    mode_str = "8-bit" if im.mode == "P" else im.mode
            except Exception:
                dim_str = f"{spec['width']}x{spec['height']}"
                mode_str = "OK"

            kb_color = Colors.GREEN if kb <= spec["max_kb"] else Colors.YELLOW
            status = f"{Colors.GREEN}PRESENTE{Colors.RESET}"
            size_info = f"{kb_color}{kb:.1f} KB ({mode_str}){Colors.RESET}"
        else:
            dim_str = f"{spec['width']}x{spec['height']}"
            status = f"{Colors.RED}FALTA{Colors.RESET}"
            size_info = f"{Colors.DIM}--{Colors.RESET}"

        print(f"  {Colors.BOLD}{spec['filename']:<12}{Colors.RESET} {dim_str:<14} {status:<19} {size_info:<21} {Colors.DIM}{spec['name']}{Colors.RESET}")
    print("\n" + f"{Colors.CYAN}──────────────────────────────────────────────────────────────────────────────{Colors.RESET}")


def interactive_tui(dest_dir: Path):
    """Main interactive TUI loop."""
    setup_path_completer()

    while True:
        render_tui_header(dest_dir)

        print(f"\n{Colors.BOLD}Selecciona la acción a realizar:{Colors.RESET}")
        print(f"  {Colors.GREEN}[1]{Colors.RESET} Adaptar {Colors.BOLD}bg0.png{Colors.RESET}   (840x500 - Fondo de LiveArea)")
        print(f"  {Colors.GREEN}[2]{Colors.RESET} Adaptar {Colors.BOLD}pic0.png{Colors.RESET}  (960x544 - Pantalla de Inicio / Splash)")
        print(f"  {Colors.GREEN}[3]{Colors.RESET} Adaptar {Colors.BOLD}icon0.png{Colors.RESET} (128x128 - Icono de la burbuja)")
        print(f"  {Colors.GREEN}[4]{Colors.RESET} Adaptar {Colors.BOLD}startup.png{Colors.RESET} (280x158 - Banner de compuerta)")
        print(f"  {Colors.GREEN}[5]{Colors.RESET} Adaptar {Colors.BOLD}Múltiples / Todos los assets{Colors.RESET} en lote")
        print(f"  {Colors.YELLOW}[C]{Colors.RESET} Cambiar directorio destino")
        print(f"  {Colors.RED}[0]{Colors.RESET} Salir\n")

        choice = input(f"{Colors.BOLD}👉 Opción [1-5, C, 0]: {Colors.RESET}").strip().lower()

        if choice in ("0", "q", "exit"):
            print(f"\n{Colors.GREEN}👋 ¡Hasta luego!{Colors.RESET}\n")
            sys.exit(0)

        elif choice == "c":
            new_dest = input(f"\n{Colors.BOLD}Introduce la nueva ruta de destino:{Colors.RESET}\n> ").strip()
            if new_dest:
                cleaned = clean_input_path(new_dest)
                if cleaned.exists() or input(f"¿Crear directorio {cleaned}? (s/n): ").strip().lower() == "s":
                    cleaned.mkdir(parents=True, exist_ok=True)
                    dest_dir = cleaned
            continue

        elif choice in ("1", "2", "3", "4"):
            asset_map = {"1": "bg0", "2": "pic0", "3": "icon0", "4": "startup"}
            asset_type = asset_map[choice]
            spec = VITA_SPECS[asset_type]

            print(f"\n{Colors.BOLD}📌 Seleccionaste: {Colors.CYAN}{spec['filename']}{Colors.RESET} ({spec['desc']})")
            print(f"{Colors.DIM}Consejo: Puedes arrastrar y soltar la imagen desde el Finder aquí directamente.{Colors.RESET}")

            while True:
                raw_in = input(f"\n{Colors.BOLD}🖼️  Ruta de la imagen original (o 'v' para volver):{Colors.RESET}\n> ").strip()
                if raw_in.lower() == "v" or not raw_in:
                    break

                img_path = clean_input_path(raw_in)
                if not img_path.exists() or not img_path.is_file():
                    print(f"{Colors.RED}❌ El archivo no existe o no es válido: '{img_path}'{Colors.RESET}")
                    continue

                try:
                    with Image.open(img_path) as test_img:
                        print(f"  {Colors.GREEN}✓ Imagen cargada:{Colors.RESET} {test_img.size[0]}x{test_img.size[1]} ({test_img.format}, {test_img.mode})")
                except Exception as e:
                    print(f"{Colors.RED}❌ Error al leer la imagen: {e}{Colors.RESET}")
                    continue

                # Choose mode
                print(f"\n{Colors.BOLD}Modo de ajuste:{Colors.RESET}")
                print(f"  {Colors.GREEN}[1]{Colors.RESET} Recorte centrado (Crop - Llena el área sin deformar) {Colors.BOLD}[Recomendado]{Colors.RESET}")
                print(f"  {Colors.GREEN}[2]{Colors.RESET} Contener completo (Fit - Con márgenes si la proporción difiere)")
                print(f"  {Colors.GREEN}[3]{Colors.RESET} Estirar directo (Stretch - Deforma si es necesario)")
                m_choice = input(f"{Colors.BOLD}👉 Modo [1-3] (Default: 1): {Colors.RESET}").strip()
                mode = {"1": "crop", "2": "fit", "3": "stretch"}.get(m_choice, "crop")

                try:
                    process_livearea_asset(
                        input_path=img_path,
                        asset_type=asset_type,
                        output_dir=dest_dir,
                        mode=mode,
                        dither=True,
                        backup=True
                    )
                    input(f"\n{Colors.CYAN}Presiona Enter para continuar...{Colors.RESET}")
                except Exception as e:
                    print(f"{Colors.RED}❌ Error en la conversión: {e}{Colors.RESET}")
                    input(f"\nPresiona Enter...")
                break

        elif choice == "5":
            print(f"\n{Colors.BOLD}📦 Conversión en lote (Batch):{Colors.RESET}")
            for atype in ["bg0", "pic0", "icon0", "startup"]:
                spec = VITA_SPECS[atype]
                raw_in = input(f"\n🖼️  Imagen para {Colors.CYAN}{spec['filename']}{Colors.RESET} ({spec['name']}) [Enter para omitir]:\n> ").strip()
                if not raw_in:
                    continue
                img_path = clean_input_path(raw_in)
                if img_path.exists() and img_path.is_file():
                    try:
                        process_livearea_asset(img_path, atype, dest_dir, mode="crop", dither=True, backup=True)
                    except Exception as e:
                        print(f"{Colors.RED}❌ Error en {spec['filename']}: {e}{Colors.RESET}")
                else:
                    print(f"{Colors.YELLOW}⚠️ Archivo no encontrado, omitiendo.{Colors.RESET}")

            input(f"\n{Colors.CYAN}Lote finalizado. Presiona Enter para continuar...{Colors.RESET}")


def main():
    parser = argparse.ArgumentParser(
        description="Adaptador de imágenes PNG (Gemini / Midjourney / Web) para PS Vita LiveArea (8-bit colormap, medidas oficiales)."
    )

    parser.add_argument(
        "input",
        nargs="?",
        type=Path,
        help="Ruta de la imagen de entrada a convertir."
    )
    parser.add_argument(
        "-i", "--interactive", "--tui",
        action="store_true",
        help="Abrir la interfaz visual interactiva en la terminal (TUI)."
    )
    parser.add_argument(
        "-t", "--type",
        choices=["bg0", "pic0", "icon0", "startup"],
        help="Tipo de imagen LiveArea a generar ('bg0', 'pic0', 'icon0', 'startup')."
    )
    parser.add_argument(
        "--bg0",
        type=Path,
        help="Ruta directa de la imagen para bg0.png (840x500)."
    )
    parser.add_argument(
        "--pic0",
        type=Path,
        help="Ruta directa de la imagen para pic0.png (960x544)."
    )
    parser.add_argument(
        "--icon0",
        type=Path,
        help="Ruta directa de la imagen para icon0.png (128x128)."
    )
    parser.add_argument(
        "--startup",
        type=Path,
        help="Ruta directa de la imagen para startup.png (280x158)."
    )
    parser.add_argument(
        "-d", "--dest",
        type=Path,
        default=DEFAULT_DEST_DIR,
        help=f"Directorio de salida donde guardar las imágenes (Por defecto: {DEFAULT_DEST_DIR})."
    )
    parser.add_argument(
        "-m", "--mode",
        choices=["crop", "fit", "stretch"],
        default="crop",
        help="Modo de ajuste: 'crop' (recorte centrado), 'fit' (contener con bordes), 'stretch' (estirar) [Default: crop]."
    )
    parser.add_argument(
        "--no-dither",
        action="store_true",
        help="Desactiva el tramado Floyd-Steinberg."
    )
    parser.add_argument(
        "--no-backup",
        action="store_true",
        help="No crea copias de seguridad (.bak) de los archivos reemplazados."
    )

    args = parser.parse_args()

    # If launched with --tui, -i, or without any CLI arguments, enter interactive TUI mode!
    no_args_provided = not args.input and not args.bg0 and not args.pic0 and not args.icon0 and not args.startup
    if args.interactive or no_args_provided:
        try:
            interactive_tui(args.dest)
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}\nOperación cancelada por el usuario.{Colors.RESET}")
            sys.exit(0)
        return

    # CLI Batch / single mode
    tasks = []
    if args.bg0:
        tasks.append((args.bg0, "bg0"))
    if args.pic0:
        tasks.append((args.pic0, "pic0"))
    if args.icon0:
        tasks.append((args.icon0, "icon0"))
    if args.startup:
        tasks.append((args.startup, "startup"))

    if args.input:
        if not args.type:
            name_lower = args.input.stem.lower()
            if "bg0" in name_lower or "bg" in name_lower or "background" in name_lower:
                inferred_type = "bg0"
            elif "pic0" in name_lower or "pic" in name_lower or "splash" in name_lower:
                inferred_type = "pic0"
            elif "icon0" in name_lower or "icon" in name_lower:
                inferred_type = "icon0"
            elif "startup" in name_lower or "gate" in name_lower:
                inferred_type = "startup"
            else:
                parser.error("Debes especificar el tipo de imagen con '-t' o '--type' (bg0, pic0, icon0, startup).")
            tasks.append((args.input, inferred_type))
        else:
            tasks.append((args.input, args.type))

    print(f"{Colors.CYAN}=========================================================={Colors.RESET}")
    print(f"{Colors.CYAN} 🎮 PS VITA LIVEAREA IMAGE CONVERTER (8-BIT COLORMAP) 🎮 {Colors.RESET}")
    print(f"{Colors.CYAN}=========================================================={Colors.RESET}")
    print(f"📁 Directorio destino: {args.dest}")

    for input_file, asset_type in tasks:
        try:
            process_livearea_asset(
                input_path=input_file,
                asset_type=asset_type,
                output_dir=args.dest,
                mode=args.mode,
                dither=not args.no_dither,
                backup=not args.no_backup
            )
        except Exception as e:
            print(f"{Colors.RED}❌ Error al procesar '{input_file}': {e}{Colors.RESET}", file=sys.stderr)

    print(f"\n{Colors.GREEN}✨ ¡Proceso completado con éxito!{Colors.RESET}")


if __name__ == "__main__":
    main()
