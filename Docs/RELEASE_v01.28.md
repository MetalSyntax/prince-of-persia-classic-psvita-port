<!--
  GitHub Release body for v01.28. Paste the contents of this file into the release
  description at https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/releases/new
  (tag v01.28, title "v01.28 — Real physical controls").
  Self-contained on purpose: absolute links only, no repo-relative paths, so it reads
  correctly on the Releases page. Keep it out of README.md.
-->

**Los botones físicos ahora son controles de verdad — y se puede saltar en corrida.**
**Physical buttons are real controls now — and you can jump while running.**

🇪🇸 [Español](#-español) · 🇬🇧 [English](#-english)

> [!WARNING]
> **Esta build no está confirmada en consola / this build is not hardware-confirmed yet.**
> Compila limpio y cada cambio está respaldado por disassembly de las librerías reales del juego, pero el
> rework de controles no se jugó todavía en una Vita. Si algo sale mal, **v01.27** es la build de retirada.
> It builds clean and every change is backed by disassembly of the real game libraries, but the control
> rework hasn't been played on a console yet. If something is off, **v01.27** is the fallback build.

---

## 🇪🇸 Español

### Descarga e instalación

1. Descargá **`popclassic.vpk`** de los assets de este release.
2. Instalalo con **VitaShell** en tu PS Vita (con HENkaku/h-encore).
3. Copiá los datos del juego a `ux0:data/popclassic/` desde tu **copia legal** del juego Android.
   Guía completa: [INSTALL_HARDWARE.md](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/blob/master/Docs/INSTALL_HARDWARE.md)

**¿Actualizando?** Mismo `TITLEID` (`POPC00001`) que todas las builds desde v01.10: **instalar encima
conserva las partidas guardadas.** Si venís de v01.20 o posterior, alcanza con instalar el `.vpk` — los
datos en `ux0:data/popclassic/` no cambian.

> Este repo **no contiene** código, binarios ni assets originales del juego. Prince of Persia Classic es
> propiedad de Ubisoft Entertainment; necesitás tu propia copia legal del juego Android.

### Controles

El juego alterna solo entre modo "plataformas" y modo "combate" (espada envainada o no), así que varios
botones tienen doble función.

| Botón | Plataformas | Combate |
|:---:|:---|:---|
| **Stick izq. / D-Pad** | Mover — toque corto = un paso medido, **mantenido = correr** | igual |
| **Cruz** | **Saltar** | Atacar |
| **Cuadrado** | Agacharse | **Defender / parar** |
| **Triángulo** | Interactuar (agarrar, palancas) | Envainar la espada |
| **Círculo / Abajo** | Agacharse | — |
| **Arriba** | Saltar / trepar | — |
| **Start** | Menú de pausa | Menú de pausa |
| **Select** | Menú | Menú |
| **L + R juntos** | Mostrar/ocultar los botones táctiles | igual |
| **Pantalla táctil** | Sigue funcionando para navegar menús | — |

> [!IMPORTANT]
> **Si venías de v01.20 o anterior, dos cosas cambiaron:**
> 1. **Cuadrado ya no es el modificador de "andar"** — ahora es agacharse / defender. Andar ya no necesita
>    modificador: toque corto para un paso, mantenido para correr.
> 2. **El D-Pad ya corre.** Era una limitación conocida desde la primera build ("usá el stick analógico
>    hasta que se arregle"); ya no aplica.

### Novedades

#### 🎮 Controles

- **Se puede saltar en corrida.** Era el bug reportado que sobrevivió a cuatro intentos de arreglo. Nunca
  fue un bloqueo del motor: la emulación de toques sintéticos no podía expresar "correr" y "saltar" a la vez
  porque competían por ranuras de toque finitas.
- **Los botones físicos usan la ruta de gamepad del propio juego.** Este es el port de la build de **Xperia
  PLAY**, que trae soporte completo de gamepad físico… y el port nunca lo había encendido (mandaba
  `"PSVita"` como nombre de dispositivo en vez de `"R800i"`, el modelo del Xperia PLAY, desde su primer
  commit). Ahora sí: los botones táctiles se ocultan con el código del propio juego, los menús ganan cursor
  navegable con el D-Pad, y los tutoriales describen botones físicos.
- **Caminar vs. correr usa la regla real del juego** (umbral de 250 ms), en vez de las tres mecánicas
  inventadas que se probaron antes — distancia al centro, doble toque, simulación de arrastre — ninguna de
  las cuales funcionó.
- **Cruz vuelve a saltar.** v01.26 había concluido, con dos funciones invertidas, que el salto era
  `control1Clicked` — que en realidad es *agacharse*.
- **Cruz vuelve a confirmar en los menús**, roto por el borrador anterior mientras hubiera partida cargada.
- **Toggle L+R para ocultar los botones táctiles**, ya sin el bug de v01.25 que deshabilitaba caminar.
- **Corrupción de memoria arreglada**: el toggle anterior escribía hasta ~150 bytes más allá del final de un
  objeto, cada vez que se usaba.

#### 🩺 Diagnóstico y estabilidad

- **Logs en vivo por Wi-Fi** (UDP broadcast, además del archivo): una sesión que termina en crash o reinicio
  duro ya no pierde su cola. Solo en builds con `ENABLE_VERBOSE_LOG=ON`.
- **Crash arreglado** al arrancar ese logger de red (faltaba cargar los módulos `SceNet`/`SceNetCtl`).
- **Splashscreen de vitaGL desactivable** (`NO_SPLASHSCREEN`, activado por defecto).

### Sigue pendiente

- **Esta release está sin probar en consola.** Lo más probable que necesite ajuste: el umbral de 250 ms
  para pasar de caminar a correr.
- **No compatible con Vita3K** — es un bug del emulador, no del port (texturas swizzled no potencia de 2).
  Este port apunta a hardware real.
- **Sin las tipografías originales** del juego: se usa DejaVu Serif por temas de licencia.

---

## 🇬🇧 English

### Download & install

1. Download **`popclassic.vpk`** from this release's assets.
2. Install it with **VitaShell** on your PS Vita (running HENkaku/h-encore).
3. Copy the game data to `ux0:data/popclassic/` from your **legally owned** copy of the Android game.
   Full guide: [INSTALL_HARDWARE.md](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/blob/master/Docs/en/INSTALL_HARDWARE.md)

**Upgrading?** Same `TITLEID` (`POPC00001`) as every build since v01.10: **installing over an older one keeps
your saves.** Coming from v01.20 or later, just install the `.vpk` — the data in `ux0:data/popclassic/` is
unchanged.

> This repo contains **no** original game code, binaries or assets. Prince of Persia Classic is the
> intellectual property of Ubisoft Entertainment; you need your own legal copy of the Android game.

### Controls

The game switches between "platform" and "combat" mode on its own (sword drawn or not), so several buttons
do double duty.

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

> [!IMPORTANT]
> **Coming from v01.20 or earlier, two things changed:**
> 1. **Square is no longer the "walk" modifier** — it's crouch / defend now. Walking needs no modifier any
>    more: tap for a step, hold to run.
> 2. **The D-Pad runs now.** "D-Pad only walks, use the analog stick until it's fixed" was a known
>    limitation from the very first build. It no longer applies.

### What's new

#### 🎮 Controls

- **You can jump while running.** This was the reported bug that survived four attempted fixes. It was never
  an engine block: the synthetic-touch emulation couldn't express "run" and "jump" at once because they
  competed for finite touch slots.
- **Physical buttons use the game's own gamepad path.** This port wraps the **Xperia PLAY** build, which
  ships full physical-gamepad support — and the port had never switched it on (it passed `"PSVita"` as the
  device name instead of `"R800i"`, the Xperia PLAY's model number, since its first commit). Now it does:
  the touch buttons are hidden by the game's own code, menus get a D-Pad-navigable cursor, and tutorials
  describe physical buttons.
- **Walk vs. run uses the game's real rule** (a 250 ms threshold), instead of the three invented mechanics
  tried before — distance-from-centre, double-tap, drag simulation — none of which worked.
- **Cross jumps again.** v01.26 had concluded, with two functions inverted, that jump was
  `control1Clicked` — which is actually *crouch*.
- **Cross confirms in menus again**, broken by the previous draft for as long as a game was loaded.
- **L+R toggle to hide the touch buttons**, now without v01.25's bug that disabled walking.
- **Memory corruption fixed**: the previous toggle wrote up to ~150 bytes past the end of an object every
  time it was used.

#### 🩺 Diagnostics & stability

- **Live logs over Wi-Fi** (UDP broadcast alongside the file): a session that ends in a crash or hard reset
  no longer loses its tail. `ENABLE_VERBOSE_LOG=ON` builds only.
- **Crash fixed** when starting that network logger (the `SceNet`/`SceNetCtl` modules weren't being loaded).
- **vitaGL splashscreen can be disabled** (`NO_SPLASHSCREEN`, on by default).

### Still open

- **This release is untested on console.** Most likely to need tuning: the 250 ms walk-to-run threshold.
- **Not compatible with Vita3K** — an emulator bug, not a port bug (non-power-of-2 swizzled textures). This
  port targets real hardware.
- **Not using the game's original fonts**: DejaVu Serif is used instead, for licensing reasons.

---

## 📦 Assets

| File | Size | SHA-256 |
|---|---|---|
| `popclassic.vpk` | 1.3 MB | `4bc99f64849b4eeb487d2179d4b154acfe249fec9f814bc0a3c7d26795f822c8` |

## 🔗 Links

- **Full technical changelog** (per-version, bilingual): [Docs/CHANGELOG.md](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/blob/master/Docs/CHANGELOG.md)
- **Deep dive per fix**, with addresses and evidence: [Docs/Fixes_Log.md](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/blob/master/Docs/Fixes_Log.md) §20–§27
- **Controls reference** for developers (`ControlsLayer` member layout, keycode → handler → event table, the `CONTROL_EVENT` bitmask, the walk→run rule): [docs/comments/main.c.md](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/blob/master/Docs/comments/main.c.md)
- **Commit diff since the last release**: [`v01.20...v01.28`](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/compare/v01.20...v01.28)
- **Report a bug**: [open an issue](https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/issues/new) — a log from an `ENABLE_VERBOSE_LOG=ON` build helps a lot.

---

Port / wrapper development: **MetalSyntax** · built on
[soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) by v-atamanenko, itself based
on TheFloW's Android SO Loader approach. Distributed under the MIT License.
