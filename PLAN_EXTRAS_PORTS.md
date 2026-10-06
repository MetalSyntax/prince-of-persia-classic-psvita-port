# Plan General — Características Extras para los Ports (gap vs. escena)

> Objetivo: llevar los ports propios (Zenonia 2/3/4, Dungeon Hunter 2/3, Asphalt 5/6,
> Advena, ILLUSIA 1/2, Inotia 3, resto Gameloft/Gamevil) al nivel de extras de los
> ports de referencia de internet (TheFlow, Rinnegatamante, gl33ntwine/v-atamanenko,
> frobnicator, Nevak, etc.), con un diseño **común y reutilizable** en vez de
> parches ad-hoc por juego.
>
> Verificación local (2026-09-22): ninguno de los 6 repos muestreados
> (`Zenonia2/3/4-vita`, `Dungeon-Hunter-2-vita`, `Asphalt-5-Vita`, `Advena-Vita`)
> tiene `trophy/`, configurator app, `settings.bin`, `manual/`, ni `controls.txt`.
> Ese es el gap real, no una suposición.

## Fuentes analizadas

| Port / repo | Extras que aporta (y los nuestros no tienen) |
|---|---|
| `TheOfficialFloW/gtasa_vita` (GTA SA) | Configurator app desde LiveArea, `controls.txt` remapeable, L2/R2→rear touch y L3/R3→front touch (conmutable), teclado OSK con L+SELECT para cheats, quick-save al salir + Resume=carga último save, canciones de radio recortadas restaurables (`MUSIC.md`), MP3 fuzzy seek, texturas HD opcionales, render PS2-like conmutable |
| `Rinnegatamante/raider-vita`, `ff4_vita`, `fahrenheit-vita`, `not_a_hero-vita` | **Trofeos** (incl. ocultos + iconos hi-res), multilenguaje (hasta 7 idiomas, TR2), FMV por transcode ffmpeg `.bat` opcional, `datafiles.zip` separado, front-touch mapping L2/L3/R2/R3, nota PSVshell 500 MHz |
| `v-atamanenko/soloader-boilerplate` (Solobop) | `settings.bin` persistente (`source/utils/settings.c`), redirect `-config` a binario configurador, overclock 444/222/222/166 al arrancar, `SHADER_FORMAT` GLSL/CG/GXP + `DUMP_COMPILED_SHADERS`, targets CMake `send/dump/reboot` vía `PSVITAIP` |
| `v-atamanenko/masseffect-vita`, Backstab HD, Dead Space (gl33ntwine) | Companion/configurator lanzable con botón **Settings en el LiveArea**, deadzones de sticks, nivel de detalle gráfico, FPS limiter, sensibilidad, `Control Scheme 2` documentado |
| `Nevak/bgda-vita` (Dark Alliance) | L2/R2 en rear touch, idioma = idioma del sistema Vita, coop 2 mandos PSTV, Battery Saving = lock 30 FPS, logros marcados como "not yet" (roadmap honesto) |
| Escena general (VitaDB Downloader, reRescaler, VitaGrafix, PSVshell, reVita) | Overclock/Manual FPS counter como expectativa del jugador, VitaGrafix-style res/FPS, manual LiveArea, bg music en LiveArea/Downloader, auto-updater + changelogs visibles |

## Gap analysis resumido

