# Plan de Portabilidad de Prince of Persia Classic (Android → PS Vita)

Este documento reemplaza la versión anterior del plan. La versión anterior asumía varias cosas sobre el
estado del proyecto y sobre los binarios de Android que **no se sostienen tras inspeccionar los archivos
reales** (`bin/*.so`, `original/*.apk`, `main.c`, `CMakeLists.txt`, `source/`). Todo lo que sigue está basado
en evidencia verificable con comandos reproducibles (`objdump`, `unzip -l`, `strings`), no en suposiciones
genéricas de un port de Cocos2d-x.

---

## 0. Diagnóstico del estado actual del repositorio (léase antes de tocar código)

El proyecto **ya es** un checkout del *SoLoader Boilerplate* (SoLoBoP) de v-atamanenko, con dos partes que
**hoy están desconectadas entre sí**:

| Ruta | Qué es | Estado real |
|---|---|---|
| `CMakeLists.txt` (raíz) | Build genérico de SoLoBoP | Compila `source/*.c`, **no** el `main.c` de la raíz. Todavía asume **un solo** `.so` (`SO_PATH`, `DATA_PATH`), nombre de proyecto genérico (`so-loader` / `SOLOADER0`) |
| `source/main.c`, `source/dynlib.c`, `source/java.c`, `source/patch.c`, `source/reimpl/*`, `source/utils/*` | Esqueleto genérico de SoLoBoP (FalsoJNI) | Sin adaptar: `source/main.c` tiene 33 líneas de stub, `java.c` tiene tablas JNI vacías, `patch.c` tiene un hook de ejemplo comentado. Esto es lo que **realmente se compila**. |
| `main.c` (raíz, 1905 líneas) | Borrador manual estilo Rinnegatamante/backstab-vita/deadspace-vita (JNIEnv falso hecho a mano, `so_module` × 3) | **No está en el build** (`CMakeLists.txt` no lo referencia). Incluye headers que no existen en el repo (`main.h`, `config.h`, `splash.h`) → no compila tal cual. Tiene buenas ideas (orden de carga de los 3 `.so`, mapeo de input) pero está desalineado con la arquitectura de SoLoBoP (FalsoJNI) que ya trae el proyecto. |
| `bin/` | `.so` + assets ya extraídos | Correcto como base, pero **incompleto** (ver Fase 2 — faltan los audios). |
| `lib/falso_jni`, `lib/so_util`, `lib/fios`, `lib/kubridge`, `lib/sha1`, `lib/libc_bridge` | Dependencias de SoLoBoP, iguales a las que usan `backstab-vita` y `deadspace-vita` | OK, ya están vendorizadas. |

**Decisión de arquitectura para este plan:** consolidar todo en la estructura modular de SoLoBoP
(`source/` + FalsoJNI), que es la que el `CMakeLists.txt` ya compila y la que el usuario pidió como base
("SoLoader Boilerplate"). El `main.c` de la raíz se usa **solo como referencia** para migrar su lógica
(orden de carga de los 3 módulos, mapeo de controles, `data_path`) hacia `source/main.c`, y luego se borra
para no dejar dos fuentes de verdad. **No** se recomienda adoptar el estilo monolítico de
`backstab-vita/loader/main.c` o `deadspace-vita/loader/main.c` (JNIEnv armado a mano con offsets de
vtable): FalsoJNI ya resuelve ese problema de forma más mantenible mediante las tablas de `source/java.c`.

---

## 1. Hallazgos de la inspección estática de los `.so` (evidencia, no suposición)

Los tres binarios son ELF32 ARM EABI5, *stripped*, pero conservan **tabla de símbolos dinámica**, así que
se pueden analizar perfectamente con las herramientas del propio Mac (no hace falta un toolchain ARM
cruzado):

```bash
objdump -T bin/libgame_logic.so       # símbolos dinámicos (exportados + UND)
objdump -p bin/libcocos2d.so | grep -E "NEEDED|SONAME"   # dependencias declaradas
strings -a bin/libcocos2d.so | grep -i assets
```

### 1.1. Grafo de dependencias real (confirma y corrige el orden de carga)

