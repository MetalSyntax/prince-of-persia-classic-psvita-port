# Instalar Prince of Persia Classic en una PS Vita real (vía FTP)

Esta guía asume que ya compilaste `popclassic_audio.vpk` (ver `plan_portabilidad.md` §8, o corré
`./build_and_install.sh` para compilarlo — genera `build/popclassic_audio.vpk`; la build vieja sin
sonido era `build/popclassic.vpk` y sigue sirviendo de rollback). Cubre todo lo necesario para
pasarlo a una Vita real por FTP con VitaShell, sin necesitar una tarjeta SD/lector aparte.

## 0. Requisitos en la consola

1. **Vita con CFW instalado** (HENkaku/h-encore² o Ensō — cualquiera que deje correr homebrew firmado
   por taiHEN). Si no la tenés jailbreakeada todavía, seguí la guía oficial de tu versión de firmware antes
   de continuar; está fuera del alcance de este documento.
2. **VitaShell** instalado (el explorador de archivos homebrew estándar — normalmente ya viene con el CFW,
   o se instala como cualquier `.vpk`). Es el que va a exponer el servidor FTP.
3. **kubridge** instalado (`ur0:tai/kubridge.skprx`, cargado en `config.txt` de taiHEN). Necesario en
   hardware real (a diferencia de Vita3K, que no lo requiere — ver plan §9.2). La mayoría de los CFW
   modernos ya lo traen; si no, se instala junto con **NoNpDrm**/**FdFix** desde el mismo repo de VitaShell.
4. **`ur0:data/libshacccg.suprx`** presente (el compilador de shaders de Sony — sin esto, el juego muestra
   un diálogo de error explícito y no arranca). Buscar "ShaRKBR33D" para las formas legítimas de obtenerlo
   desde tu propia consola (no se puede redistribuir, por eso está en `.gitignore` como `*.suprx`).

## 1. Activar el servidor FTP de VitaShell

1. En la Vita, abrí **VitaShell**.
2. Presioná **SELECT**. Esto muestra la IP local de la consola y el puerto del servidor FTP (por defecto
   `1337`), algo como:
   ```
   IP: 192.168.0.XXX  Port: 1337
   ```
3. Anotá esa IP — la vas a necesitar en la Mac. Dejá VitaShell abierto con el FTP activo durante todo el
   proceso.
4. Confirmá que la consola y la Mac están en la **misma red Wi-Fi**.

## 2. Transferir los archivos desde la Mac

Con `curl` (ya viene instalado en macOS, no hace falta nada más). Reemplazá `192.168.0.XXX` por la IP real
que te mostró VitaShell en el paso anterior.

```bash
export VITA_IP="192.168.0.XXX"   # <-- poné la IP real de tu consola
cd "/Volumes/Seagate/PSVITA Develop/Prince of Persia "
```

### 2.1. Instalar el juego (el `.vpk`)

VitaShell puede instalar un `.vpk` directamente si lo subís a `ux0:` y lo "abrís" desde ahí, pero es más
prolijo copiarlo a una carpeta temporal y confirmar la instalación por FTP con el protocolo de comandos de
VitaShell (puerto `1338`, el mismo que ya usan los targets `send`/`send_kvdb` de `CMakeLists.txt`):

```bash
# Sube el vpk a ux0:/
curl -T build/popclassic_audio.vpk "ftp://${VITA_IP}:1337/ux0:/popclassic_audio.vpk"
```

Luego, **en la consola**: en VitaShell navegá a `ux0:/`, seleccioná `popclassic_audio.vpk` con el botón **X** y
elegí **Install** en el menú contextual (botón **△**). Esto instala el juego con el Title ID `POPC00001`
(definido en `CMakeLists.txt` como `VITA_TITLEID`). Podés borrar `ux0:/popclassic_audio.vpk` después de instalar.

### 2.2. Subir los assets (`ux0:data/popclassic/`)

El juego espera encontrar, en la memoria de la consola, exactamente el árbol armado en `ux0_data/popclassic/`
de este repo (ver plan §2.3/§2.4 y §9.9 para el detalle de cada archivo):

```
ux0:data/popclassic/
├── libcocosdenshion.so
├── libcocos2d.so
├── libgame_logic.so
├── original.apk                                  <- APK mínimo que contiene únicamente assets/appConfig.txt
├── main.1.org.ubisoft.premium.POPClassic.obb     <- OBB casi completo: contiene la estructura de carpetas
│                                                     con el contenido de Data_960_576, los .loc de Data
│                                                     y el otro Data que tiene el OBB interno.
├── save/                                          <- carpeta vacía, el juego escribe sus saves ahí
├── Data/
│   ├── Audio/       <- .mp3 sueltos, leídos directo por source/audio.cpp (sceIo, sin pasar por CCFileUtils)
│   ├── font/         <- .ttf sueltos, leídos directo por get_font() en source/java.c
│   └── Video/High/   <- .mp4 sueltos, leídos directo por source/video.cpp
└── Data_960_576/     <- Animations, Effects, Localization, Logo, Maps, Particles, Texture, appConfig.txt
                          (~97 MB). HERMANA de Data/, NO va adentro -- este es el prefijo de resolución que
                          CCFileUtils::getFileData busca en runtime, con loose-file-first de fábrica (sin
                          hook nuestro) para texturas/mapas/animaciones.
```

> [!IMPORTANT]
> **`main.1.org.ubisoft.premium.POPClassic.obb` NO se puede eliminar por completo**, ni usar un `.obb` totalmente vacío. Se debe utilizar un archivo `.obb` casi completo que contenga la estructura base de directorios, incluyendo `Data_960_576` y `Data/Localization`.
> El motor arma la ruta al `.obb` directamente (`apkFilePath` + el nombre del `.obb`) y lo abre **sin pasar por el chequeo de "archivo suelto primero"** para ciertos archivos cruciales como `Localization/*.loc`.
>
> **Convención local de nombres:** La consola SIEMPRE necesita el archivo con el nombre exacto
> `main.1.org.ubisoft.premium.POPClassic.obb` -- el motor lo busca por ese nombre literal
> (`nativeSetPaths`/la ruta hardcodeada de Localization).

> [!IMPORTANT]
> `ux0_data/` está en `.gitignore` (son los assets extraídos del APK/OBB originales, con copyright de
> Ubisoft — nunca se suben a git, ver `.gitignore`). Si cloneaste este repo en otra máquina, primero hay que
> reconstruir esa carpeta siguiendo la Fase 2 del plan (`plan_portabilidad.md` §2) antes de este paso.

Crear la carpeta remota y subir todo de una vez con `curl` (recursivo por `find`, ya que `curl` no sube
directorios completos de un solo golpe). **Importante:** `ux0_data/popclassic/` en disco todavía tiene la
carpeta `Data/` completa (con `Animations`/`Effects`/`Localization`/`Logo`/`Maps`/`Particles`/`Texture`
duplicados dentro de `Data_960_576/`, y también respaldos `.full`/`.bk.zip` del `.obb` viejo de 65 MB) —
subir todo el árbol tal cual subiría mucho más de lo necesario. La lista de abajo sube el `.obb` **mínimo**
(~511 KB, ya reconstruido — ver nota más arriba), excluye los respaldos `.full`/`.bk.zip`/`.tmp` y las
subcarpetas de `Data/` ya cubiertas por `Data_960_576/`, y sube el resto (`Data/Audio`, `Data/font`,
`Data/Video`, `Data/save`, `Data_960_576/` completa, los `.so`, `original.apk`, `appConfig.txt`, `assets/`):

```bash
# Crea la carpeta base (curl -Q manda comandos FTP crudos antes de la transferencia)
curl -s "ftp://${VITA_IP}:1337/ux0:/data/" -Q "MKD popclassic" -Q "MKD popclassic/save" -Q "MKD popclassic/assets" > /dev/null

cd ux0_data/popclassic

# PASO 1: crear TODAS las subcarpetas necesarias antes de subir ningún archivo.
# Importante: MKD de FTP no crea niveles intermedios que no existan todavía
# (a diferencia de "mkdir -p") -- un solo "MKD Data_960_576/Texture/Menu/buttons"
# falla en silencio si "Data_960_576", "Data_960_576/Texture" y
# "Data_960_576/Texture/Menu" no existen aún. `sort` ordena las rutas más
# cortas (padres) antes que las más largas (hijas) porque cualquier prefijo
# de un string ordena antes que ese mismo string extendido -- así que crear
# cada carpeta EN ESE ORDEN, una por una, sí funciona.
find . -type d ! -path "." ! -path "./save" ! -path "./assets" \
    ! -path "./Data/Animations" ! -path "./Data/Animations/*" \
    ! -path "./Data/Effects" ! -path "./Data/Effects/*" \
    ! -path "./Data/Localization" ! -path "./Data/Localization/*" \
    ! -path "./Data/Logo" ! -path "./Data/Logo/*" \
    ! -path "./Data/Maps" ! -path "./Data/Maps/*" \
    ! -path "./Data/Particles" ! -path "./Data/Particles/*" \
    ! -path "./Data/Texture" ! -path "./Data/Texture/*" \
    | sed 's#^\./##' | sort | while read -r d; do
    curl -s "ftp://${VITA_IP}:1337/ux0:/data/popclassic/${d}/" -Q "MKD ${d}" > /dev/null 2>&1 || true
done

# PASO 2: subir los archivos (todas las carpetas ya existen del paso 1).
# Salvo error, `curl -T` no muestra nada -- si algo falla, avisa por stderr,
# no queda en silencio como el MKD de arriba.
find . -type f \
    ! -name "*.full" ! -name "*.bk.zip" ! -name "*.tmp" ! -name "original.zip" \
    ! -path "./Data/Animations/*" ! -path "./Data/Effects/*" ! -path "./Data/Localization/*" \
    ! -path "./Data/Logo/*" ! -path "./Data/Maps/*" ! -path "./Data/Particles/*" ! -path "./Data/Texture/*" \
    ! -path "./Data/appConfig.txt" \
    | while read -r f; do
    echo "Subiendo: $f"
    curl -T "$f" "ftp://${VITA_IP}:1337/ux0:/data/popclassic/${f#./}"
done
cd ../..
```

**Cómo verificar que funcionó (antes de volver a probar el juego):** abrí VitaShell en la consola, navegá a
`ux0:data/popclassic/` y confirmá a simple vista que `Data_960_576/` tiene contenido real (que al entrar
veas `Animations/`, `Effects/`, `Localization/`, `Logo/`, `Maps/`, `Particles/`, `Texture/`,
`appConfig.txt` — no una carpeta vacía). Si en el paso 2 de arriba `curl -T` imprime algún error
("(21) Failed FTP upload" o similar), significa que el paso 1 no creó bien esa carpeta puntual.

Esto puede tardar varios minutos (~116 MB en total). Si preferís una GUI en vez de la terminal, cualquier
cliente FTP normal (**Cyberduck**, **FileZilla**, **Transmit**) conectando a `ftp://<IP>:1337` sin usuario/
contraseña funciona igual — simplemente arrastrá la carpeta `ux0_data/popclassic/` completa a `ux0:/data/`
en la consola.

## 3. Primer arranque

1. En la Vita, volvé al menú **LiveArea** y buscá el ícono de "Prince of Persia Classic" (Title ID
   `POPC00001`).
2. Abrilo. Si `kubridge` y `libshacccg.suprx` están bien instalados, debería llegar al mismo punto que ya
   se probó en Vita3K (pantalla de logo → menú, con texto legible gracias al fix de §9.16 del plan).
3. Si se cae inmediatamente con un diálogo de error explícito, ese diálogo casi siempre dice **qué falta**
   (p. ej. "libshacccg.suprx is not installed") — revisar el paso 0 correspondiente.
4. Si se cierra sin ningún diálogo (crash silencioso), usar el target `dump` de `CMakeLists.txt`
   (`make dump`, requiere la IP configurada en `PSVITAIP` del propio `CMakeLists.txt`) para capturar un
   core dump vía FTP y analizarlo — ver `extras/scripts/get_dump.sh`.

## 4. Iterar sin reinstalar el `.vpk` cada vez

Mientras estás ajustando solo el ejecutable (no los assets), alcanza con reemplazar `eboot.bin` directamente
por FTP en vez de reinstalar el `.vpk` completo cada vez:

```bash
curl -T build/eboot.bin "ftp://${VITA_IP}:1337/ux0:/app/POPC00001/eboot.bin"
```

(Esto es exactamente lo que hacen los targets `send`/`send_kvdb` ya definidos en `CMakeLists.txt`, que además
reinician la app automáticamente vía el puerto de comandos `1338` de VitaShell — `make send` si tenés
`PSVITAIP` configurado en el `cmake` cache.)

## Notas

- Esta guía es independiente de las pruebas en Vita3K documentadas en `plan_portabilidad.md` §9: varios de
  los workarounds ahí (p. ej. `EMULATOR_BUILD`, el chequeo de `kubridge` relajado) están pensados
  específicamente para que **no** interfieran con hardware real — compilá siempre con `EMULATOR_BUILD=OFF`
  (el valor por defecto) para la consola.
- El bug de texturas no-potencia-de-2 documentado en plan §9.17 es específico del renderer de Vita3K
  (confirmado leyendo su código fuente) — es muy probable que en hardware real, donde el swizzling lo hace
  la GPU directamente, ese problema no exista. Si logras arrancar una partida real en consola, es un dato
  valioso para confirmar o descartar esa hipótesis.
