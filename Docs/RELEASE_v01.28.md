<h1 align="center">Prince of Persia Classic · PS Vita — v01.28</h1>

<p align="center">
  <b>popclassic.vpk</b> · TITLEID <code>POPC00001</code> · previous release: <a href="#">v01.20</a>
</p>

<p align="center">
  <a href="#-español">🇪🇸 Español</a> •
  <a href="#-english">🇬🇧 English</a>
</p>

> ⚠️ **This release has not been confirmed on real hardware yet.** It builds clean and every change is
> backed by disassembly of the real game libraries, but the control rework (v01.27–v01.28) has not been
> played on a console. If something is wrong, **v01.27** is the safe fallback build. Bug reports very
> welcome — see [Beta Testing](../README.md#beta-testing--contributing).

---

## 🇪🇸 Español

### Lo importante en una línea

**Los botones físicos ahora son controles de verdad.** Este es el port de la build de **Xperia PLAY** del
juego, que trae una ruta completa de gamepad físico… que este port nunca había encendido. Ahora sí. Los
botones táctiles en pantalla desaparecen solos, el movimiento no depende de simular toques en coordenadas
adivinadas, y **se puede saltar en corrida**.

### Controles nuevos

| Botón | En plataformas | En combate |
|:---:|:---|:---|
| **Stick izq. / D-Pad** | Mover — toque corto = un paso medido, **mantenido = correr** | igual |
| **Cruz** | **Saltar** | Atacar |
| **Cuadrado** | Agacharse | **Defender / parar** |
| **Triángulo** | Interactuar (agarrar, palancas) | Envainar la espada |
| **Círculo / Abajo** | Agacharse | — |
| **Arriba** | Saltar / trepar | — |
| **Start** | Menú de pausa | Menú de pausa |
| **Select** | Menú | Menú |
| **L + R juntos** | Mostrar/ocultar los botones táctiles en pantalla | igual |
| **Pantalla táctil** | Sigue funcionando para navegar menús | — |

> 🔔 **Dos cambios que hay que saber si venías de v01.20:**
>
> 1. **Cuadrado ya no es el modificador de "andar"** — ahora es agacharse / defender. Andar ya no necesita
>    modificador: es toque corto para un paso, mantenido para correr, igual que se comportaban las flechas
>    táctiles.
> 2. **El D-Pad ya corre.** Era una limitación conocida desde el principio ("usá el stick analógico hasta
>    que se arregle"); ya no aplica, el D-Pad y el stick hacen exactamente lo mismo.

### Qué cambió desde v01.20

#### Controles (el grueso de esta release)

- **Ruta nativa de gamepad activada** (v01.28). El 3er argumento de `nativeSetPaths` es un nombre de
  dispositivo, y el motor lo usa para exactamente una cosa: si es `"R800i"` (el modelo del Xperia PLAY),
  activa su modo de gamepad físico. El port mandaba `"PSVita"` desde su primer commit, así que toda la API
  de teclado del juego estaba inalcanzable. Efectos: los controles en pantalla se ocultan con el código del
  propio juego (sin trucos), los menús ganan cursor navegable con el D-Pad, y los tutoriales describen
  botones físicos en vez de botones táctiles.
- **Se puede saltar en corrida** (v01.28). Era el bug reportado que sobrevivió a cuatro intentos de arreglo.
  Nunca fue un bloqueo del motor: la emulación de toques sintéticos no podía expresar "correr" y "saltar" al
  mismo tiempo porque competían por ranuras de toque finitas. Por keycodes reales, correr y saltar son dos
  bits distintos de la misma máscara y coexisten sin más.
- **Caminar vs. correr, con la regla real del juego** (v01.28). Se replica el mismo umbral de 250 ms que
  usan las flechas táctiles, en vez de las tres mecánicas inventadas que se probaron antes (distancia al
  centro, doble toque, simulación de arrastre — ninguna funcionó).
- **Cruz vuelve a saltar** (v01.27). v01.26 había concluido, con las funciones invertidas, que el salto era
  `control1Clicked` — que en realidad es **agacharse**. El keycode 23 que el port ya mandaba desde siempre
  era el evento de salto correcto todo este tiempo.
- **Cruz vuelve a confirmar en los menús** (v01.27). El borrador previo dejaba los dos caminos mutuamente
  excluyentes mientras hubiera una partida cargada, dejando sin efecto la confirmación en el menú de pausa,
  la pantalla de fin de nivel y varias más.
- **Toggle para ocultar los botones táctiles** (v01.21 → v01.22 → v01.25 → v01.28). Llegó en v01.21 con
  Select+Start, que chocaba con la pausa (v01.22 lo movió a **L+R**); después ocultar los botones
  deshabilitaba caminar (v01.25 lo tapó con opacidad); ahora usa directamente las dos funciones que el juego
  expone para eso, y el problema no puede repetirse porque el juego deja de sondear lo que oculta.
- **Corrupción de heap arreglada** (v01.27). El toggle de v01.25/26 escribía hasta ~150 bytes más allá del
  final de un objeto de 284 bytes, cada vez que se usaba — dos de los seis punteros que trataba como
  sprites eran en realidad botones de menú.

#### Diagnóstico y estabilidad

- **Logs en vivo por Wi-Fi** (v01.23). El logger transmite cada línea por UDP broadcast además de
  escribirla al archivo, así que una sesión que termina en crash o reinicio duro ya no pierde su cola.
  Compatible directo con `psvita-toolkit logs-live`. Solo en builds con `ENABLE_VERBOSE_LOG=ON`.
- **Crash de v01.23 arreglado** (v01.24). El logger de red se caía al arrancar: en PS Vita los módulos
  `SceNet`/`SceNetCtl` no son residentes y hay que cargarlos con `sceSysmoduleLoadModule` antes de usarlos.
- **Splashscreen de vitaGL desactivable** (v01.28, opción `NO_SPLASHSCREEN`, activada por defecto).

### Instalación / actualización

Mismo `TITLEID` (`POPC00001`) que todas las builds desde v01.10: **instalar encima conserva las partidas
guardadas.** Si venís de v01.20 o posterior, basta con instalar el `.vpk`; los datos del juego en
`ux0:data/popclassic/` no cambian.

Instalación desde cero (incluido cómo armar el árbol de datos desde tu copia legal del juego Android):
[`Docs/INSTALL_HARDWARE.md`](INSTALL_HARDWARE.md).

Recordatorio de v01.20: **el `.apk` y el `.obb` ya no son obligatorios** — alcanza con la carpeta
`Data`/`Data_960_576` suelta. Los archivos originales siguen soportados como alternativa.

### Sin confirmar / pendiente

- **Toda esta release está sin probar en consola.** Lo más probable que necesite ajuste: el umbral de
  250 ms para pasar de caminar a correr, y si `IsXperia` cambia algo más que las tres cosas verificadas.
- **No compatible con Vita3K.** Es un bug del emulador, no del port (texturas swizzled no potencia de 2).
  Este port apunta a hardware real.
- **Sin las tipografías originales del juego** — se usa DejaVu Serif por temas de licencia.

### Para desarrolladores

El detalle técnico completo de cada cambio, con direcciones y evidencia, está en el repo:

- [`Docs/Fixes_Log.md`](Fixes_Log.md) §20–§27 — un item por versión.
- [`Docs/CHANGELOG.md`](CHANGELOG.md) — tabla versión por versión, bilingüe.
- [`docs/comments/main.c.md`](comments/main.c.md) — **la referencia de controles**: layout de miembros de
  `ControlsLayer`, tabla keycode → handler → evento, el bitmask `CONTROL_EVENT`, y la regla de
  caminar→correr. Fuente de la verdad; no re-derivarlo.

Dos conclusiones de sesiones anteriores quedaron corregidas con fecha en esa documentación, porque eran
falsas y costaron varias releases: `control1Clicked`/`control2Clicked` estaban invertidos, y
`CCDirector+0xad` no es `m_bPaused` sino el flag `IsXperia`. **Lección de método:** Ghidra se rinde en
varias de estas funciones (`"WARNING: Subroutine does not return"`) y las dos conclusiones equivocadas
salieron de esa salida parcial. Leer el `arm-vita-eabi-objdump` real y resolver los literales pc-relativos
para obtener los nombres de sprite frame es lo que resolvió el caso.

---

## 🇬🇧 English

### The headline

**Physical buttons are real controls now.** This port wraps the **Xperia PLAY** build of the game, which
ships a complete physical-gamepad code path — one this port had never switched on. Now it does. The
on-screen touch buttons hide themselves, movement no longer depends on faking touches at guessed
coordinates, and **you can jump while running**.

### New controls

| Button | Platform mode | Combat mode |
|:---:|:---|:---|
| **Left stick / D-Pad** | Move — tap for one careful step, **hold to run** | same |
| **Cross** | **Jump** | Attack |
| **Square** | Crouch | **Defend / parry** |
| **Triangle** | Interact (grab, levers) | Sheath sword |
| **Circle / Down** | Crouch | — |
| **Up** | Jump / climb | — |
| **Start** | Pause menu | Pause menu |
| **Select** | Menu | Menu |
| **L + R together** | Show/hide the on-screen touch buttons | same |
| **Touchscreen** | Still works for menu navigation | — |

> 🔔 **Two changes to know about if you're coming from v01.20:**
>
> 1. **Square is no longer the "walk" modifier** — it is crouch / defend now. Walking needs no modifier
>    any more: tap for a step, hold to run, exactly how the on-screen arrows always behaved.
> 2. **The D-Pad runs now.** "D-Pad only walks, use the analog stick until it's fixed" was a known
>    limitation from the very first build. It no longer applies — D-Pad and stick do exactly the same thing.

### What changed since v01.20

#### Controls (the bulk of this release)

- **Native gamepad path enabled** (v01.28). `nativeSetPaths`' third argument is a device name, and the
  engine uses it for exactly one thing: if it's `"R800i"` (the Xperia PLAY's model number), it switches on
  the game's physical-gamepad mode. The port had been passing `"PSVita"` since its first commit, so the
  game's entire keypad API was unreachable. Effects: the on-screen controls are hidden by the game's own
  code (no tricks), menus get a D-Pad-navigable cursor, and tutorials describe physical buttons instead of
  touch ones.
- **You can jump while running** (v01.28). This was the reported bug that survived four attempted fixes. It
  was never an engine block: the synthetic-touch emulation couldn't express "run" and "jump" at once
  because they competed for finite touch slots. Through real keycodes, run and jump are two separate bits
  of the same mask and coexist for free.
- **Walk vs. run, using the game's own rule** (v01.28). It replays the same 250 ms threshold the on-screen
  arrows use, instead of the three invented mechanics tried before (distance-from-centre, double-tap, drag
  simulation — none of which worked).
- **Cross jumps again** (v01.27). v01.26 had concluded, with two functions inverted, that jump was
  `control1Clicked` — which is actually **crouch**. Keycode 23, which the port had been sending all along,
  was the correct jump event the whole time.
- **Cross confirms in menus again** (v01.27). The previous draft made the two paths mutually exclusive for
  as long as a game was loaded, killing confirm in the pause menu, the level-complete screen and several
  others.
- **Toggle to hide the touch buttons** (v01.21 → v01.22 → v01.25 → v01.28). Shipped in v01.21 on
  Select+Start, which collided with pause (v01.22 moved it to **L+R**); then hiding the buttons disabled
  walking (v01.25 papered over it with opacity); now it uses the two functions the game exposes for exactly
  this, and the problem can't recur because the game stops polling what it hides.
- **Heap corruption fixed** (v01.27). The v01.25/26 toggle wrote up to ~150 bytes past the end of a
  284-byte object every time it was used — two of the six pointers it treated as sprites were actually menu
  buttons.

#### Diagnostics and stability

- **Live logs over Wi-Fi** (v01.23). The logger broadcasts every line over UDP as well as writing it to the
  file, so a session that ends in a crash or hard reset no longer loses its tail. Works directly with
  `psvita-toolkit logs-live`. Only in `ENABLE_VERBOSE_LOG=ON` builds.
- **v01.23's crash fixed** (v01.24). The network logger died on startup: on PS Vita the `SceNet`/`SceNetCtl`
  modules aren't resident and must be loaded with `sceSysmoduleLoadModule` before use.
- **vitaGL splashscreen can be disabled** (v01.28, `NO_SPLASHSCREEN` option, on by default).

### Install / upgrade

Same `TITLEID` (`POPC00001`) as every build since v01.10: **installing over an older one keeps your saves.**
Coming from v01.20 or later, just install the `.vpk` — the game data in `ux0:data/popclassic/` is unchanged.

Installing from scratch (including how to build the data tree from your own legal copy of the Android
game): [`Docs/en/INSTALL_HARDWARE.md`](en/INSTALL_HARDWARE.md).

Reminder from v01.20: **the `.apk` and `.obb` are no longer required** — the loose `Data`/`Data_960_576`
folder is enough. The original files are still supported as an alternative.

### Unconfirmed / open

- **This whole release is untested on console.** Most likely to need tuning: the 250 ms walk-to-run
  threshold, and whether `IsXperia` changes anything beyond the three verified effects.
- **Not compatible with Vita3K.** That's an emulator bug, not a port bug (non-power-of-2 swizzled
  textures). This port targets real hardware.