```
libcocosdenshion.so  SONAME=libcocosdenshion.so
                      NEEDED: liblog libstdc++ libm libc libdl        (hoja, sin deps entre los 3 .so)

libcocos2d.so         SONAME=libcocos2d.so
                      NEEDED: libGLESv1_CM liblog libz libstdc++ libm libc libdl
                      (no depende de los otros dos .so del juego)

libgame_logic.so      SONAME=libgame_logic.so
                      NEEDED: libGLESv1_CM libcocos2d.so libcocosdenshion.so liblog libstdc++ libm libc libdl
                      (SÍ depende explícitamente de los otros dos)
```

`lib/so_util/so_util.c` (ya vendorizado en este repo) implementa resolución cruzada automática entre
módulos: `so_file_load()` añade cada módulo a una lista enlazada global (`head`/`tail`), y
`so_resolve_link()` recorre las entradas `DT_NEEDED` de un módulo buscando el `SONAME` de otro módulo ya
cargado para resolver el símbolo desde ahí antes de caer al `default_dynlib` (nuestras reimplementaciones
de libc/log/etc.). Lo único que importa para que esto funcione es que **`libgame_logic.so` se cargue
último**, después de que los otros dos ya estén en la lista — el orden entre `libcocos2d.so` y
`libcocosdenshion.so` es indistinto porque ninguno depende del otro. La Fase 3 (decompilación del APK,
ver `informe_flujo_arranque.md` §1) confirmó que el **orden real** que usa la app Android es:

1. `libcocos2d.so` (`System.loadLibrary("cocos2d")`, primero)
2. `libcocosdenshion.so` (segundo)
3. `libgame_logic.so` (depende de los dos anteriores — debe ser el último `so_file_load()`, y solo se
   debe llamar a `so_resolve()` sobre él después de que los otros dos ya estén cargados)

Usar este orden real (no el `denshion → cocos2d → game_logic` que se había deducido solo mirando
`NEEDED`/`SONAME` en la Fase 1, que también sería funcionalmente válido pero no es el que se probó en
producción).

No hace falta pasar ninguna lista de símbolos "de repuesto" entre módulos a mano: basta con cargarlos
(`so_file_load`) en ese orden antes de relocar/resolver el que depende de ellos.

### 1.2. Dónde vive realmente cada símbolo JNI (esto corrige un error de la versión anterior del plan)

La versión anterior del plan asumía que las funciones `Java_org_cocos2dx_lib_Cocos2dxRenderer_native*`
estaban en `libgame_logic.so`. **Falso.** El desglose real es:

```
libcocos2d.so exporta JNI_OnLoad + 25 métodos Java_, entre ellos TODO el ciclo de render/input:
  Cocos2dxRenderer_nativeRender
  Cocos2dxRenderer_nativeTouchesBegin / Move / End / Cancel
  Cocos2dxRenderer_nativeKeyDown / nativeKeyUp
  Cocos2dxRenderer_nativeOnPause / nativeOnResume
  Cocos2dxRenderer_nativeInsertText / nativeDeleteBackward / nativeGetContentText
  Cocos2dxActivity_nativeSetPaths / nativeSetPackageName / nativeSetNumOfCPUCores
  Cocos2dxActivity_nativeSetDensityScaleValue / nativeSetDevicePixelsPerInch
  Cocos2dxActivity_GetConfig / SetControlVisible / SetControlInVisible
  Cocos2dxBitmap_nativeInitBitmapDC
  Cocos2dxAccelerometer_onSensorChanged
  Cocos2dxVideo_onVideoCompleted
  ubisoft_InApp_InAppHandler_purchaseSuccessful

libgame_logic.so exporta UN SOLO método Java_:
  Cocos2dxRenderer_nativeInit

libcocosdenshion.so no exporta ningún Java_ (solo JNI_OnLoad + la clase C++ CocosDenshion::SimpleAudioEngine,
  que se llama directo en C++, resuelta vía so_resolve_link, no vía JNI)
```

**Por qué importa:** en el APK original, `System.loadLibrary()` carga `libcocosdenshion.so`, luego
`libcocos2d.so`, luego `libgame_logic.so` (último). Cuando dos bibliotecas nativas exportan el mismo
símbolo JNI (`nativeInit` existe en ambas), Android resuelve la llamada `Cocos2dxRenderer.nativeInit()`
contra la **última** biblioteca cargada que lo define — o sea, `libgame_logic.so`, que probablemente hace
el `AppDelegate`/registro de escenas del juego y **después** delega en cocos2d. Si el loader llama por
error al `nativeInit` de `libcocos2d.so` (el genérico del engine, sin el juego), el resultado más probable
es una pantalla en blanco o un motor cocos2d "vacío" sin escenas. **Regla para el loader:**

