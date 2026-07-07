# Registro de Fixes y Soluciones - Prince of Persia (PS Vita Port)
## Fixes Log and Solutions - Prince of Persia (PS Vita Port)

---

### 🇪🇸 Español

#### 1. Mapeo de Controles y Movimiento (D-Pad y Analógico Izquierdo)
- **Problema:** El personaje no caminaba ni corría correctamente al usar la cruceta o el joystick analógico izquierdo; solo lograba girar sobre su propio eje.
- **Solución:** Se reconfiguró la captura de inputs físicos de la PS Vita para convertirlos en eventos de toque virtuales (Toques táctiles sintetizados). Utilizando una imagen de referencia, se mapearon las coordenadas exactas de la pantalla táctil que corresponden a los botones virtuales de interfaz del juego (flechas de caminar y correr).

#### 2. Implementación del Motor de Audio (SoLoud C++)
- **Problema:** El juego no emitía música ni efectos de sonido, ya que el motor original de Cocos2dx de Android no podía decodificar los audios en la PS Vita.
- **Solución:** Se integró el motor nativo en C++ **SoLoud**. Se interceptaron las llamadas Java a JNI (Cocos2dxMusic y Cocos2dxSound) y se reemplazaron por llamadas a la API de SoLoud. Los BGMs (Música de fondo) se implementaron mediante `WavStream` para no saturar la memoria RAM, mientras que los SFX se implementaron con `Wav` y un mapa de caché en memoria para acceso ultrarrápido.

#### 3. Bug de Salto Infinito y Lógica Rota (Dummy Audio Handles)
- **Problema:** Cuando el juego no lograba encontrar un archivo de sonido, la API devolvía un identificador (`handle`) con valor `0`. Esto provocaba que el motor interno de Cocos2d-x entrara en un bucle lógico de error, causando un "Bug de salto infinito".
- **Solución:** Se implementó un sistema de `Dummy Handles` incrementales (900000X). Si la carga falla, el juego recibe un handle numérico válido, evitando el bloqueo del bucle del motor, y simplemente guarda silencio sin romper el gameplay.

#### 4. Redirección de Rutas de Audio y Carpeta Fallback
- **Problema:** El código de Java del juego solicitaba archivos `.mp3` dentro de una carpeta `/Extra/`. Sin embargo, en el juego desempaquetado, los archivos estaban en formato `.ogg` y ubicados en `/Data/`. Además, el motor a veces solicitaba assets en resoluciones específicas (ej. `Data_960_576`).
- **Solución:** Se agregó un traductor de rutas (`sanitize_audio_path`) que captura peticiones falsas y las redirige hacia su equivalente `.ogg` en `/Data/`. Adicionalmente, se añadió un sistema de "fallback": si el motor falla al leer un audio con `fopen`, automáticamente reintenta buscar en la ruta alterna `/Data_960_576/` antes de cancelar la lectura.

#### 5. Extracción Física de Recursos OBB
- **Problema:** SoLoud (al ser un motor nativo C++) no podía cargar los archivos empaquetados dentro de los contenedores `.obb` o `.apk` usando los lectores ZIP de Cocos2d-x.
- **Solución:** Identificamos que la carpeta `Data` debía ser copiada físicamente sin empaquetar a la ruta `ux0:data/popclassic/` en la memoria de la consola para que SoLoud pudiera leerlos mediante descriptores estándar (`fopen`).

#### 6. Crash por Conflicto de Hilos de Audio (Use-After-Free)
- **Problema:** El juego lanzaba un Core Dump (Crash total del sistema - SegFault) repentino.
- **Solución:** Se detectó un problema de "hilos cruzados" (Cross-thread / Use-after-free). Cuando el juego invocaba la función de limpieza `Cocos2dxSound_unloadEffect`, liberaba y destruía la memoria del efecto de sonido en el hilo principal mientras el "Hilo de Audio de SoLoud" (en segundo plano) aún intentaba leer la memoria para reproducirlo. Se corrigió añadiendo `gSoloud.stopAudioSource(...)` de forma obligatoria antes de liberar la memoria de cada puntero `SoLoud::Wav`, garantizando seguridad entre hilos.

