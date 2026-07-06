<h1 align="center">
Prince of Persia Classic · PSVita Port
</h1>
<p align="center">
  <a href="#setup-instructions-for-players">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="#license">License</a>
</p>

Prince of Persia Classic es una versión de 2012 para dispositivos Android e iOS que recrea los niveles del legendario juego clásico de 1989 con gráficos tridimensionales modernizados.

Este repositorio contiene un cargador para **la versión de Android de Prince of Persia Classic (basada en Cocos2d-x)**, adaptando el entorno para que se ejecuten las librerías dinámicas ARM nativas en la PS Vita usando la base de [Android SO Loader por TheFloW].

Disclaimer
----------------

**Prince of Persia Classic** es propiedad intelectual de Ubisoft Entertainment. Este software no contiene código original, ejecutables, ni recursos del juego. Para jugar, es indispensable contar con una copia legítima en formato `.apk` y su correspondiente archivo de datos `.obb`.

Setup Instructions (For Players)
----------------

Para realizar la instalación en una PS Vita real:

- Asegúrate de estar en una versión de firmware Enso (3.60 o 3.65).
- Instala o actualiza los plugins de kernel [kubridge] y [FdFix] agregándolos a tu `config.txt` bajo la sección `*KERNEL`:
  ```
  *KERNEL
  ur0:tai/kubridge.skprx
  ur0:tai/fd_fix.skprx
  ```
- Copia el archivo `libshacccg.suprx` en `ur0:/data/` (se puede descargar usando la herramienta ShaRKBR33D).
- Extrae las siguientes librerías de la carpeta `lib/armeabi/` o `lib/armeabi-v7a/` de tu archivo `.apk` y colócalas en `ux0:data/popclassic/`:
  * `libcocos2d.so`
  * `libcocosdenshion.so`
  * `libgame_logic.so`
- Extrae la carpeta de recursos de tu archivo de datos `.obb` (usualmente contiene carpetas como `Animations`, `Texture`, etc.) y colócala en `ux0:data/popclassic/Assets/`.
- Instala el archivo `popclassic.vpk` generado.

Controls
-----------------

| Botón | Acción |
|:---:|:---|
| Left Analog / D-Pad | Mover al Príncipe (Izquierda/Derecha), Agacharse (Abajo), Saltar/Trepar (Arriba) |
| Cross | Saltar |
| Circle | Rodar |
| Square | Acción / Usar espada |
| Start | Menú de Pausa (Android KEYCODE_BACK / Botón Atrás) |
| Touchscreen | Navegación por los menús táctiles |

Build Instructions (For Developers)
----------------

Para compilar el proyecto se requiere el SDK de PS Vita compilado con soporte para `softfp`:

```bash
git clone https://github.com/vitasdk-softfp/vdpm
```

Para generar la compilación usando CMake:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

License
----------------

Este cargador se distribuye bajo los términos de la licencia MIT. Consulta el archivo [LICENSE](LICENSE) para más detalles.