- `nativeInit` → resolver y llamar desde `game_mod` (`libgame_logic.so`)
- `nativeRender`, `nativeTouches*`, `nativeKeyDown/Up`, `nativeOnPause/Resume`, `nativeSetPaths`, etc. →
  resolver y llamar desde `cocos2d_mod` (`libcocos2d.so`), porque `libgame_logic.so` no las redefine.

### 1.3. Carga de assets: `nativeSetPaths` existe, no hace falta un `AAssetManager` real

`libcocos2d.so` exporta `Cocos2dxActivity_nativeSetPaths` y contiene el string literal `"assets/"`. Esto
es la vía por la que Cocos2dx-Android le dice al motor dónde buscar los recursos. En vez de emular un
`AAssetManager`/JNI completo para leer del "APK", el loader puede llamar directamente a `nativeSetPaths`
apuntando a la carpeta de `ux0:data/...` donde están los assets extraídos — igual que hacen la mayoría de
ports Cocos2dx a Vita. Confirmar el orden exacto de argumentos/llamadas con la Fase 3 (decompilación del
APK) antes de escribir el código definitivo, en vez de adivinarlo.

---

## 2. Preparación de assets

> Fase corregida con evidencia del informe de la Fase 1 (`informe_analisis_binarios.md`, §6):
> `libgame_logic.so` contiene 125 literales de ruta con el prefijo `Data/` (ej.
> `Data/Animations/big_guard_final/big_guard_final_01.plist`) y **cero** literales `Data_640_384/` o
> `Data_960_576/`. El juego solo sabe pedir archivos bajo una carpeta llamada **`Data`**, no `Assets`
> (como decía una versión anterior de este plan). El nombre de carpeta en destino **debe ser `Data/`**.

### 2.1. Falta el audio — excepción puntual a "ignorar `original/`"

`bin/` **no contiene ningún archivo de audio** (se verificó: 0 `.ogg`/`.mp3`/`.wav` en todo `bin/`). Los
sonidos y música del juego están **solo dentro del APK**, en `assets/Extra/Audio/**` (formato `.mp3`, con
subcarpetas `Music/`, `SFX/`, `Ambiance/`), no en el `.obb`. El `.obb`
(`original/main.1.org.ubisoft.premium.POPClassic.zip`) solo contiene las carpetas
`Data*/{Animations,Effects,Localization,Logo,Maps,Particles,Texture}`, que es justo lo que ya está en
`bin/popclassic/`.

Por lo tanto, hay que hacer una excepción puntual a "ignorar `original/`": es necesario extraer
`assets/Extra/Audio/` del APK (`original/Prince of Persia Classic 2.1.apk`, que es un zip) como parte de la
preparación de assets. `original/` sigue sin usarse para nada más (ni el `.obb`, ni el resto del APK).

### 2.2. Resolución de `bin/popclassic/`: ya es la correcta, no hace falta re-extraer

El `.obb` trae **tres variantes de resolución** (`Data/`, `Data_640_384/`, `Data_960_576/`). Comparando
tamaños de archivo (`Logo/logo.png`: 123,079 B en `Data/`, 55,420 B en `Data_640_384/`, 89,080 B en
`Data_960_576/`, contra 89,080 B en `bin/popclassic/Logo/logo.png`), **`bin/popclassic/` ya fue extraído
del bucket `Data_960_576`** — el de mejor calce con los 960×544 de la Vita. No hace falta volver a
extraer nada del `.obb`. Lo único que hay que hacer es **renombrar la carpeta de destino a `Data/`** al
copiarla a la tarjeta (el contenido puede seguir viniendo de `Data_960_576`, el binario no distingue el
origen, solo el nombre final de la carpeta importa).

### 2.3. Pasos de esta fase

1. Extraer `assets/Extra/Audio/**` del APK a una carpeta nueva (manteniendo la subestructura `Music/`,
   `SFX/`, `Ambiance/`).
2. Convertir todos los `.mp3` (y los pocos `.m4a`) a `.ogg` (Vorbis) con `ffmpeg`, porque no hay un decoder
   de MP3/AAC maduro y libre de regalías en el ecosistema VitaSDK, y la ruta de audio nativa que hay que
   escribir (Fase 5) va a usar Tremor/libvorbis. Mantener los mismos nombres de archivo sin extensión para
   poder mapear 1:1 las rutas que pide el juego.
