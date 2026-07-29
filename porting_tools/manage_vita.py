#!/usr/bin/env python3
import os
import re
import sys
import subprocess
import time
from ftplib import FTP, all_errors

VITA_IP = "192.168.3.15"
VITA_PORT = 1337
# Carpeta donde build.sh/build_and_install.sh dejan los .vpk generados. A
# diferencia de un LOCAL_VPK_PATH fijo a un solo archivo, esto se recorre
# dinamicamente (ver list_local_vpks()) para soportar cualquier cantidad de
# variantes de build (dungeon_hunter_2.vpk, dungeon_hunter_2_gpu_quads_test.vpk, etc.) sin
# tener que hardcodear cada nombre -- generico para cualquier proyecto que
# genere sus VPKs bajo esta carpeta, no solo Prince of Persia Classic.
BUILD_DIR = "build"
VITA_DOWNLOADS_DIR = "/ux0:/downloads"
VITA_DATA_DIR = "/ux0:/data"
VITA_LOGS_DIR = "/ux0:/data/popclassic/logs"
VITA_CG_DIR = "ux0:/data/popclassic/cg"
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def getch():
    import sys, tty, termios
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        ch = sys.stdin.read(1)
        if ch == '\x1b':
            ch += sys.stdin.read(2)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

def disconnect_proton_vpn():
    print("[*] Intentando desconectar Proton VPN...")
    try:
        result = subprocess.run(["protonvpn-cli", "disconnect"], capture_output=True, text=True)
        if result.returncode == 0:
            print("[+] Proton VPN desconectado exitosamente.")
            return True
        else:
            if "not connected" in result.stderr.lower() or "not connected" in result.stdout.lower():
                print("[+] Proton VPN ya estaba desconectado.")
                return True
            print(f"[-] Error de Proton VPN: {result.stderr.strip() or result.stdout.strip()}")
    except FileNotFoundError:
        print("[!] 'protonvpn-cli' no está instalado en el PATH.")
    except Exception as e:
        print(f"[-] Ocurrió un error inesperado al intentar desconectar VPN: {e}")
    return False

def get_local_ip_for_route():
    import socket
    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if ip.startswith("192.168.3."):
                return ip
    except Exception:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((VITA_IP, 9))
        ip = s.getsockname()[0]
        s.close()
        if ip.startswith("192.168.3."):
            return ip
    except Exception:
        pass
    return None

def connect_ftp():
    print(f"[*] Conectando a la PS Vita en {VITA_IP}:{VITA_PORT}...")
    local_ip = get_local_ip_for_route()
    source_addr = (local_ip, 0) if local_ip else None
    if local_ip:
        print(f"[*] Forzando ruta local mediante IP origen física: {local_ip} (Bypasseando VPN)")
    try:
        ftp = FTP()
        ftp.connect(VITA_IP, VITA_PORT, timeout=10, source_address=source_addr)
        ftp.login() 
        print("[+] Conexión FTP establecida.")
        return ftp
    except all_errors as e:
        print(f"[-] Error al conectar por FTP a la PS Vita: {e}")
        return None

def create_directory_if_not_exists(ftp, path):
    try:
        ftp.cwd(path)
    except all_errors:
        print(f"[*] El directorio '{path}' no existe. Intentando crearlo...")
        try:
            parts = [p for p in path.split('/') if p]
            current = ""
            for part in parts:
                if ":" in part:
                    current = part
                else:
                    current = f"{current}/{part}"
                try:
                    ftp.mkd(current)
                except all_errors:
                    pass 
            ftp.cwd(path)
            print(f"[+] Directorio '{path}' listo.")
        except all_errors as e:
            print(f"[-] No se pudo crear el directorio '{path}': {e}")

