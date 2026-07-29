<h1 align="center">
Prince of Persia Classic · PSVita Port
</h1>
<p align="center">
  <a href="#instalación">Cómo Instalar</a> •
  <a href="#controles">Controles</a> •
  <a href="#compilación-para-desarrolladores">Compilación</a> •
  <a href="README_EN.md">English Version</a>
</p>

Prince of Persia Classic es una versión de 2012 para dispositivos Android e iOS que recrea los niveles del legendario juego clásico de 1989 con gráficos tridimensionales modernizados.

Este repositorio contiene un **cargador (wrapper)** para la versión de Android de Prince of Persia Classic (basada en Cocos2d-x). Adapta el entorno para que las librerías dinámicas ARM nativas (`.so`) se ejecuten en la PS Vita usando la base de Android SO Loader por TheFloW.

## ⚠️ Aviso Legal / DMCA Disclaimer
**Prince of Persia Classic** es propiedad intelectual de Ubisoft Entertainment.  
Este repositorio **NO** contiene código original, ejecutables, binarios protegidos ni recursos del juego. Solo se distribuye el código fuente abierto del "wrapper" (cargador). Los recursos visuales de LiveArea incluidos son imágenes generadas por IA u open-source, libres de restricciones de copyright.

Para jugar, es indispensable contar con una copia legítima del juego para Android. El usuario debe extraer y proporcionar sus propias librerías `.so` y los datos del juego (ya sea como `.apk`/`.obb`, o como los archivos equivalentes extraídos sueltos — ver el paso 5 más abajo).

---

## Instalación (Para Jugadores)

Para realizar la instalación en una PS Vita real:

1. Asegúrate de tener **Enso** (firmware 3.60 o 3.65).
2. Instala los plugins [kubridge] y [FdFix] en tu `config.txt` bajo `*KERNEL`:
   ```
   *KERNEL
   ur0:tai/kubridge.skprx
   ur0:tai/fd_fix.skprx
   ```
3. Instala `libshacccg.suprx` (puedes usar la app ShaRKBR33D).
4. Instala el archivo `popclassic_audio.vpk` (build con audio; la antigua `popclassic.vpk` es la build estable sin sonido) en tu consola.
5. Obtén tu juego legal de Android y copiá sus datos a `ux0:data/popclassic/`. **Desde v01.20, `original.apk` y el `.obb` ya no son obligatorios** (confirmado en consola real) siempre que esté la carpeta `Data_960_576/` completa suelta (con `Localization/` y `appConfig.txt` incluidos) — el motor antes mandaba cualquier archivo pedido directo al `.apk`/`.obb` sin intentar nunca un archivo suelto en ese punto puntual; ahora se intenta primero un archivo suelto para cualquier ruta, y solo se cae al `.apk`/`.obb` si no se encuentra nada suelto (ver `Docs/Fixes_Log.md` §19). Si preferís, seguís pudiendo usar un **APK modificado mínimo** + un **OBB modificado** (optimizado/extraído) en vez de la carpeta suelta completa — ambos caminos funcionan.
6. Extrae las librerías de la carpeta `lib/armeabi/` o `lib/armeabi-v7a/` de tu `.apk` y colócalas junto con los datos del juego:
   * `libcocos2d.so`
   * `libcocosdenshion.so`
   * `libgame_logic.so`

*(Nota: Revisa los foros de la comunidad para encontrar herramientas y scripts de parcheo para preparar tu APK y OBB legalmente, o para extraer directamente la estructura de carpetas equivalente suelta).*

## Controles

| Botón | Acción |
|:---:|:---|
| Left Analog / D-Pad | Mover al Príncipe (Izquierda/Derecha), Agacharse (Abajo), Saltar/Trepar (Arriba) |
| Cross | Saltar |
| Circle | Rodar |
| Square | Acción / Usar espada |
| Start | Menú de Pausa (Android KEYCODE_BACK) |
| Touchscreen | Navegación por los menús táctiles |

## Compilación (Para Desarrolladores)

Se requiere el SDK de PS Vita compilado con soporte para `softfp`:

```bash
git clone https://github.com/vitasdk-softfp/vdpm
```

Para generar el `.vpk` usando CMake:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Licencia

Este cargador se distribuye bajo la licencia MIT. Consulta el archivo `LICENSE` para más detalles.