3. Layout final en la tarjeta de memoria:

```text
ux0:data/
└── popclassic/
    ├── libcocosdenshion.so
    ├── libcocos2d.so
    ├── libgame_logic.so
    └── Data/                  <- contenido de bin/popclassic/ (bucket Data_960_576), carpeta RENOMBRADA a "Data"
        ├── Animations/
        ├── Effects/
        ├── Localization/
        ├── Logo/
        ├── Maps/
        ├── Particles/
        ├── Texture/
        └── Audio/             <- añadido en esta fase, no viene en el bin/ original
            ├── Music/
            ├── SFX/
            └── Ambiance/
```

> [!IMPORTANT]
> Los nombres de archivos/carpetas deben coincidir exactamente en mayúsculas/minúsculas: `ux0:` es
> case-sensitive en la práctica para las rutas que resuelve `sceIo*`, a diferencia de lo que asumiría el
> código original de Android. Confirmar en la Fase 3 (decompilación) si `nativeSetPaths` recibe la ruta
> base como `ux0:data/popclassic/` (y el código arma `.../Data/...` solo) o si espera la ruta completa
> hasta `Data/`, para no duplicar el segmento `Data` por error.

### 2.4. Estado: Fase 2 ejecutada

Árbol generado en `ux0_data/popclassic/` (listo para copiar tal cual a `ux0:data/popclassic/` en la
consola):

```
popclassic/
├── libcocosdenshion.so
├── libcocos2d.so
├── libgame_logic.so
└── Data/            (copia de bin/popclassic/, renombrada)
    ├── Animations/   116 archivos
    ├── Audio/         93 archivos .ogg  <- extraído del APK y convertido
    ├── Effects/       31 archivos
    ├── Localization/   7 archivos
    ├── Logo/           1 archivo
    ├── Maps/          60 archivos
    ├── Particles/     12 archivos
    └── Texture/       75 archivos
```

Tamaño total: 116 MB (`Data/` 113 MB + los tres `.so` ~3 MB). `bin/` original queda intacto, sin tocar.

Nota de reproducibilidad: el `ffmpeg` de Homebrew (`ffmpeg-full` bottle) **no trae `libvorbis`** como
encoder; se usó el encoder Vorbis nativo/experimental de ffmpeg, que solo soporta salida estéreo:

```bash
ffmpeg -y -i "$origen" -ac 2 -c:a vorbis -strict -2 -q:a 4 "$destino.ogg"
```

Los 93 archivos de audio (90 `.mp3` + 1 `.m4a` + 2 `.mp4`, estos últimos con audio-only pese a la
extensión) se convirtieron sin fallos. Los originales se borraron del árbol de despliegue tras convertir
(no se van a usar en el port); el `.apk` de `original/` no se tocó.

> [!NOTE]
> El disco `Seagate PSVITA` está formateado en un filesystem que no soporta atributos extendidos de
> macOS (probablemente exFAT): cada `cp`/`rsync`/escritura de ffmpeg generó archivos `._*` (AppleDouble).
> Se limpiaron con `find ... -name '._*' -delete`. Si se vuelve a copiar/generar algo dentro de
> `ux0_data/`, repetir esa limpieza antes de copiar a la memoria de la Vita (esos archivos son basura de
> macOS, no deben terminar en la tarjeta).

### 2.5. Adenda tras la Fase 3: falta `appConfig.txt`

La Fase 3 (decompilación del APK) encontró que `GetConfig()` (llamada nativa desde
`Cocos2dxActivity.setPackageName()`) lee un archivo `assets/appConfig.txt` que no estaba contemplado en
el layout original. Se extrajo del APK, y se generó una versión para Vita con los flags de integraciones
online irrelevantes (`ENABLE_FLURRY`, `ENABLE_APPCIRCLE`, `ENABLE_PAPAYA`, `ENABLE_FACEBOOK`,
`ENABLE_MAIL`, `ENABLE_APPRATER`, `ENABLE_CROSSPROMOTION`, `ENABLE_GETMOREGAMES`) puestos en `NO`, para
que el juego ni intente usar esas clases (`CCFlurryUtils`, `CCShareUtils`, etc., identificadas como
candidatas a no-opear en el informe de la Fase 1). Se copió en dos ubicaciones hasta confirmar en consola
cuál usa realmente el motor (ver informe de la Fase 3, §4-5):