def list_local_vpks():
    """Todos los .vpk en BUILD_DIR/, mas reciente primero -- detecta
    automaticamente cualquier variante de build (build.sh genera un nombre
    de VPK distinto por flag, ver build.sh --help en la cabecera del
    archivo) sin que este script tenga que conocer sus nombres de antemano."""
    project_root = os.path.dirname(BASE_DIR)
    build_dir = os.path.join(project_root, BUILD_DIR)
    if not os.path.isdir(build_dir):
        return []
    vpks = [
        os.path.join(build_dir, f) for f in os.listdir(build_dir)
        if f.endswith(".vpk") and not f.startswith("._")
    ]
    vpks.sort(key=os.path.getmtime, reverse=True)
    return vpks


def choose_vpk():
    """Si hay un solo VPK lo usa directo; si hay varios, muestra un menu
    (tamano + fecha de modificacion para distinguir variantes de un vistazo)
    y deja elegir -- Enter usa el mas reciente."""
    vpks = list_local_vpks()
    if not vpks:
        print(f"[-] No se encontró ningún .vpk en '{BUILD_DIR}/'. Compilá el proyecto primero "
              f"(build.sh o build_and_install.sh, opción 5 de este menú).")
        return None

    if len(vpks) == 1:
        print(f"[*] Un solo VPK encontrado: {os.path.basename(vpks[0])}")
        return vpks[0]

    print(f"[*] Se encontraron {len(vpks)} VPKs en '{BUILD_DIR}/' (más reciente primero):")
    for i, path in enumerate(vpks, 1):
        size_mb = os.path.getsize(path) / (1024 * 1024)
        mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(path)))
        print(f"  {i}. {os.path.basename(path):<40} {size_mb:6.2f} MB   {mtime}")
    print()

    choice = input(f"Elegí el VPK a subir [1-{len(vpks)}] (Enter = el más reciente): ").strip()
    if not choice:
        return vpks[0]
    try:
        idx = int(choice)
        if 1 <= idx <= len(vpks):
            return vpks[idx - 1]
    except ValueError:
        pass
    print("[-] Opción inválida.")
    return None


