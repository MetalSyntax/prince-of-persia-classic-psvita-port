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
de libc/log/etc.). Esto significa que **el orden de carga que ya proponía el borrador de `main.c` es
correcto y es obligatorio, no opcional**:

1. `libcocosdenshion.so` (sin dependencias internas)
2. `libcocos2d.so` (sin dependencias internas)
3. `libgame_logic.so` (depende de los dos anteriores — debe ser el último `so_file_load()`, y solo se
   debe llamar a `so_resolve()` sobre él después de que los otros dos ya estén cargados)

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

## 2. Preparación de assets — corrige un supuesto importante: **faltan los audios**

`bin/` **no contiene ningún archivo de audio** (se verificó: 0 `.ogg`/`.mp3`/`.wav` en todo `bin/`). Los
sonidos y música del juego están **solo dentro del APK**, en `assets/Extra/Audio/**` (formato `.mp3`, con
subcarpetas `Music/`, `SFX/`, `Ambiance/`), no en el `.obb`. El `.obb` (`original/main.1.org.ubisoft...zip`)
solo contiene las carpetas `Data*/{Animations,Effects,Localization,Logo,Maps,Particles,Texture}`, que es
justo lo que ya está en `bin/popclassic/`.

Por lo tanto, **hay que hacer una excepción puntual a "ignorar `original/`"**: es necesario extraer
`assets/Extra/Audio/` del APK (`original/Prince of Persia Classic 2.1.apk`, que es un zip) como parte de la
preparación de assets. `original/` sigue sin usarse para nada más (ni el `.obb`, ni el resto del APK).

Además, el `.obb` trae **tres variantes de resolución** (`Data/`, `Data_640_384/`, `Data_960_576/`).
Conviene verificar de cuál de las tres se extrajo `bin/popclassic/` y, si no es `Data_960_576`, considerar
re-extraer desde esa variante: la pantalla de PS Vita es 960×544, así que `Data_960_576` da el mejor calce
de resolución (menos escalado/reescalado de texturas por parte de vitaGL).

### 2.1. Pasos de esta fase

1. Confirmar variante de resolución usada en `bin/popclassic/` comparando tamaños/dimensiones de un par de
   `.png` contra las tres variantes dentro del `.obb`; re-extraer si conviene.
2. Extraer `assets/Extra/Audio/**` del APK a una carpeta nueva `bin/popclassic/Audio/` (manteniendo la
   subestructura `Music/`, `SFX/`, `Ambiance/`).
3. Convertir todos los `.mp3` (y los pocos `.m4a`) a `.ogg` (Vorbis) con `ffmpeg`, porque no hay un decoder
   de MP3/AAC maduro y libre de regalías en el ecosistema VitaSDK, y la ruta de audio nativa que hay que
   escribir (Fase 5) va a usar Tremor/libvorbis. Mantener los mismos nombres de archivo sin extensión para
   poder mapear 1:1 las rutas que pide el juego.
4. Layout final en la tarjeta de memoria:

```text
ux0:data/
└── popclassic/
    ├── libcocosdenshion.so
    ├── libcocos2d.so
    ├── libgame_logic.so
    └── Assets/
        ├── Animations/
        ├── Effects/
        ├── Localization/
        ├── Logo/
        ├── Maps/
        ├── Particles/
        ├── Texture/
        └── Audio/          <- añadido en esta fase, no viene en el bin/ original
            ├── Music/
            ├── SFX/
            └── Ambiance/
```

> [!IMPORTANT]
> Los nombres de archivos/carpetas deben coincidir exactamente en mayúsculas/minúsculas: `ux0:` es
> case-sensitive en la práctica para las rutas que resuelve `sceIo*`, a diferencia de lo que asumiría el
> código original de Android.

---

## 3. (Recomendado) Ingeniería inversa del flujo de arranque Java

Antes de adivinar en qué orden y con qué argumentos se llaman `nativeSetPaths`, `nativeSetPackageName`,
`nativeSetNumOfCPUCores`, `nativeInit`, etc., conviene decompilar el **`classes.dex`** del APK (esto es
leer el bytecode Java, no tocar `bin/` ni usar el `.obb`) con `jadx` o `apktool`:

```bash
jadx -d /tmp/pop_decomp "original/Prince of Persia Classic 2.1.apk"
```

Buscar la clase `org.cocos2dx.lib.Cocos2dxActivity` (o la subclase específica del juego, probablemente algo
como `com.ubisoft.mobile.popclassic.AppActivity`) y su método `onCreate`/`onLoadNativeLibraries` para leer
la secuencia exacta de llamadas nativas y sus argumentos. Esto reduce drásticamente el trabajo de prueba y
error de la Fase 4 y de la Fase 6, porque da la secuencia de inicialización "de referencia" tal cual la
ejecuta Android.

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