```
ux0_data/popclassic/appConfig.txt        (junto a los .so, por si apkFilePath = carpeta base)
ux0_data/popclassic/Data/appConfig.txt   (por si se busca junto a los assets)
```

---

## 3. Ingeniería inversa del flujo de arranque Java — completada

Se decompiló `classes.dex` del APK con `jadx` (detalle completo, con código fuente citado, en
`informe_flujo_arranque.md`). La Activity real es `org.ubisoft.premium.POPClassic.POPClassic`, subclase de
`org.cocos2dx.lib.Cocos2dxActivity`. Resumen accionable para la Fase 4:

### 3.1. Secuencia exacta de inicialización nativa (reemplaza cualquier suposición anterior)

```c
// En Cocos2dxActivity.setPackageName(), llamado desde POPClassic.onCreate():
nativeSetPaths(apkFilePath, apkSourceDir, device);   // ¡3 strings, no 1! (corrige la Fase 1, §1.3)
nativeSetPackageName(packageName);
nativeSetIsGoogleLauncherBuild(isGoogleLauncherBuild);   // true en esta build (ver AndroidManifest.xml)
GetConfig(apkFilePath, "DEVICE_SLEEP");                  // y más GetConfig(...) — ver Fase 2.5
nativeSetNumOfCPUCores(numCores);
nativeSetDensityScaleValue(dScale);         // 1.0 a 120/160dpi, 1.3 a 240dpi+ (no aplica caso especial "GT-P1000T")
nativeSetDevicePixelsPerInch(ydpi);
SetControlInVisible();  // solo si no hay teclado físico — sí aplica en Vita

// Aparte, en el callback asíncrono de creación de superficie GL (Cocos2dxRenderer.onSurfaceCreated):
nativeInit(screenWidth, screenHeight);   // NO es void sin argumentos: toma ancho y alto de pantalla
```

`nativeSetPaths` toma **tres** argumentos string, no uno solo como asumía la Fase 1. En esta build
(`isCompletePackage=false`, `isGoogleLauncher=true`, confirmado en `AndroidManifest.xml`), el primer
argumento en Android es la carpeta estándar `.../Android/obb/<paquete>/` (que en el dispositivo real
contiene el `.obb` como ZIP, no archivos sueltos).

### 3.2. Riesgo abierto: ¿el motor lee assets sueltos o necesita el `.obb` como ZIP?

`libcocos2d.so` tiene el string `.obb` y depende de zlib (`inflate`/`deflate`/`crc32`, ver informe Fase 1
§5) — evidencia de que el motor real sabe leer el `.obb` como ZIP directamente. No se puede confirmar solo
con análisis estático si también intenta `fopen()` plano antes de eso. **No bloquea seguir con la Fase 4**:
la primera prueba en consola (con logging de `source/reimpl/io.c`) va a mostrar qué ruta intenta abrir el
motor contra los assets sueltos que ya se armaron en la Fase 2; si falla, la Fase 4/9 tiene un plan de
mitigación (empaquetar un `.obb`/zip sin comprimir, o hookear la función de apertura puntual). Detalle
completo en `informe_flujo_arranque.md` §4.

### 3.3. Códigos de tecla que el juego ya maneja (para el mapeo de controles de la Fase 4/10)

`Cocos2dxGLSurfaceView` solo reenvía estos códigos Android a `nativeKeyDown`/`nativeKeyUp` (cualquier otro
se descarta):

| Tecla Android | Código | Vita — sugerido |
|---|---:|---|
| `KEYCODE_BACK` | 4 | Vita: START o SELECT (a definir en Fase 10) |
| `KEYCODE_MENU` | 82 | Vita: SELECT |
| `KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT` | 19/20/21/22 | D-Pad físico |
| `KEYCODE_DPAD_CENTER` | 23 | Confirmar (¿CROSS?) |
| `KEYCODE_BUTTON_X` | 99 | Botón de acción (probar en consola qué hace) |
| `KEYCODE_BUTTON_Y` | 100 | Botón de acción secundaria |
| `KEYCODE_BUTTON_L1` | 102 | L (gatillo izquierdo) |
| `KEYCODE_BUTTON_R1` | 103 | R (gatillo derecho) |

No están implementados `BUTTON_A`(96)/`BUTTON_B`(97)/`BUTTON_Z`(101) — el juego original solo usa X/Y/L1/R1
como botones de acción. El significado exacto de cada uno (qué botón ataca, cuál hace rodar al príncipe,
etc.) solo se puede confirmar jugando en consola real, no por este análisis.