#### 7. Análisis de Cinemáticas (Video MP4)
- **Problema:** El usuario reportó no encontrar videos `.mp4` en el directorio principal `/Data/`.
- **Solución:** Al desempacar el `original.apk.full`, se documentó que los verdaderos archivos de video (ej. `PoP_V1_1.mp4`) estaban ocultos dentro de la carpeta `assets/Extra/Video/High/` en el `.apk`. Debido a la falta de codecs nativos de Android, esta función (`Cocos2dxActivity_playVideo`) fue inhabilitada y marcada para una futura implementación usando `SceAvPlayer`.

---

### 🇬🇧 English

#### 1. Control Mapping and Movement (D-Pad and Left Analog)
- **Issue:** The character couldn't walk or run properly using the D-Pad or left analog stick; they would only turn around.
- **Fix:** PS Vita physical input capture was reconfigured to dispatch virtual touch events (synthesized touches). Using a reference screenshot, the exact touchscreen coordinates corresponding to the game's virtual interface buttons (walk and run arrows) were mapped.

#### 2. Native Audio Engine Implementation (SoLoud C++)
- **Issue:** The game emitted no music or sound effects, as the original Android Cocos2dx engine could not decode the audio on the PS Vita.
- **Fix:** The **SoLoud** native C++ engine was integrated. Java JNI calls (Cocos2dxMusic and Cocos2dxSound) were intercepted and replaced with SoLoud API calls. Background Music (BGM) was implemented via `WavStream` to prevent RAM saturation, while Sound Effects (SFX) used `Wav` paired with a memory cache map for ultra-fast access.

#### 3. Infinite Jump Bug and Broken Logic (Dummy Audio Handles)
- **Issue:** When the game failed to find a sound file, the API returned an identifier (`handle`) of `0`. This caused the Cocos2d-x internal engine to enter an error loop, triggering an "Infinite Jump Bug".
- **Fix:** An incremental `Dummy Handles` system (900000X) was implemented. If loading fails, the game receives a valid numeric handle, preventing the engine from looping/crashing, gracefully failing with silence without breaking gameplay states.

#### 4. Audio Path Redirection and Fallback Folder
- **Issue:** The Java game code requested `.mp3` files from an `/Extra/` folder. However, the unpacked assets were `.ogg` files located in `/Data/`. Furthermore, the engine sometimes requested specific resolution assets (e.g., `Data_960_576`).
- **Fix:** A path translator (`sanitize_audio_path`) was added to capture false requests and redirect them to their `.ogg` equivalents in `/Data/`. Additionally, a "fallback" system was introduced: if `fopen` fails to read the audio file, it automatically retries looking in the alternate `/Data_960_576/` path before giving up.

#### 5. Physical Extraction of OBB Resources
- **Issue:** SoLoud (being a native C++ engine) could not load packed files from inside `.obb` or `.apk` containers using Cocos2d-x's ZIP readers.
- **Fix:** We identified that the `Data` folder had to be physically copied unpacked into the console's memory at `ux0:data/popclassic/` so that SoLoud could read them using standard file descriptors (`fopen`).

#### 6. Audio Thread Conflict Crash (Use-After-Free)
- **Issue:** The game suffered from sudden Core Dumps (System Crash / SegFault).
- **Fix:** A cross-thread (Use-after-free) issue was detected. When the game invoked the cleanup function `Cocos2dxSound_unloadEffect`, it freed and destroyed the sound effect's memory on the main thread while the SoLoud Background Audio Thread was still attempting to read it for playback. This was fixed by enforcing a mandatory `gSoloud.stopAudioSource(...)` call before freeing each `SoLoud::Wav` pointer's memory, ensuring thread safety.

#### 7. Cinematics Analysis (MP4 Video)
- **Issue:** The user reported being unable to find `.mp4` videos in the main `/Data/` directory.
- **Fix:** By unpacking `original.apk.full`, we documented that the real video files (e.g., `PoP_V1_1.mp4`) were hidden inside the `assets/Extra/Video/High/` folder within the `.apk`. Due to the lack of native Android codecs on PS Vita, this function (`Cocos2dxActivity_playVideo`) was stubbed and marked for future implementation via `SceAvPlayer`.
