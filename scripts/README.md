# scripts/

Herramientas de automatización usadas durante las sesiones de depuración en Vita3K (ver
`plan_portabilidad.md` §9 para el contexto completo).

## Build y despliegue

- **`deploy_and_launch_vita3k.sh`** — compila (asumiendo `~/popc-src`/`~/popc-build` ya configurados, ver
  comentario en el propio script), copia `eboot.bin` + `DejaVuSans.ttf` directo al directorio de la app ya
  instalada en Vita3K (sin reinstalar el `.vpk`), relanza el emulador limpio, y hace doble clic real en el
  ícono del juego en la biblioteca. Es el flujo que se repitió manualmente decenas de veces en esta sesión.

## Automatización de input (Quartz/CGEvent)

> [!NOTE]
> Los clics/teclas de Accessibility (`osascript`, `System Events click`) **no funcionan** contra la ventana
> del juego en Vita3K — sí funcionan para navegar la ventana de la biblioteca (tabla de juegos, botones),
> pero no llegan al `GameWindow` de Qt una vez que el juego está corriendo (ver plan §9.14: requiere
> `focusInEvent` en esa ventana específica, que los eventos sintéticos no disparan). Estos scripts usan
> `Quartz.CGEventPost` en su lugar, que sí llega a la tabla/botones de la biblioteca — para tocar la
> pantalla/presionar botones *dentro* de una partida ya en curso hace falta un humano con mouse/teclado
> reales, no se pudo automatizar en esta sesión.

- **`click_helper.py <x> <y> [count]`** — clic real (mouseDown+mouseUp) en coordenadas de pantalla absolutas.
  `count` es cuántas veces repetirlo (usar `2` para simular un doble clic).
- **`hold_click.py <x> <y> [hold_seconds]`** — clic sostenido: mouseDown, espera, mouseUp.
- **`mousedown_only.py <x> <y>`** / **`mouseup_only.py <x> <y>`** — mouseDown y mouseUp por separado, para
  mantener un clic sostenido mientras se ejecutan otros comandos en el medio (usado para verificar si
  Vita3K registraba el estado del botón mientras seguía presionado).
- **`key_helper.py <tecla> [hold_seconds]`** — presiona una tecla por su nombre (mapeo reducido de keycodes
  de macOS, ver el diccionario `KEYCODES` dentro del script — agregar más si hace falta).

Todos requieren `pyobjc` (`Quartz`), ya disponible en este Mac vía `python3 -c "import Quartz"`.