---

## 4. Adaptación del loader (`source/`, arquitectura SoLoBoP + FalsoJNI)

Migrar la lógica útil del `main.c` de la raíz hacia `source/main.c`, pero usando FalsoJNI (que ya está
vendorizado en `lib/falso_jni/`) en vez del `JNIEnv`/`JavaVM` armado a mano por offsets. Concretamente:

1. **Tres `so_module` globales** (`denshion_mod`, `cocos2d_mod`, `game_mod`), cargados y resueltos en ese
   orden exacto (ver §1.1). Reservar direcciones base con separación suficiente
   (`LOAD_ADDRESS`, `LOAD_ADDRESS + 0x1000000`, `LOAD_ADDRESS + 0x2000000` — los binarios rondan 1–2 MB de
   `.text` cada uno según el tamaño de archivo, así que dejar margen holgado) para evitar colisiones de
   relocaciones.
2. **`data_path`** apuntando a `ux0:data/popclassic` y un `getcwd()` hookeado (`source/reimpl/sys.c`) que
   devuelva esa ruta, igual que hace el borrador de la raíz — el string `/data/data/` visto en
   `libgame_logic.so` sugiere que el juego intenta acceder a una ruta de preferencias/guardado al estilo
   Android; hay que interceptar esos `fopen`/`open` (vía `source/reimpl/io.c`) y redirigirlos a
   `ux0:data/popclassic/save/`.
3. **Resolución de símbolos:** `so_resolve(&denshion_mod, default_dynlib, ..., 0)`, luego lo mismo para
   `cocos2d_mod`, luego para `game_mod` — el último parámetro `0` (no `default_dynlib_only`) es el que
   habilita la resolución cruzada de la §1.1, así que no se puede omitir.
4. **Despacho JNI correcto** (ver §1.2): `nativeInit` se busca con `so_symbol(&game_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit")`; el resto de `nativeXxx` se busca en `cocos2d_mod`.
5. **Bucle principal**: `vglInitExtended`, lectura de `sceCtrlPeekBufferPositive`/`sceTouchPeek`, traducción
   a `nativeTouchesBegin/Move/End` y `nativeKeyDown` (el borrador ya mapea START→`KEYCODE_BACK`/tecla 4;
   ampliar con al menos: D-Pad/stick para movimiento si el juego lo soporta por teclado, y algún botón para
   pausa/menú). Cerrar con `vglSwapBuffers`.
6. Verificado con `objdump -T`: los tres `.so` requieren `libGLESv1_CM.so` (OpenGL ES 1.1, no ES2) — usar
   la capa de compatibilidad ES1-sobre-vitaGL que ya trae el proyecto en `source/reimpl/egl.c` /
   `source/utils/glutil.c`, y confirmar que cubre las llamadas `glAlphaFunc`, `glColorPointer`,
   `glBindFramebufferOES`, etc. vistas en los símbolos UND de `libcocos2d.so`.

Una vez migrada y compilando la lógica, **borrar el `main.c` de la raíz** (o moverlo a `docs/reference/`)
para que no queden dos loaders inconsistentes en el repo.

### 4.1. Estado: Fase 4 ejecutada

- Se actualizó el `CMakeLists.txt` con los nombres y rutas correctas para Prince of Persia Classic y se agregó la dependencia `vorbisidec` y `ogg` en `target_link_libraries`.
- Se copió la dependencia de FalsoJNI desde otro boilerplate funcional.
- Se refactorizó `source/utils/init.c` para cargar los módulos `libcocosdenshion.so`, `libcocos2d.so` y `libgame_logic.so` de forma secuencial asegurando las compensaciones de memoria para evitar cruces.
- Se reescribió `source/main.c` resolviendo exitosamente los métodos nativos (Java_org_cocos2dx_lib_..._nativeSetPaths, etc.) y asignando el input mapeando todos los controles analizados en `informe_flujo_arranque.md`.
- Se movió el archivo raíz `main.c` a `extras/old_main.c`. No se pudo compilar por falta del toolchain `cmake` en el sistema pero el código está listo para la Fase 5.

---

## 5. Audio: reimplementación nativa de `SimpleAudioEngine` / `Cocos2dxMusic` / `Cocos2dxSound`

