<!--
  GitHub Release body for v01.45.
  Title: "v01.45 — In-Game Controls Remapping Overlay, Exit Crash Fix & Trophies"
-->

# v01.45 — In-Game Controls Remapping Overlay, Exit Crash Fix & Native Trophies

**Adds an in-game controls remapping overlay menu (START + SELECT), bidirectional controls configuration persistence (`controls.txt`), fixes the in-game Exit button crash, and prepares native PS Vita trophies.**

---

## 🇪🇸 Novedades y Características

### 1. Menú Overlay de Configuración de Controles en Juego (START + SELECT)
- **Overlay integrado**: Presiona **START + SELECT** en cualquier momento para pausar el juego y abrir el menú flotante en pantalla sin salir de la partida.
- **Remapeo completo de acciones**: Cambia las asignaciones de salto, ataque, agacharse, defensa, interacción, trepar y movimiento.
- **Controles flexibles**:
  - Presiona **Cruz** sobre una acción para reemplazar su botón.
  - Presiona **Cuadrado** para añadir un botón secundario (asignación múltiple).
  - Presiona **Triángulo** para desasignar/limpiar una acción.
  - Presiona **Círculo** o selecciona "Save and Close" para guardar y continuar jugando.
- **Calibración de Zona Muerta (Analog Deadzone)**: Ajusta la sensibilidad de la palanca izquierda directamente desde el menú con las flechas Izquierda/Derecha.
- **Soporte para Panel Táctil Trasero y PSTV**:
  - Cuadrantes traseros asignables: `L2` (superior izq), `R2` (superior der), `L3` (inferior izq), `R3` (inferior der).
  - Reconocimiento nativo de gatillos L2/R2 físicos en PlayStation TV (DualShock 3 y DualShock 4).
- **Persistencia bidireccional (`controls.txt`)**: Todas las configuraciones se guardan y leen automáticamente desde `ux0:data/popclassic/controls.txt`.

### 2. Solución al Crash al Salir del Juego (Botón Salir / X)
- Se corrigió el cuelgue / core dump del sistema al seleccionar **"Sí"** en el cuadro de confirmación de salida del juego.
- Se implementó la llamada nativa a `Cocos2dxActivity_terminateProcess` con `sceKernelExitProcess(0)`, cerrando el juego limpiamente hacia el LiveArea.

### 3. Sistema de Trofeos PS Vita (En desarrollo / Work in Progress)
- Se ha integrado la infraestructura nativa (`SceNpTrophy`) y el paquete de 18 trofeos (`POPC00001_00/TROPHY.TRP`) con Platino exclusivo.
- **Estado actual**: Los trofeos **aún no se muestran ni se instalan en la consola** debido a un error del diálogo del sistema (`0x800204C4`). Se está trabajando activamente en la solución para una próxima actualización.

---

## 🇬🇧 Release Notes

### 1. In-Game Controls Remapping Overlay (START + SELECT)
- **Built-in Overlay Menu**: Press **START + SELECT** at any time to open the configuration menu directly over the paused game.
- **Full Action Remapping**:
  - **Cross**: Replace binding.
  - **Square**: Add secondary binding.
  - **Triangle**: Clear action binding.
  - **Circle / Start**: Save and close.
- **Analog Deadzone Calibration**: Fine-tune analog stick sensitivity with D-Pad Left/Right.
- **Rear Touch Quadrants & PSTV**: Full support for Vita rear touch quadrants (`L2`, `R2`, `L3`, `R3`) and native PSTV DualShock triggers.
- **Persistent Storage**: Bidirectional reading and writing from `ux0:data/popclassic/controls.txt`.

### 2. In-Game Exit Crash Fix
- Fixed a segmentation fault / data abort occurring when confirming exit in the game's confirmation dialog.
- Intercepted `Cocos2dxActivity_terminateProcess` via FalsoJNI to gracefully exit to LiveArea via `sceKernelExitProcess(0)`.

### 3. PS Vita Trophies (Work in Progress)
- Native trophy subsystem structure and 18-trophy pack (`TROPHY.TRP`) are implemented in code.
- **Current status**: Trophies do **not work/install yet on device** (system setup dialog error `0x800204C4`). Actively being worked on for the next update.

---

### Installation Instructions
1. Install `popclassic.vpk` using VitaShell.
2. Required plugins in `ur0:tai/config.txt`:
   ```ini
   *KERNEL
   ur0:tai/kubridge.skprx
   ur0:tai/fd_fix.skprx

   *main
   ur0:tai/NoTrpDrm.suprx
   ```
3. Reboot the console if `NoTrpDrm` was just added or changed.
