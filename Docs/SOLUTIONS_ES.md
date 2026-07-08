# Historial de Soluciones y Parches (PS Vita Port)

Este documento detalla los problemas críticos encontrados durante el desarrollo del port de *Prince of Persia Classic* para PS Vita y las soluciones técnicas implementadas en el *Wrapper*.

## 1. Crasheo por "Unknown Symbol" (Ej. 0x99165940)
**Problema:**
Al intentar cargar un nuevo nivel, el juego se cerraba abruptamente mostrando un mensaje de error como `Unknown symbol "???" (0x99165940)`. El error ni siquiera mostraba el nombre de la función faltante debido a que la dirección de memoria solicitada estaba fuera de los límites esperados por el capturador de errores.

**Solución:**
* **Mejora del Log de Errores (`so_util.c`):** Se modificó la función `reloc_err` para buscar en *todas* las reubicaciones de todos los módulos (`.text`, `.data`, etc.) eliminando la restricción de límites estricta. También se inyectó un parche para imprimir *todos* los símbolos faltantes durante la carga inicial en el archivo de logs antes de que el juego colapse.
* **Símbolos exportados (`dynlib.c`):** Gracias al log mejorado, descubrimos que el motor de Android intentaba llamar funciones estándar de `C` y de compresión que no estaban expuestas al `.so`. Se añadieron las funciones `bsearch`, `cosh` y `gzread` al arreglo `default_dynlib[]` para enlazar el binario del juego con las librerías nativas `libc` y `libz` de la PS Vita.

## 2. Textos Invisibles, Cuadros Rotos y Parpadeo (Scroll)
**Problema:**
Las letras con tildes (á, é) y las eñes (ñ) se renderizaban como cuadrados blancos. Ciertos cuadros de diálogo o menús ("Quick games") aparecían completamente vacíos. Adicionalmente, al finalizar un nivel, el texto temblaba o parpadeaba bruscamente al hacer scroll hacia arriba.

**Solución (`java.c` - `Cocos2dxBitmap_createTextBitmap`):**
* **Decodificador UTF-8:** Se implementó una función `utf8_decode` para reemplazar el salto estricto por byte (ASCII) al iterar la cadena de texto, permitiendo que `stb_truetype` reconozca y dibuje correctamente los caracteres especiales del español u otros idiomas.
* **Auto-tamaño de lienzos (Bounding Boxes):** Se agregó lógica para que, cuando el motor solicita dimensiones `0x0`, el Wrapper calcule automáticamente el ancho máximo (`max_w`) y la cantidad de líneas (`num_lines`) necesarias para crear una caja de tamaño exacto. Esto solucionó los textos que desaparecían.
* **Pixel-Perfect Rendering:** Se eliminó un hack que multiplicaba la escala de la fuente por `1.25x` y sumaba un margen artificial de `+4` píxeles. Esto restauró el renderizado nativo de 1:1 píxeles, evitando que Cocos2d-x genere artefactos al hacer "snapping" a sub-píxeles durante un movimiento de scroll suave.

## 3. Unificación y Persistencia de Logs (Debug)
**Problema:**
Los mensajes informativos y de depuración estándar se imprimían usando `sceClibPrintf` o `printf`, por lo que solo eran visibles conectando la consola a un terminal en vivo. El usuario no tenía forma sencilla de auditar la carga en una PS Vita real si el juego no crasheaba directamente.

**Solución:**
* **Redirección Global (`logger.c`):** Se modificó el código fuente a nivel general (`dynlib.c`, `java.c`, `so_util.c`, etc.) para reemplazar las salidas por consola tradicionales hacia llamadas de la librería interna `l_debug` y `l_info`. 
* Ahora, absolutamente todos los procesos (inicialización de JNI, parches de OpenGL, carga de texturas, y errores) se guardan automáticamente y de forma permanente en `ux0:data/popclassic/logs/log_TIMESTAMP_.txt`, permitiendo auditar la ejecución de principio a fin desde VitaShell.

## 4. Audio Completo con SoLoud (Crash `PC=0x20` al Abrir el Juego)
**Problema:**
La primera integración de SoLoud crasheaba el port al abrirlo, antes de entrar al menú (11 core dumps entre las 00:16 y la 01:06 del 7-jul-2026). Como medida de emergencia se publicó una build estable con el audio desactivado (`popclassic.vpk`). El análisis forense de los dumps con `vita-parse-core` (excepción `Prefetch abort`, `PC=0x20` constante en el hilo principal, cadena de pila `LogoScene → MenuScene → MainMenuLayer::init → playBackgroundMusic → despachador JNI`) demostró que el crash estaba en la **ruta de fallo de carga** del BGM del menú: el archivo no se encontraba en la consola (`error 2`) y el código seguía de largo llamando a través de un puntero sin inicializar.

**Solución (`source/audio.cpp`, `source/audio_path.h`, `source/java.c`):**
* **Reimplementación blindada sobre SoLoud** (backend `vita_homebrew`): BGM por *streaming* (`WavStream`), SFX decodificados en RAM con caché (`Wav`), y un *voice group* para pausar/detener todos los efectos sin tocar la música. Ninguna ruta de error ejecuta punteros: carga fallida = log + silencio + *dummy handle* incremental (nunca `0`); todo objeto de audio pasa por `stopAudioSource()` antes de liberarse.
* **Sanitizador de rutas testeable:** `sanitize_audio_path` vive en un header puro (`audio_path.h`) que se compila idéntico en la consola y en la Mac. Traduce `Extra/Audio/*.mp3|.m4a|.mp4` → `ux0:data/popclassic/Data/Audio/*.ogg` (las peticiones `.mp4` de audio, como `94_jaffar_fight.mp4`, son sonido, no video) con *fallback* a `Data_960_576/`.
* **Pruebas automáticas previas al build** (`extras/tests/run_tests.sh`): sanitizador contra los formatos reales de petición; verificación de que las 84 rutas que el juego puede pedir (extraídas de los `.so` con `strings`) resuelven a un `.ogg` existente (84/84); y decodificación completa de los 93 `.ogg` con el `stb_vorbis` vendorizado (93/93, 9.3M samples).
* **Tabla JNI saneada:** se eliminó el registro duplicado de `preloadEffect` en `methodsVoid[]` (vivía también en `methodsInt[]` con firma incompatible) y se restauró el disparo inmediato de `onVideoCompleted` en el stub de `playVideo` (sin él, "New Game" se cuelga esperando el fin de un video que nunca se reproduce — ver §9.20 del plan).
* **Build identificable:** `popclassic_soloud.vpk`, app "Prince of Persia Classic SND" v01.10, mismo `TITLEID` (conserva saves). El script de build ahora archiva `build/so_loader.elf` con símbolos para poder simbolizar futuros `.psp2dmp`.

> **Actualización final:** dos dumps posteriores demostraron que la reimplementación SoLoud descrita arriba seguía crasheando por un contrato interno de SoLoud (stb_vorbis sin su "file hack") y por un `__sF_fake` demasiado pequeño. SoLoud fue eliminado del build: el audio definitivo es un mixer propio sobre `sceAudioOut` (build `popclassic_audio.vpk` v01.11). Detalle completo en `Fixes_Log.md` #10 y `plan_portabilidad.md` §9.30.
