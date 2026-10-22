<!--
  GitHub Release body for v01.50.
  Title: "v01.50 — Full Simplified Chinese Text"
-->

# v01.50 — Full Simplified Chinese Text

**Fixes missing characters when playing in Chinese.**

---

## 🇪🇸 Novedades

### Texto en chino completo
- Al jugar en chino faltaban caracteres en menús y textos. El juego usa `msmincho.ttf` (fuente japonesa), que no tiene 198 de los 700 caracteres de chino simplificado del juego; en Android el sistema los completaba, en la Vita quedaban en blanco.
- Se agrega una fuente de respaldo (`NotoSerifSC-Subset.otf`, Noto Serif SC, licencia OFL) que solo se usa para los caracteres que la fuente original no tiene. El resto del texto se sigue dibujando con la fuente original.

### Instalación
1. Instala `popclassic.vpk` con VitaShell (se conservan las partidas guardadas).

---

## 🇬🇧 Release Notes

### Full Chinese text
- Some characters were missing when playing in Chinese. The game uses `msmincho.ttf` (a Japanese font) that lacks 198 of the game's 700 Simplified Chinese characters; Android filled them from system fonts, on Vita they were blank.
- Added a fallback font (`NotoSerifSC-Subset.otf`, Noto Serif SC, OFL license) used only for characters the original font lacks. All other text still renders with the original font.

### Installation
1. Install `popclassic.vpk` with VitaShell (save data is kept).