- **Not using the game's original fonts** — DejaVu Serif is used instead, for licensing reasons.

### For developers

The full technical detail of every change, with addresses and evidence, lives in the repo:

- [`Docs/Fixes_Log.md`](Fixes_Log.md) #20–#27 — one item per version.
- [`Docs/CHANGELOG.md`](CHANGELOG.md) — bilingual version-by-version table.
- [`docs/comments/main.c.md`](comments/main.c.md) — **the controls reference**: `ControlsLayer`'s member
  layout, the keycode → handler → event table, the `CONTROL_EVENT` bitmask, and the walk-to-run rule.
  Source of truth; don't re-derive it.

Two conclusions from earlier sessions are corrected in place, dated, in that documentation, because they
were false and cost several releases: `control1Clicked`/`control2Clicked` were inverted, and
`CCDirector+0xad` is not `m_bPaused` but the `IsXperia` flag. **Method lesson:** Ghidra bails on several of
these functions (`"WARNING: Subroutine does not return"`) and both wrong conclusions came out of that
partial output. Reading the real `arm-vita-eabi-objdump` output and resolving the pc-relative literals to
get the sprite frame names is what cracked it.

---

<p align="center">
  Port / wrapper development: <b>MetalSyntax</b> ·
  built on <a href="https://github.com/v-atamanenko/soloader-boilerplate">soloader-boilerplate</a>
  by v-atamanenko, itself based on TheFloW's Android SO Loader approach.
</p>