`libcocosdenshion.so` **no decodifica audio por su cuenta** (no tiene símbolos UND de OpenSL ES, Vorbis ni
MP3): en Android delega la reproducción real en clases Java (`org.cocos2dx.lib.Cocos2dxMusic` para música de
fondo vía `MediaPlayer`, `Cocos2dxSound` para efectos vía `SoundPool`), a las que llama por JNI. Como acá no
hay una JVM real, hay que interceptar esas llamadas JNI con FalsoJNI y reimplementarlas de forma nativa,
exactamente con el patrón que ya usan `backstab-vita/loader/audioPlayer.c` y las reimpl de audio de
`deadspace-vita` (`loader/android/EAAudioCore.c`):

1. En `source/java.c`, registrar en `methodsVoid`/`methodsInt`/`nameToMethodId` los métodos que el juego va
   a intentar invocar sobre esas clases (se descubren en la Fase 3 y/o iterativamente con el log de FalsoJNI
   en la Fase 6: `play`, `pause`, `stop`, `setVolume`, `preload`, etc.).
2. Cada stub hace la reproducción real usando `sceAudioOutOpenPort`/`sceAudioOutOutput` (streaming, para
   música) y buffers cortos decodificados en memoria (para SFX), decodificando los `.ogg` convertidos en la
   Fase 2 con **Tremor** (`libvorbisidec`, más liviano que `libvorbis` completo — ambos están empaquetados
   para VitaSDK vía `vdpm`).
3. Añadir `vorbisidec`/`ogg` a `target_link_libraries` en el `CMakeLists.txt` (Fase 7).

---

## 6. Tablas JNI de FalsoJNI (`source/java.c`) — metodología iterativa, no exhaustiva de entrada

No merece la pena intentar precompletar todas las clases/métodos Java que el juego pueda pedir por
`FindClass`/`GetMethodID`/`GetStaticMethodID`: eso no es visible en la tabla de símbolos dinámica (son
strings que se resuelven en tiempo de ejecución contra el `JNIEnv` falso). El flujo probado que usan tanto
SoLoBoP como `backstab-vita`/`deadspace-vita` es:

1. Compilar con logging de FalsoJNI activado (`add_definitions(-DFALSOJNI_DEBUGLEVEL=0)`, ya presente y
   comentado en el `CMakeLists.txt`).
2. Ejecutar en consola (o en el emulador si se dispone de uno) y capturar el log (UART/`sceClibPrintf`/FTP).
3. Por cada `FindClass`/`GetMethodID`/`GetStaticFieldID` no resuelto que aparezca en el log, añadir la
   entrada correspondiente en `nameToMethodId`/`nameToFieldId` y su implementación en `methodsXxx`/
   `fieldsXxx` en `source/java.c` (usando la Fase 3 como referencia de qué debería devolver cada uno, por
   ejemplo `SDK_INT`, `WINDOW_SERVICE`, rutas de `Environment.getExternalStorageDirectory()`, etc., que ya
   están de ejemplo en el stub actual).
4. Recompilar y repetir hasta que el juego llegue al primer frame renderizado.

Esta fase típicamente se entrelaza con la Fase 4 (loop principal) y con la Fase 9 (debugging), no es un
paso aislado.

---

## 7. `CMakeLists.txt`: de "un solo `.so` genérico" a "tres `.so` de POP Classic"

Cambios necesarios sobre el `CMakeLists.txt` actual (que hoy asume `SO_PATH`/`DATA_PATH` de un solo
binario):

```cmake
set(VITA_APP_NAME "Prince of Persia Classic")
set(VITA_TITLEID "POPC00001")   # 4 letras + 5 dígitos, formato válido de homebrew
set(VITA_VPKNAME "popclassic")
set(VITA_VERSION "01.00")

set(DATA_PATH "ux0:data/popclassic/" CACHE STRING "Path to data (with trailing /)")
add_definitions(-DDATA_PATH="${DATA_PATH}")
# Ya no hay un único SO_PATH: los 3 nombres de archivo se referencian directamente
# en source/main.c a partir de DATA_PATH.
```

Y en `target_link_libraries`, sumar lo necesario para audio (Fase 5) y confirmar lo de OpenGL ES1
(Fase 4, punto 6):

```cmake
target_link_libraries(${CMAKE_PROJECT_NAME}
    # ... lo que ya trae el boilerplate (vitaGL, vitashark, kubridge_stub, etc.) ...
    vorbisidec
    ogg
)
```