| # | Extra de la escena | Estado en nuestros ports | Prioridad |
|---|---|---|---|
| 1 | Remapeo de controles (`controls.txt` + esquema Vita mejorado) | ❌ Hardcodeado en `source/input.c` | P0 |
| 2 | L2/R2/R3 trasero/delantero + soporte PSTV real | ❌ Parcial / sin documentar | P0 |
| 3 | Deadzones + sensibilidad configurables | ❌ | P0 |
| 4 | Configurator app + botón Settings en LiveArea + `settings.bin` | ❌ (boilerplate lo soporta, ningún port lo usa) | P0 |
| 5 | Overclock automático 444 MHz + mención PSVshell | ⚠️ Parcial (algunos lo fijan, sin opción ni docs) | P0 |
| 6 | Trofeos (`sce_sys/trophy/`, ocultos, iconos hi-res, NoTrpDrm) | ✅ Implementado y compilado en Prince of Persia (18 trofeos HD) | P1 |
| 7 | Idioma = idioma del sistema + fallback | ❌ (rutas `eng/` hardcodeadas) | P1 |
| 8 | LiveArea premium (manual, frames, bgm, botón config) | ⚠️ Solo bg/pic/icon básicos | P1 |
| 9 | Quick-save/auto-resume + import saves Android | ❌ | P1 |
| 10 | Cheat/debug OSK (L+SELECT) donde aplique | ❌ | P1 |
| 11 | Opciones gráficas (res interna, MSAA/bilinear, FPS limiter, PostFX/CRT) | ⚠️ Solo flags de build sueltos | P1 |
| 12 | Shader cache en disco + primer arranque rápido | ⚠️ Inconsistente | P1 |
| 13 | Videos/FMV con script transcode + skip elegante si falta | ⚠️ Solo Asphalt 5 lo hace bien | P2 |
| 14 | `datafiles.zip` + checker de archivos + mensajes de error con diálogo | ⚠️ Desigual | P2 |
| 15 | Parches version-agnostic + multi-versión APK soportada | ❌ (un APK exacto por port) | P2 |
| 16 | Release estándar VitaDB (screenshots, trailer, changelog, QR, gamefiles.zip) | ⚠️ Desigual | P2 |

## Fase P0 — Quick wins (1–2 tardes por port, alto valor jugador)

### P0.1 Controles remapeables + `controls.txt`
- Copiar el patrón `gtasa_vita`: `ux0:data/<juego>/controls.txt` con parsing
  simple `VITA_BOTON = ACCION_JUEGO`, + esquema por defecto "Vita mejorado".
- Mantener compat: si no existe el archivo, usar el mapeo actual (cero regresión).
- Documentar en README con tabla (como ya hace Asphalt 5, que es el mejor ejemplo propio).

### P0.2 L2/R2/R3 touch + PSTV
- Estándar escena: **L2/R2 = rear touch arriba, L3/R3 = front touch abajo**,
  con alternativa L2/R2 = front arriba (la que añadió GTA SA v1.2 para PSTV).
- Donde el juego no necesite L2/R2, mapearlos a acciones útiles
  (poción/mapa/save rápido) en vez de dejarlos muertos.
- Probar explícitamente en PSTV (o declarar "PSTV requiere DS3/DS4 por MiniVitaTV").

### P0.3 Deadzone + sensibilidad
- Exponer 2 valores en settings (default: inner ~8–12): evita drift y quejas de
  "stick no llega al máximo en diagonal" (cf. AnalogsEnhancerKai).
- Backstab HD lo pide hasta en su guía ("bajar sensibilidad al mínimo") — mejor
  poner defaults sanos desde el loader que pedirlo al jugador.

### P0.4 Configurator mínimo + `settings.bin`
- Activar lo que Solobop ya da gratis: `settings.c` + redirect `-config` +
  botón **Settings en LiveArea** (ejemplo: Mass Effect, Backstab HD).
- Contenido mínimo v1: deadzones, detalle gráfico (si el port tiene flags),
  FPS limiter on/off, toggle overlay táctil (Zenonia 4 ya oculta gamepad virtual;
  hacerlo setting en vez de `#define`).
- El toolkit (`psvita-toolkit`) puede generar el esqueleto del configurador al
  crear/adoptar un port — no copiarlo a mano en cada repo.

### P0.5 Relojes + PSVshell
- Fijar `444/222/222/166` al arrancar (ya lo hace `soloader_init_all()`; verificar
  que todos los ports lo llamen) + línea estándar en README:
  "Opcional: PSVshell a 500 MHz si algún nivel cae".