def upload_vpk():
    local_vpk_path = choose_vpk()
    if not local_vpk_path:
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    try:
        create_directory_if_not_exists(ftp, VITA_DOWNLOADS_DIR)
        filename = os.path.basename(local_vpk_path)
        dest_file_path = f"{VITA_DOWNLOADS_DIR}/{filename}"

        print(f"[*] Subiendo {local_vpk_path} a {dest_file_path}...")

        with open(local_vpk_path, "rb") as f:
            ftp.storbinary(f"STOR {dest_file_path}", f)

        print(f"[+] Transferencia exitosa! Instala el VPK en tu Vita desde '{VITA_DOWNLOADS_DIR.replace('/ux0:', 'ux0:')}/{filename}'")
    except all_errors as e:
        print(f"[-] Falló la transferencia del VPK: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def upload_eboot():
    project_root = os.path.dirname(BASE_DIR)
    local_eboot_path = os.path.join(project_root, BUILD_DIR, "eboot.bin")

    if not os.path.exists(local_eboot_path):
        print(f"[-] No se encontró '{local_eboot_path}'. Asegúrate de haber compilado el proyecto.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    dest_dir = "/ux0:/app/POPC00001"
    dest_file_path = f"{dest_dir}/eboot.bin"

    try:
        create_directory_if_not_exists(ftp, dest_dir)
        print(f"[*] Subiendo {local_eboot_path} a {dest_file_path}...")
        
        with open(local_eboot_path, "rb") as f:
            ftp.storbinary(f"STOR {dest_file_path}", f)
            
        print("[+] ¡eboot.bin subido exitosamente! Ya puedes iniciar el juego sin tener que reinstalar el VPK entero.")
    except all_errors as e:
        print(f"[-] Falló la transferencia del eboot.bin: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def download_latest_debug_files():
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    descargar_dmp = input("¿Deseas descargar el último crash dump (DMP)? (s/n): ").strip().lower()
    
    if descargar_dmp in ['s', 'y', 'si', 'yes']:
        print("[*] Buscando el último crash dump (.dmp / psp2core) en ux0:/data...")
        latest_dmp = None
        latest_dmp_time = 0
        try:
            ftp.cwd(VITA_DATA_DIR)
            try:
                files = list(ftp.mlsd())
                for name, facts in files:
                    if (name.startswith("psp2core") or name.endswith(".dmp")) and not name.endswith(".tmp"):
                        mtime = facts.get("modify", "0")
                        if mtime > str(latest_dmp_time):
                            latest_dmp_time = int(mtime) if mtime.isdigit() else mtime
                            latest_dmp = name
            except all_errors:
                file_list = []
                ftp.retrlines("LIST", file_list.append)
                valid_files = []
                for line in file_list:
                    parts = line.split()
                    if len(parts) >= 9:
                        name = " ".join(parts[8:])
                        if (name.startswith("psp2core") or name.endswith(".dmp")) and not name.endswith(".tmp"):
                            valid_files.append(name)
                if valid_files:
                    valid_files.sort()
                    latest_dmp = valid_files[-1]
            
            if latest_dmp:
                project_root = os.path.dirname(BASE_DIR)
                logs_folder = os.path.join(project_root, "logs")
                if not os.path.exists(logs_folder):
                    os.makedirs(logs_folder)
                local_dmp_name = os.path.join(logs_folder, f"PopClassic-{latest_dmp}")
                print(f"[+] Último dump detectado: '{latest_dmp}'. Descargando como '{local_dmp_name}'...")
                with open(local_dmp_name, "wb") as f:
                    ftp.retrbinary(f"RETR {latest_dmp}", f.write)
                print(f"[+] Descargado '{latest_dmp}' en la carpeta logs/.")
                
                parsear_dmp = input(f"¿Deseas parsear '{latest_dmp}' automáticamente ahora? (s/n): ").strip().lower()
                if parsear_dmp in ['s', 'y', 'si', 'yes']:
                    output_file = local_dmp_name + ".parsed.txt"
                    print(f"[*] Parseando el dump y guardando en '{output_file}'...")
                    analyze_crash_dump(local_dmp_name, output_file)
            else:
                print("[-] No se encontraron archivos de crash dump en ux0:/data.")
        except all_errors as e:
            print(f"[-] Error al buscar o descargar dump: {e}")
    else:
        print("[*] Omitiendo la descarga del crash dump.")

    print("[*] Buscando el último log de Prince of Persia Classic en ux0:/data/popclassic/logs...")
    latest_log = None
    latest_log_time = 0
    try:
        ftp.cwd(VITA_LOGS_DIR)
        try:
            files = list(ftp.mlsd())
            for name, facts in files:
                if name.endswith(".txt") or "log" in name.lower():
                    mtime = facts.get("modify", "0")
                    if mtime > str(latest_log_time):
                        latest_log_time = int(mtime) if mtime.isdigit() else mtime
                        latest_log = name
        except all_errors:
            file_list = []
            ftp.retrlines("LIST", file_list.append)
            valid_logs = []
            for line in file_list:
                parts = line.split()
                if len(parts) >= 9:
                    name = " ".join(parts[8:])
                    if name.endswith(".txt") or "log" in name.lower():
                        valid_logs.append(name)
            if valid_logs:
                valid_logs.sort()
                latest_log = valid_logs[-1]
                        
        if latest_log:
            project_root = os.path.dirname(BASE_DIR)
            logs_folder = os.path.join(project_root, "logs")
            if not os.path.exists(logs_folder):
                os.makedirs(logs_folder)
            local_log_name = os.path.join(logs_folder, f"{latest_log}")
            print(f"[+] Último log detectado: '{latest_log}'. Descargando como '{local_log_name}'...")
            with open(local_log_name, "wb") as f:
                ftp.retrbinary(f"RETR {latest_log}", f.write)
            print(f"[+] Descargado '{latest_log}' en la carpeta logs/.")
        else:
            print("[-] No se encontraron archivos de registro (.txt) en ux0:/data/popclassic/logs.")
    except all_errors as e:
        print(f"[-] Error al buscar o descargar logs: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass

def download_glsl_shaders():
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    vita_glsl_dir = "ux0:/data/popclassic/glsl"
    project_root = os.path.dirname(BASE_DIR)
    local_glsl_dir = os.path.join(project_root, "glsl_dump")
    if not os.path.exists(local_glsl_dir):
        os.makedirs(local_glsl_dir)

    print(f"[*] Buscando shaders en {vita_glsl_dir}...")
    try:
        ftp.cwd(vita_glsl_dir)
        files = []
        try:
            files_info = list(ftp.mlsd())
            files = [name for name, facts in files_info if name.endswith(".glsl")]
        except all_errors:
            file_list = []
            ftp.retrlines("LIST", file_list.append)
            for line in file_list:
                parts = line.split()
                if len(parts) >= 9:
                    name = " ".join(parts[8:])
                    if name.endswith(".glsl"):
                        files.append(name)
        
        if not files:
            print("[-] No se encontraron shaders en la carpeta.")
        else:
            print(f"[+] Encontrados {len(files)} shaders. Descargando...")
            for f in files:
                local_path = os.path.join(local_glsl_dir, f)
                with open(local_path, "wb") as local_f:
                    ftp.retrbinary(f"RETR {f}", local_f.write)
                print(f"  -> Descargado: {f}")
            print(f"[+] Todos los shaders descargados en {local_glsl_dir}")
    except all_errors as e:
        print(f"[-] Error al listar o descargar shaders: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass


def upload_cg_shaders():
    project_root = os.path.dirname(BASE_DIR)
    local_cg_dir = os.path.join(project_root, "assets", "cg")

    if not os.path.exists(local_cg_dir):
        print(f"[-] No se encontró la carpeta local '{local_cg_dir}'.")
        return

    cg_files = sorted(
        f for f in os.listdir(local_cg_dir)
        if f.endswith(".cg") and not f.startswith("._")
    )
    if not cg_files:
        print(f"[-] No hay archivos .cg en '{local_cg_dir}'.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    try:
        create_directory_if_not_exists(ftp, VITA_CG_DIR)
        print(f"[*] Subiendo {len(cg_files)} shader(s) .cg a {VITA_CG_DIR}...")
        for fname in cg_files:
            local_path = os.path.join(local_cg_dir, fname)
            dest_path = f"{VITA_CG_DIR}/{fname}"
            with open(local_path, "rb") as f:
                ftp.storbinary(f"STOR {dest_path}", f)
            print(f"  -> Subido: {fname}")
        print(f"[+] Todos los shaders .cg fueron subidos a {VITA_CG_DIR}.")
    except all_errors as e:
        print(f"[-] Falló la subida de shaders .cg: {e}")
    finally:
        try:
            ftp.quit()
        except:
            pass


def sync_shaders():
    """Descarga los .glsl volcados (no traducidos) y sube los .cg ya
    traducidos en un solo paso, reportando qué shaders siguen sin traducir."""
    print("[*] Paso 1/2: descargando shaders GLSL sin traducir desde la Vita...")
    download_glsl_shaders()

    project_root = os.path.dirname(BASE_DIR)
    local_glsl_dir = os.path.join(project_root, "glsl_dump")
    local_cg_dir = os.path.join(project_root, "assets", "cg")

    dumped = set()
    if os.path.exists(local_glsl_dir):
        dumped = {
            os.path.splitext(f)[0] for f in os.listdir(local_glsl_dir)
            if f.endswith(".glsl") and not f.startswith("._")
        }
    translated = set()
    if os.path.exists(local_cg_dir):
        translated = {
            os.path.splitext(f)[0] for f in os.listdir(local_cg_dir)
            if f.endswith(".cg") and not f.startswith("._")
        }

    missing = sorted(dumped - translated)
    if missing:
        print(f"[!] {len(missing)} shader(s) todavía SIN traducir a .cg "
              f"(pídele a Claude que los traduzca en assets/cg/ antes de continuar):")
        for h in missing:
            print(f"  - {h}.glsl")
    else:
        print("[+] Todos los shaders volcados ya tienen su .cg correspondiente.")

    print("[*] Paso 2/2: subiendo todos los .cg traducidos a la Vita...")
    upload_cg_shaders()


def check_libshacccg():
    """Chequea por FTP si libshacccg.suprx existe y su tamaño en las 2 rutas
    donde el juego/vitaGL lo buscan. Un tamaño de 0 o muy chico (esperado:
    ~1-2 MB) o directamente "no existe" en ambas indica una instalación
    corrupta/incompleta, que es exactamente el tipo de fallo genérico
    ("fatal internal error" en cualquier shader, incluso uno trivial) que
    estamos viendo en el log."""
    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    candidates = [
        "/ur0:/data/libshacccg.suprx",
        "/ur0:/data/external/libshacccg.suprx",
    ]

    try:
        ftp.voidcmd("TYPE I")  # binary mode -- SIZE is unreliable/rejected in ASCII mode
        for path in candidates:
            try:
                size = ftp.size(path)
                print(f"[+] {path}: existe, {size} bytes"
                      + ("  <-- sospechosamente chico/vacio!" if not size or size < 100000 else ""))
            except all_errors as e:
                print(f"[-] {path}: no encontrado ({e})")
    finally:
        try:
            ftp.quit()
        except:
            pass


VITA_GAME_DATA_DIR = "/ux0:/data/popclassic/Data"
LOCAL_DATA_REFERENCE_DIR = "bin/popclassic"


def _local_file_count(path):
    total = 0
    for root, dirs, files in os.walk(path):
        total += sum(1 for f in files if not f.startswith("._") and f != ".DS_Store")
    return total


def _local_shallow_count(path):
    return sum(
        1 for name in os.listdir(path)
        if not name.startswith("._") and name != ".DS_Store"
    )


_DEVICE_MOUNT_RE = re.compile(r"^[A-Za-z0-9]+0:$")


def _ftp_list_entries(ftp, path):
    """Devuelve lista de (nombre, es_directorio) para 'path'. cwd() primero
    (falla con un error claro tipo 550 si no existe) y despues un LIST sin
    argumentos -- pasarle el path completo a LIST directamente confunde al
    ftpd de VitaShell (contesta "200 Okay" en vez de una lista real)."""
    ftp.cwd(path)  # raises all_errors if it doesn't exist -- let it propagate

    entries = []
    try:
        for name, facts in ftp.mlsd():
            if name in (".", ".."):
                continue
            entries.append((name, facts.get("type") == "dir"))
        return entries
    except all_errors:
        pass

    lines = []
    ftp.retrlines("LIST", lines.append)
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) < 9:
            continue
        name = parts[8]
        if name in (".", ".."):
            continue
        entries.append((name, line.startswith("d")))
    return entries


def _ftp_shallow_count(ftp, path):
    """Cuenta cuántas entradas (archivos + carpetas) hay directamente
    adentro de 'path', SIN recursar a subcarpetas. Un conteo recursivo
    completo abre una conexión de datos por cada subcarpeta -- en algo como
    3d/ (miles de subcarpetas) eso agota las conexiones del ftpd de
    VitaShell y termina en "Connection refused" a mitad de camino. Un
    conteo superficial alcanza para detectar "esta carpeta esta vacia/no
    existe", que es lo que realmente estamos chequeando."""
    try:
        entries = _ftp_list_entries(ftp, path)
    except all_errors as e:
        print(f"  [-] No se pudo listar '{path}': {e}")
        return None  # None = no se pudo determinar (distinto de 0 = vacia)

    # VitaShell's ftpd falls back to listing the device root (ux0:, ur0:,
    # os0:, etc.) instead of erroring when asked for something that isn't
    # really a browsable directory -- treat that as "doesn't exist".
    if any(_DEVICE_MOUNT_RE.match(name) for name, _ in entries):
        print(f"  [-] '{path}' devolvió una lista de dispositivos (os0:/ur0:/...) "
              f"en vez de contenido real -- probablemente no existe.")
        return None

    return sum(
        1 for name, _ in entries
        if not name.startswith("._") and name != ".DS_Store"
    )


def verify_data_assets():
    """Compara, carpeta por carpeta, cuántas entradas de primer nivel hay en
    el volcado local de referencia (com.gameloft.android.GAND.GloftD2SS/files/data)
    contra lo que realmente está subido en ux0:data/popclassic/Data/ en
    la Vita. Es un chequeo SUPERFICIAL (no recursivo) a propósito: contar
    recursivamente todo adentro de carpetas con miles de subcarpetas (3d/)
    abre demasiadas conexiones de datos seguidas y el ftpd de VitaShell
    termina rechazando la conexión a mitad de camino. Alcanza para detectar
    "esta carpeta esta vacia o no existe", que es lo que importa acá.
    Reconecta por cada subcarpeta para no acumular ninguna conexión de datos
    a medio cerrar entre una y la siguiente."""
    project_root = os.path.dirname(BASE_DIR)
    local_data_dir = os.path.join(project_root, LOCAL_DATA_REFERENCE_DIR)

    if not os.path.exists(local_data_dir):
        print(f"[-] No se encontró la referencia local en '{local_data_dir}'.")
        return

    subfolders = sorted(
        d for d in os.listdir(local_data_dir)
        if os.path.isdir(os.path.join(local_data_dir, d))
    )
    if not subfolders:
        print(f"[-] '{local_data_dir}' no tiene subcarpetas.")
        return

    disconnect_proton_vpn()
    ftp = connect_ftp()
    if not ftp:
        return

    print(f"[*] Comparando {len(subfolders)} subcarpeta(s) de data/ "
          f"(local vs. {VITA_GAME_DATA_DIR} en la Vita, chequeo superficial)...\n")
    any_mismatch = False
    
    try:
        for sub in subfolders:
            local_count = _local_shallow_count(os.path.join(local_data_dir, sub))
            remote_count = _ftp_shallow_count(ftp, f"{VITA_GAME_DATA_DIR}/{sub}")

            if remote_count is None:
                status = "?"
                any_mismatch = True
            elif local_count == remote_count:
                status = "OK"
            else:
                status = "MISMATCH"
                any_mismatch = True
            print(f"  [{status}] {sub}/: local={local_count}  vita={remote_count}")
    finally:
        try:
            ftp.quit()
        except:
            pass

    print()
    if any_mismatch:
        print("[!] Alguna(s) carpeta(s) no coinciden -- probablemente "
              "quedaron a mitad de copiar. Volvé a subirlas por FTP.")
    else:
        print("[+] Todas las carpetas coinciden en cantidad de archivos.")



def analyze_crash_dump(dump_path=None, output_file=None):
    parse_script = os.path.join(BASE_DIR, "parse_dump.py")
    if not os.path.exists(parse_script):
        print(f"[-] Error: No se encontró el script de análisis en '{parse_script}'.")
        return
    cmd = [sys.executable, parse_script]
    if dump_path:
        cmd.append(dump_path)
    try:
        if output_file:
            with open(output_file, 'w') as f:
                subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, check=True)
            print(f"[+] Análisis completado y guardado en: {output_file}")
        else:
            subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"[-] Error al ejecutar el análisis del dump: {e}")
    except Exception as e:
        print(f"[-] Error inesperado: {e}")

def run_script(folder, script_name, is_python=False):
    script_path = os.path.join(BASE_DIR, folder, script_name)
    if os.path.exists(script_path):
        print(f"[*] Ejecutando {script_name} desde {folder}...")
        try:
            cmd = [sys.executable if is_python else "bash", script_path]
            subprocess.run(cmd, check=True)
            print(f"[+] {script_name} ejecutado exitosamente.")
        except subprocess.CalledProcessError as e:
            print(f"[-] Error al ejecutar {script_name}: {e}")
        except Exception as e:
            print(f"[-] Ocurrió un error inesperado: {e}")
    else:
        print(f"[-] Error: No se encontró el script en '{script_path}'.")

def main():
    options = [
        ("Subir VPK compilado a la PS Vita (ux0:downloads/)", upload_vpk),
        ("Subir SOLO el eboot.bin a la PS Vita (ux0:app/POPC00001/)", upload_eboot),
        ("Descargar el último dump (.dmp) y log (.txt) de Prince of Persia Classic", download_latest_debug_files),
        ("Desconectar Proton VPN ahora mismo", disconnect_proton_vpn),
        ("Ejecutar clean_macos.sh (build/)", lambda: run_script("build", "clean_macos.sh")),
        ("Ejecutar build_and_install.sh (build/)", lambda: run_script("build", "build_and_install.sh")),
        ("Ejecutar deploy_and_launch_vita3k.sh (build/)", lambda: run_script("build", "deploy_and_launch_vita3k.sh")),
        ("Ejecutar decompile_all.sh (build/)", lambda: run_script("build", "decompile_all.sh")),
        ("Ejecutar run_tests.sh (tests/)", lambda: run_script("tests", "run_tests.sh")),
        ("Ejecutar get_dump.sh (misc/)", lambda: run_script("misc", "get_dump.sh")),
        ("Descargar Shaders GLSL dumpeados", download_glsl_shaders),
        ("Subir Shaders CG traducidos (assets/cg/ -> Vita)", upload_cg_shaders),
        ("Sincronizar Shaders (descargar GLSL + subir CG)", sync_shaders),
        ("Chequear libshacccg.suprx (tamano/existencia por FTP)", check_libshacccg),
        ("Verificar data/ completa (conteo de archivos local vs Vita)", verify_data_assets),
        ("Analizar crash dump (.psp2dmp) con vita-parse-core", analyze_crash_dump),
        ("Salir", None)
    ]
    
    current_idx = 0
    while True:
        # Limpiar la pantalla
        print("\033[H\033[J", end="")
        print("\033[96m====================================================\033[0m")
        print("\033[92m         PS VITA DEPLOYMENT & DEBUG TOOL            \033[0m")
        print("\033[93m               (Prince of Persia Classic)          \033[0m")
        print("\033[96m====================================================\033[0m")
        print("\033[90mUsa las flechas \033[97m↑/↓\033[90m para moverte y \033[97mENTER\033[90m para elegir.\033[0m\n")
        
        for i, (text, func) in enumerate(options):
            prefix = f"{i+1:2d}. "
            if i == current_idx:
                print(f"\033[44;97m> {prefix}{text}\033[0m")
            else:
                print(f"  {prefix}{text}")
                
        print("\033[96m====================================================\033[0m")
        
        c = getch()
        if c == '\x1b[A': # up arrow
            current_idx = (current_idx - 1) % len(options)
        elif c == '\x1b[B': # down arrow
            current_idx = (current_idx + 1) % len(options)
        elif c in ('\r', '\n'): # enter
            print("\033[H\033[J", end="")
            print(f"\033[92m[*] Ejecutando: {options[current_idx][0]}\033[0m\n")
            if options[current_idx][1] is None:
                print("\033[93m¡Hasta luego!\033[0m")
                break
            
            try:
                options[current_idx][1]()
            except Exception as e:
                print(f"\033[91m[-] Error inesperado en la ejecución: {e}\033[0m")
                
            print("\n\033[90m[ \033[97mPresiona ENTER para volver al menú principal...\033[90m ]\033[0m")
            input()
        elif c == '\x03': # Ctrl+C
            print("\n\033[93m¡Hasta luego!\033[0m")
            break
        elif c.isdigit():
            # Si presiona un número del 1 al 9, saltar directamente a esa opción
            val = int(c)
            if 1 <= val <= min(9, len(options)):
                current_idx = val - 1

if __name__ == "__main__":
    main()