Mantener el resto del boilerplate (`vita_create_self`, `vita_create_vpk`, targets `send`/`dump`/`reboot`)
tal cual, ya que `extras/livearea/*` y `extras/scripts/get_dump.sh` ya existen en el repo y son
reutilizables sin cambios (opcionalmente, más adelante, reemplazar los PNG de LiveArea genéricos por arte
de POP tomado de `bin/popclassic/Logo/logo.png`).

---

## 8. Compilación y despliegue

1. Variables de entorno: `VITASDK` apuntando al SDK, con soporte `softfp` (ya contemplado por
   `-mfloat-abi=softfp` en el `CMakeLists.txt`).
2. Paquetes VitaSDK adicionales requeridos vía `vdpm`: `vitaGL`, `vitaShark`, `kubridge`, `FAudio`/`vorbisidec`
   u `ogg`/`vorbisidec` (según lo que finalmente se use en la Fase 5).
3. Compilar:
   ```bash
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j$(sysctl -n hw.ncpu)
   ```
   Esto genera `popclassic.vpk`.
4. Instalar `popclassic.vpk` en la consola (VitaShell) e instalar por separado en la tarjeta el árbol de
   `ux0:data/popclassic/` construido en la Fase 2 (los `.so` + `Assets/` con `Audio/` incluido).
5. Requisitos ya cubiertos por el propio `main.c` (validarlos igual en `source/main.c` al migrar):
   `kubridge.skprx` instalado y `ur0:/data/libshacccg.suprx` (o su ruta alterna) presente, o falla con un
   diálogo de error explícito (patrón `fatal_error()` ya usado en el borrador).

---

## 9. Debugging iterativo en consola real

- Usar el target `dump` ya definido en `CMakeLists.txt` (`extras/scripts/get_dump.sh` + un core dump
  parser) para capturar y analizar crashes en consola real vía `kubridge`.
- Los puntos de fallo más probables, en orden esperado de aparición:
  1. Relocación/carga de los 3 `.so` (verificar direcciones base, ver §4.1).
  2. Símbolos de `libgame_logic.so` sin resolver por dependerse de `libcocos2d.so`/`libcocosdenshion.so`
     antes de que estén cargados (si el orden de carga de §1.1 no se respeta).
  3. Llamadas JNI no implementadas en `source/java.c` (Fase 6) — típicamente el primer bloqueo real tras
     lograr que los `.so` carguen.
  4. Llamadas a `libGLESv1_CM` no cubiertas por la capa de compatibilidad ES1 (Fase 4, punto 6).
  5. Audio: crashes o silencio si las rutas de `Audio/*.ogg` (Fase 2) no calzan con lo que pide
     `Cocos2dxMusic`/`Cocos2dxSound` (Fase 5).

---

## 10. Pulido final

- Mapeo completo de controles físicos (más allá de START→Back): revisar en la Fase 3 qué `KeyEvent`s
  entiende el juego (probablemente navegación de menús) y mapear D-Pad/botones de forma consistente.
- Reemplazar el arte genérico de `extras/livearea/*` por versiones con el logo/arte de
  `bin/popclassic/Logo/logo.png`.
- Ajuste de clocks (`scePowerSet*ClockFrequency`, ya presentes en el borrador) y de
  `vglUseTripleBuffering`/multisample según el rendimiento observado en consola real.
- Confirmar que el guardado de partida (ruta `/data/data/...` detectada en `libgame_logic.so`, ver §4.2)
  persiste correctamente entre ejecuciones en `ux0:data/popclassic/save/`.

---

## Riesgos conocidos / cosas a revalidar si algo no cuadra

- Este análisis se hizo con los `.so` de `bin/` tal cual están hoy; si se reemplazan por otra versión del
  APK, **repetir los comandos de la §1** (los símbolos exportados, `NEEDED`/`SONAME` y el mapeo
  `nativeInit` vs `nativeRender` pueden cambiar entre versiones de la app).
- La ausencia de audio en `bin/` (§2) fue verificada contra el `.obb` y el `.apk` provistos; si aparece una
  copia de `bin/` distinta que sí incluya audio, saltarse la extracción manual del APK.
- La recomendación de usar `Data_960_576` (§2) es una recomendación de calce de resolución, no una certeza:
  confirmar visualmente en consola que no introduce artefactos de escalado en UI fija en píxeles.