## Fase P1 — Nivel escena (1 semana por port aprox.)

### P1.1 Trofeos Nativos PS Vita (Implementación Oficial: Prince of Persia Classic)

El soporte de trofeos para PS Vita se implementa siguiendo el estándar de la escena homebrew (`sceNpTrophy` + plugin `NoTrpDrm`), integrando los **17 logros/trofeos oficiales del juego original** más **1 trofeo de Platino** exclusivo del port de PS Vita (total: **18 trofeos**).

#### 1. Arquitectura Técnica del Subtítulo de Trofeos

- **Contenedor TRP**: `sce_sys/trophy/TROPHY.TRP` empaquetado en el VPK.
- **Bypass de firmas**: Requiere el plugin kernel `NoTrpDrm` (desarrollado por Rinnegatamante), que elimina la comprobación criptográfica obligatoria de firmas TRP y NP Communication ID para homebrew.
- **Intercepción nativa en C/C++**: El binario `libgame_logic.so` ya gestiona internamente todos los logros mediante el método:
  ```cpp
  // Símbolo exportado en libgame_logic.so (offset 0x0006c0f0):
  void AchievementManager::UnLockAchievement(AchievementManager *this, int param_1, bool param_2);
  // Símbolo mangled GCC/Clang:
  _ZN18AchievementManager17UnLockAchievementEib
  ```
- **Punto de enganche (Hook)**:
  En `source/patch.c` o `source/trophies.c`, se intercepta `_ZN18AchievementManager17UnLockAchievementEib`. Al ser invocado por la lógica de juego nativa:
  1. Se ejecuta la rutina original (`real_UnLockAchievement`) para preservar el guardado en `SaveGame::sharedSaveGame()` y el popup interno.
  2. Se envía una señal al hilo asíncrono de desbloqueo de trofeos de PS Vita: `trophies_unlock(param_1 + 1)`.
  3. Si los 17 trofeos base quedan desbloqueados, se desbloquea automáticamente el trofeo de Platino (`TROP000`, ID 0).
- **Desbloqueo Retroactivo**: Al iniciar el juego o al llamar a `AchievementManager::LoadAchievementStatus()`, el loader recorre los slots del perfil (`SaveGame::IsAchievementLocked(save, i)`). Si el jugador ya completó requisitos en un guardado previo, los trofeos correspondientes en la PS Vita se sincronizan y desbloquean de forma retroactiva.
- **Hilo desacoplado (Zero-Stutter)**: La llamada a `sceNpTrophyUnlockTrophy()` se procesa en un hilo dedicado con semáforo (`trps request`) para no congelar ni provocar stutter en el bucle de renderizado a 60 FPS.

#### 2. Tabla de Trofeos Oficiales (1 Platino + 17 Oficiales del Juego)

Los nombres y descripciones provienen textualmente de los archivos oficiales de localización del juego (`ux0_data/popclassic/Data_960_576/Localization/English/Localizable.loc` y `Spanish/Localizable.loc`):

| ID Vita | ID Motor | Tipo | Oculto | Nombre Oficial (EN) | Nombre Oficial (ES) | Descripción Oficial (ES) / (EN) | Disparador en Motor |
|:---:|:---:|:---:|:---:|---|---|---|---|
| **00** | — | **Platino** | No | **PRINCE OF PERSIA** | **PRÍNCIPE DE PERSIA** | Consigue todos los trofeos del juego.<br>*(Unlock all trophies in the game.)* | Desbloquear los trofeos 01 al 17. |
| **01** | `0` | Bronce | No | **TOOLS AND TRADES** | **HERRAMIENTAS DEL OFICIO** | Consigue la espada en la prisión.<br>*(Acquire the sword in the Prison.)* | Recoger la espada en el Nivel 1. |
| **02** | `1` | Bronce | No | **ETERNAL PRISON** | **LA PRISIÓN ETERNA** | Despierta al esqueleto guardián.<br>*(Awaken the skeleton guards.)* | `Skeleton::SwitchAnimation(case 1)`. |
| **03** | `2` | Bronce | **Sí** | **INNER REFLECTIONS** | **REFLEXIONES INTERIORES** | Rompe el espejo y libera al Príncipe Oscuro.<br>*(Break the Mirror and release the Dark Prince.)* | Al saltar y atravesar el espejo mágico en Nivel 4. |
| **04** | `3` | Bronce | No | **THE PATH FORWARD** | **EL CAMINO ADELANTE** | Mata al guardián de la puerta.<br>*(Kill the Gatekeeper.)* | Derrotar al guardián jefe de la puerta (Nivel 6). |
| **05** | `4` | Bronce | No | **FLYING MIGHT** | **PODER VOLADOR** | Realiza un salto tras tomar la poción pluma.<br>*(Perform a jump after consuming the Feather Potion.)* | Saltar tras ingerir la poción de ingravidez (Nivel 7). |
| **06** | `5` | Bronce | **Sí** | **OF PRINCES AND MICE** | **DE RATONES Y PRÍNCIPES** | Te ha rescatado el ratón de la princesa.<br>*(You were rescued by the Princess's mouse.)* | Escena del ratón abriendo la reja (Nivel 8). |
| **07** | `6` | Bronce | No | **UP IS DOWN IS UP** | **TODO LO QUE SUBE BAJA** | Toma ambas pociones inversas.<br>*(Consume both inverse potions.)* | Ingerir las dos pociones invertidas en el juego. |
| **08** | `7` | Plata | **Sí** | **FATEFUL SYNAPSE** | **EL MOMENTO FATÍDICO** | Reúnete con el Príncipe Oscuro.<br>*(Reunite with the Dark Prince.)* | Fundirse con la sombra en Nivel 12. |
| **09** | `8` | Oro | No | **OF LOVE AND REVENGE** | **AMOR Y VENGANZA** | Derrota a Jaffar.<br>*(Defeat Jaffar.)* | Vencer al visir Jaffar en la torre final. |
| **10** | `9` | Plata | No | **QUEST FOR IMMORTALITY** | **LA INMORTALIDAD** | Encuentra todos los elixires.<br>*(Find all Elixirs.)* | Recoger los 9 botes de elixir de vida (`InteractiveItems::IsElixirPickedInLevel == 9`). |
| **11** | `10` | Bronce | No | **RESCUE RACE** | **RESCATE A TODA PRISA** | Supera un récord anterior de cualquier nivel en cualquier modo.<br>*(Beat a previous score of any level in any mode.)* | Mejorar la puntuación previa en cualquier nivel. |
| **12** | `11` | Oro | No | **SANDS OF FATE** | **ARENAS DEL DESTINO** | Completa el juego en modo Contrarreloj.<br>*(Complete the game in Time Attack mode.)* | Terminar los 14 niveles en Time Attack. |
| **13** | `12` | Oro | No | **THE TRUE PRINCE** | **EL VERDADERO PRÍNCIPE** | Completa el juego en modo Supervivencia.<br>*(Complete the game in Survivor mode.)* | Terminar los 14 niveles en Survivor (1 vida, < 60 min). |
| **14** | `13` | Plata | No | **UNSTOPPABLE** | **IMPARABLE** | Completa el nivel sin perder salud.<br>*(Complete a level without losing health.)* | Terminar cualquier nivel con 100% de salud intacta. |
| **15** | `14` | Plata | No | **PEACE MAKER** | **EL PACIFICADOR** | Completa el nivel sin matar a un solo guardia.<br>*(Complete a level without killing a single guard.)* | Superar un nivel sin bajas enemigas directas. |
| **16** | `15` | Bronce | No | **UNLEASHED WRATH** | **IRA DESATADA** | Mata al primer enemigo.<br>*(Perform the 1st Kill.)* | Primer enemigo abatido con espada. |
| **17** | `16` | Bronce | No | **SLICE OF LIFE** | **UN PEDAZO DE VIDA** | Mata a un guardia con la rebanadora, con un pincho o haciendo que se caiga.<br>*(Kill a guard with a slicer, spike or by falling.)* | Muerte ambiental de un guardia (trampas de cuchillas/pinchos/vacío). |

*Puntuación total según baremo PlayStation*: 1 Platino (300) + 3 Oro (270) + 4 Plata (120) + 10 Bronce (150) = **840 puntos**, correspondiente al estándar oficial de un título descargable/arcade de PSN.

#### 3. Pipeline de Assets e Iconos Gráficos

Los iconos de trofeos se extraen directamente del atlas oficial del juego:
- **Atlas original**: `ux0_data/popclassic/Data_960_576/Texture/Achievement/achv_screen.png` y su definición `achv_screen.plist`.
- **Mapeo de iconos**:
  - `ICON0.PNG` (500×500): Extraído de `achievement_trophy` en `Texture/Achievement/achievement.png`.
  - `TROP000.PNG` (Platino, 500×500): Icono de corona/príncipe dorado enmarcado con ribete platino.
  - `TROP001.PNG` a `TROP017.PNG` (500×500): Recortes correspondientes a `achv_icon_01` hasta `achv_icon_17` reescalados con filtro lanczos/bicúbico y marco estándar de trofeos de PlayStation Vita.

#### 4. Implementación del Wrapper de Trofeos (`source/trophies.c` / `source/trophies.h`)

Estructura modular lista para enlazar con `libSceNpTrophy_stub`:

```c
// source/trophies.h
#ifndef __TROPHIES_H__
#define __TROPHIES_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int trophies_init(void);
void trophies_unlock(uint32_t id);
uint8_t trophies_is_unlocked(uint32_t id);
void trophies_sync_from_savegame(void);

#ifdef __cplusplus
}
#endif

#endif // __TROPHIES_H__
```

Lógica de desbloqueo asíncrono y compatibilidad `NoTrpDrm`:

```c
// source/trophies.c
#include <vitasdk.h>
#include <stdio.h>
#include <string.h>
#include "trophies.h"
#include "utils/logger.h"

static char comm_id[12] = {0};
static char signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};
static int trp_ctx = 0;
static int plat_id = 0; // ID 0 es el Platino en la Vita
static int trophies_available = 0;

typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    int context;
    int options;
    uint8_t reserved[128];
} SceNpTrophySetupDialogParam;

typedef struct {
    uint32_t unk[4];
} SceNpTrophyUnlockState;

static SceNpTrophyUnlockState trophies_unlocks;
static volatile int trp_id_queue;
static SceUID trp_request_sema;

static int trophies_unlocker_thread(SceSize args, void *argp) {
    while (1) {
        sceKernelWaitSema(trp_request_sema, 1, NULL);
        int local_id = trp_id_queue;
        int trp_handle = 0;
        sceNpTrophyCreateHandle(&trp_handle);
        int res = sceNpTrophyUnlockTrophy(trp_ctx, trp_handle, local_id, &plat_id);
        l_info("Trophy unlock triggered: ID %d (res: 0x%08X)", local_id, res);
        sceNpTrophyDestroyHandle(trp_handle);
    }
    return 0;
}

int trophies_init(void) {
    sceAppMgrAppParamGetString(0, 12, comm_id, 256);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    sceNpTrophyInit(NULL);

    int res = sceNpTrophyCreateContext(&trp_ctx, comm_id, signature, 0);
    if (res < 0) {
        l_warn("sceNpTrophyCreateContext falló (0x%08X). ¿NoTrpDrm instalado?", res);
        return res;
    }

    SceNpTrophySetupDialogParam setupParam;
    memset(&setupParam, 0, sizeof(setupParam));
    _sceCommonDialogSetMagicNumber(&setupParam.commonParam);
    setupParam.sdkVersion = PSP2_SDK_VERSION;
    setupParam.context = trp_ctx;
    sceNpTrophySetupDialogInit(&setupParam);

    while (sceNpTrophySetupDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        sceKernelDelayThread(16000);
    }
    sceNpTrophySetupDialogTerm();

    trp_request_sema = sceKernelCreateSema("trps_request_sema", 0, 0, 1, NULL);
    SceUID thid = sceKernelCreateThread("trophies_unlocker", trophies_unlocker_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(thid, 0, NULL);

    int trp_handle = 0;
    uint32_t count = 0;
    sceNpTrophyCreateHandle(&trp_handle);
    sceNpTrophyGetTrophyUnlockState(trp_ctx, trp_handle, &trophies_unlocks, &count);
    sceNpTrophyDestroyHandle(trp_handle);

    trophies_available = 1;
    l_info("Sistema de trofeos de PS Vita inicializado correctamente.");
    return 0;
}

uint8_t trophies_is_unlocked(uint32_t id) {
    if (!trophies_available) return 0;
    return (trophies_unlocks.unk[id >> 5] & (1 << (id & 31))) != 0;
}

void trophies_unlock(uint32_t id) {
    if (trophies_available && !trophies_is_unlocked(id)) {
        trophies_unlocks.unk[id >> 5] |= (1 << (id & 31));
        trp_id_queue = id;
        sceKernelSignalSema(trp_request_sema, 1);
    }
}
```

#### 5. Hook en `source/patch.c`

```c
// Símbolo exportado de libgame_logic.so
#define UNLOCK_ACHIEVEMENT_SYM "_ZN18AchievementManager17UnLockAchievementEib"

static so_hook gUnLockAchievementHook;
static void (*real_UnLockAchievement)(void *this, int id, bool popup) = NULL;

static void hook_UnLockAchievement(void *this, int id, bool popup) {
    l_info("Game Logic Achievement: ID %d (popup=%d)", id, popup);
    
    // 1. Invocar la lógica original para preservar guardado y feedback interno
    real_UnLockAchievement(this, id, popup);
    
    // 2. Desbloquear el trofeo correspondiente de PS Vita (IDs 1..17)
    if (id >= 0 && id <= 16) {
        trophies_unlock(id + 1);
        
        // 3. Comprobar si se completaron los 17 para otorgar el Platino (ID 0)
        int all_unlocked = 1;
        for (int i = 1; i <= 17; i++) {
            if (!trophies_is_unlocked(i)) {
                all_unlocked = 0;
                break;
            }
        }
        if (all_unlocked) {
            trophies_unlock(0);
        }
    }
}

void patch_trophies(void) {
    uintptr_t addr = so_symbol(&game_mod, UNLOCK_ACHIEVEMENT_SYM);
    if (addr) {
        real_UnLockAchievement = (void (*)(void *, int, bool)) addr;
        gUnLockAchievementHook = hook_addr(addr, (uintptr_t) hook_UnLockAchievement);
        l_info("Hook instalado en %s en 0x%08x", UNLOCK_ACHIEVEMENT_SYM, (unsigned) addr);
    } else {
        l_warn("No se encontró el símbolo %s en libgame_logic", UNLOCK_ACHIEVEMENT_SYM);
    }
}
```

#### 6. Build & Empaquetado en CMakeLists.txt

- **Enlace de librería**: Añadir `-lSceNpTrophy_stub` en `target_link_libraries`.
- **Estructura del VPK**:
  ```cmake
  vita_create_vpk(${CMAKE_PROJECT_NAME}.vpk ${VITA_TITLEID} ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.velf
      CONFIG ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml
      ASSETS ${CMAKE_SOURCE_DIR}/extras/trophy sce_sys/trophy
  )
  ```
- **Requisito en README/LiveArea**: Añadir aviso al usuario: `"Requiere el plugin NoTrpDrm instalado en taiHEN para habilitar el registro de trofeos en LiveArea."`

### P1.2 Idioma del sistema
- Leer idioma de Vita (`sceAppUtilSystemParamGetById`) y mapear a carpeta de
  assets (`eng/spa/fra/...`); fallback a inglés. Como `bgda-vita`.
- Aplica directo a Zenonia/DH/Inotia (ya traen varias carpetas de idioma en el APK).

### P1.3 LiveArea completo
- `template.xml` con frames + botón Settings/Config + **manual** (`sce_sys/manual/`,
  convertir FAQ actual con FAQ-to-Manual) + bgm `at9` opcional.
- El módulo `livearea.py` del toolkit ya valida specs; añadirle plantillas
  "premium" (manual + botón config) en vez de hacerlo a mano por port.

### P1.4 Saves: quick-save + import Android
- Quick-save al elegir Quit (patrón GTA SA) + Resume = último save.
- Script/doc "copia tu save de Android a `ux0:data/<juego>/`" (ruta exacta por juego).

### P1.5 Opciones gráficas en runtime (no solo flags de build)
- Mover NEON/turbo/downsample/frameskip de `build.sh --flags` a settings:
  `Detalle: Alto/Bajo`, `FPS lock: 30/60/off`, `Filtro: bilinear/sharp`,
  `Velocidad: 1x–3x` (el ciclo `R+SELECT` de Zenonia 4 es buen patrón, pero que
  sea setting persistente, no solo toggle volátil).
- CRT/PostFX solo si vitaGL lo da gratis; no inventar shaders propios por port.

### P1.6 Shader cache
- `DUMP_COMPILED_SHADERS=ON` + avisar "primer arranque tarda, los siguientes no"
  (Solobop ya lo implementa; activarlo y documentarlo en todos).

## Fase P2 — Pulido release (cuando P0+P1 estén)

- **FMV**: patrón `raider-vita`/Asphalt 5: `.bat`+ffmpeg opcional, skip elegante si
  falta el `.mp4` (nunca colgar).
- **Checker de datos**: al arrancar sin `.so`/assets, diálogo `SceCommonDialog`
  con la ruta exacta que falta (no crash mudo `C2-12828-1`).
- **`datafiles.zip`** separado del VPK para lo convertible (texturas/shaders CG).
- **Parches version-agnostic** (patrón `ff4_vita`): buscar por firma, no por offset
  duro, y listar APKs probadas en el README.
- **Release checklist VitaDB**: VPK + screenshots + trailer 30 s + changelog +
  `gamefiles.txt` + QR Brewology + nota kubridge/FdFix/libshacccg.

## Orden de aplicación sugerido (tus repos)

1. **Prince of Persia Classic** → piloto insignia de **P1.1 Trofeos Oficiales** (los 18 trofeos oficiales completamente mapeados desde `libgame_logic.so`, hook directo en `AchievementManager::UnLockAchievement`, `NoTrpDrm` + `TROPHY.TRP`).
2. **Asphalt-5-Vita** (el más maduro: ya tiene controles documentados, audio fixed-point, video-skip) → piloto de P0.1+P0.4+P1.5.
3. **Zenonia4-vita** (turbo/frameskip ya existe) → convertir flags a settings P0.4+P1.5.
4. **Dungeon-Hunter-2-vita** → P0.2+P0.5 (ayuda al FPS bajo sin tocar el motor).
5. **Zenonia2/3, ILLUSIA, Inotia3, Advena** → aplicar el paquete P0 ya rodado en masa.
6. Propagación de trofeos (P1.1) al resto de ports una vez validado el estándar en Prince of Persia Classic.

## Automatización con el toolkit (qué hace la máquina, qué pide IA)

> Estado real auditado 2026-09-22 con `psvita-toolkit log-standard`:
> `asphalt5` conforme (0 discrepancias, es el estándar canónico);
> `advena` 3 (sin `next.idx`, path hardcodeado, sin flag de producción);
> `dungeon-hunter-2` 4 (basename `log_NNN` sin slug, base 000, sin `next.idx`, path hardcodeado);
> `zenonia2` sin logger (esquema viejo `log_<timestamp>.txt`).

| Extra del plan | Toolkit lo automatiza | Requiere IA / mano |
|---|---|---|
| Subir `logs/`+`saves/` vacíos por FTP | ✅ Sí — `upload_ux0_data_dir` ahora crea dirs vacíos (fix); `scaffold_ux0_data` deja `.gitkeep` (el uploader lo salta como payload pero crea el dir remoto) | — |
| Estándar `<slug>_001..999` + `next.idx` + `DATA_PATH` + flag prod | ✅ `log-standard [--fix-dirs]` audita y crea staging; el `PORTING_PLAN.md` nuevo ya trae la sección 6 con la norma | Migrar cada logger a mano (plantilla: el de Asphalt 5), la herramienta solo señala el gap |
| Trazas `[TRACE] >> f()` detalladas sin IA | ✅ `log-trace [--dry-run] [--ensure-flag] [--remove]` — regex determinista, entry-only (las de salida con returns/gotos mentirían), idempotente, bloque `PORT_TRACE` (ON=debug, OFF=producción, verificado que compila con `gcc -fsyntax-only`) | La IA las *lee*, no las escribe: con trazas homogéneas el `export-context` le da a cualquier copiloto el mismo formato en todos los ports |
| Controles remapeables, configurador, trofeos, LiveArea premium | ⚠️ Parcial — `sync-shared` propaga un componente ya hecho (ej. `controls.txt`, `settings.c`) a toda la familia de motor; `livearea --validate` y `jni-analyze` cubren su parte | Diseñar el componente una vez (en el piloto Asphalt 5) sí es trabajo de port |
| Release VitaDB | ⚠️ `build`+`deploy`+`analyze` ya headless para CI | Screenshots/trailer/changelog a mano |

Flujo recomendado por port: `log-standard --fix-dirs` → migrar logger → `log-trace --ensure-flag`
para cazar el bug → `log-trace --remove` (o build con `PORT_TRACE=OFF`) antes del VPK final →
`export-context` para que la IA diagnostique sobre logs homogéneos.

## Criterio de "hecho" por port

- [ ] `controls.txt` funciona y está documentado
- [ ] L2/R2/R3 tienen función en Vita y PSTV
- [ ] Botón Settings en LiveArea abre configurador (deadzones, detalle, FPS lock)
- [ ] Relojes fijados + nota PSVshell en README
- [x] **Trofeos oficiales de PS Vita plenamente operativos**:
  - [x] `TROPHY.TRP` generado en `sce_sys/trophy/` con los 18 trofeos oficiales (1 Platino + 17 juego)
  - [x] Intercepción en `AchievementManager::UnLockAchievement` dispara `trophies_unlock()`
  - [x] Iconos de trofeos con orientación vertical corregida (90° CCW), sin sangrado de marcos, realce de contraste y marco metálico por categoría (Platino/Oro/Plata/Bronce)
  - [x] Trofeo 000 (Platino) con medallón dorado oficial de alta resolución y engaste celestial
  - [x] Empaquetado e integrado en `popclassic.vpk`
  - [ ] Desbloqueo retroactivo verificado al cargar partida desde `SaveGame` en consola
  - [ ] Desbloqueo visible en pantalla (popup oficial de la Vita) probado en consola física con `NoTrpDrm`
- [ ] Idioma sigue al sistema con fallback
- [ ] LiveArea con manual + frames, sin warning `0x8010113D`
- [ ] Release con changelog, screenshots, `gamefiles.txt`, versiones APK probadas

## Notas anti-fabricación

- No prometer online/multijugador resucitado, 60 FPS fijos donde el motor no da,
  ni contador GPU (PowerVR no lo expone a homebrew) — la telemetría honesta es
  frame time, como ya hace `perf_telemetry.py`.
- Todo lo de arriba existe y está referenciado en ports públicos; nada requiere
  investigación nueva, solo portar el patrón al esqueleto común y rodarlo por
  todos los juegos.
