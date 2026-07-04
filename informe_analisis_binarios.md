# Informe de análisis estático — Fase 1 del plan de portabilidad

Entregable de la **Fase 1** de `plan_portabilidad.md` ("Hallazgos de la inspección estática de los `.so`").
Todo lo que sigue se generó con comandos ejecutables desde macOS sin toolchain ARM cruzado
(`objdump`, `unzip -l`, `strings`, `file`), sobre los tres binarios reales en `bin/`. Los comandos exactos
están documentados en cada sección para que sea reproducible si se reemplazan los `.so` por otra versión
del APK.

## 0. Identificación de los binarios

```bash
file bin/*.so
```

| Archivo | Tamaño | Fecha de build | Formato |
|---|---:|---|---|
| `libcocosdenshion.so` | 74,532 B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |
| `libcocos2d.so` | 1,437,604 B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |
| `libgame_logic.so` | 1,345,616 B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |

Los tres tienen tabla de símbolos dinámica intacta (`stripped` solo quitó símbolos locales/debug, no la
tabla dinámica), así que `objdump -T`/`-p` funcionan igual que con un binario no-stripped para todo lo que
importa a un loader `so_util`.

## 1. Sección dinámica: `NEEDED` / `SONAME`

```bash
objdump -p bin/<lib>.so | sed -n '/Dynamic Section/,/^$/p'
```

```
libcocosdenshion.so   SONAME=libcocosdenshion.so
                      NEEDED: liblog libstdc++ libm libc libdl

libcocos2d.so         SONAME=libcocos2d.so
                      NEEDED: libGLESv1_CM liblog libz libstdc++ libm libc libdl

libgame_logic.so      SONAME=libgame_logic.so
                      NEEDED: libGLESv1_CM libcocos2d.so libcocosdenshion.so liblog libstdc++ libm libc libdl
```

**Conclusión (usada en Fase 4 del plan):** `libgame_logic.so` es el único que declara dependencia explícita
sobre los otros dos `.so` del juego. Orden de carga obligatorio: `libcocosdenshion.so` →
`libcocos2d.so` → `libgame_logic.so`. `lib/so_util/so_util.c` ya resuelve símbolos cruzados entre módulos
cargados automáticamente vía `so_resolve_link()` comparando estos mismos `NEEDED`/`SONAME`, así que no hace
falta ningún mecanismo adicional.

## 2. Inventario de símbolos exportados vs. no resueltos (`UND`)

```bash
objdump -T bin/<lib>.so > <lib>_dynsym.txt
grep -c '\*UND\*' <lib>_dynsym.txt          # no resueltos
grep -vc '\*UND\*' <lib>_dynsym.txt         # exportados
grep -c ' Java_' <lib>_dynsym.txt           # métodos JNI exportados
```

| Librería | Símbolos totales | Exportados | `UND` (a resolver) | Métodos `Java_` exportados | `JNI_OnLoad` |
|---|---:|---:|---:|---:|:---:|
| `libcocosdenshion.so` | 431 | 400 | 27 | 0 | sí |
| `libcocos2d.so` | 5,513 | 5,344 | 165 | 25 | sí |
| `libgame_logic.so` | 5,444 | 5,051 | 389 | 1 | no |

## 3. Dónde vive cada método JNI de Cocos2dx-Android (corrige un supuesto erróneo del plan original)

`libcocos2d.so` es el que expone **todo** el ciclo de vida de `Cocos2dxRenderer`/`Cocos2dxActivity`:

```
Cocos2dxRenderer_nativeRender
Cocos2dxRenderer_nativeTouchesBegin / _Move / _End / _Cancel
Cocos2dxRenderer_nativeKeyDown / nativeKeyUp
Cocos2dxRenderer_nativeOnPause / nativeOnResume
Cocos2dxRenderer_nativeInsertText / nativeDeleteBackward / nativeGetContentText
Cocos2dxActivity_nativeSetPaths / nativeSetPackageName / nativeSetNumOfCPUCores
Cocos2dxActivity_nativeSetDensityScaleValue / nativeSetDevicePixelsPerInch
Cocos2dxActivity_nativeSetIsGoogleLauncherBuild
Cocos2dxActivity_GetConfig / SetControlVisible / SetControlInVisible
Cocos2dxBitmap_nativeInitBitmapDC
Cocos2dxAccelerometer_onSensorChanged
Cocos2dxVideo_onVideoCompleted
org_ubisoft_InApp_InAppHandler_purchaseSuccessful
```

`libgame_logic.so` exporta **un solo** método `Java_`: `Cocos2dxRenderer_nativeInit`. Como Android carga
`libgame_logic.so` en último lugar (`System.loadLibrary` orden: denshion → cocos2d → game_logic), su
`nativeInit` es el que efectivamente se ejecuta al llamar `Cocos2dxRenderer.nativeInit()` desde Java (shadowing
por orden de carga). **Regla para el loader (Fase 4):** resolver `nativeInit` desde `game_mod`; el resto
(`nativeRender`, `nativeTouches*`, `nativeKeyDown/Up`, `nativeSetPaths`, etc.) desde `cocos2d_mod`, porque
`game_mod` no los redefine.

`libcocosdenshion.so` no expone ningún `Java_`: solo la clase C++ `CocosDenshion::SimpleAudioEngine`,
llamada directo en C++ desde los otros dos módulos (ver §4).

## 4. Prueba de la resolución cruzada entre módulos

```bash
comm -12 <(sort libgame_logic_UND.txt) <(sort libcocos2d_EXPORTS.txt)
comm -12 <(sort libgame_logic_UND.txt) <(sort libcocosdenshion_EXPORTS.txt)
```

- **284** símbolos `UND` de `libgame_logic.so` están exportados por `libcocos2d.so` (se resuelven solos si
  se respeta el orden de carga de §1).
  - 235 son del namespace `cocos2d::` (motor: `CCSprite`, `CCNode`, `CCArray`, `CCMenuItem`, etc.)
  - 21 son de utilidades third-party empaquetadas dentro de `libcocos2d.so`, no del engine en sí:
    `CCFlurryUtils` (analytics), `CCShareUtils` (compartir/Facebook), `CCVideoUtils` (ads en video),
    `CCInAppUtils` (compras in-app), `CCCrossPromoUtils`, `CCPapayaUtils` (red social Papaya, ver
    `original/.../com.papaya.socialsdk...` en el APK), `CCGeneralUtils`, `CCFileUtils`.
    **Acción para fases posteriores:** estas 8 clases son candidatas a **no-opear** (devolver
    `false`/vacío/sin efecto) en vez de reimplementar de verdad — no tiene sentido portar telemetría de
    Flurry o compras in-app de Google Play a Vita. Priorizar solo si el juego cuelga al no recibir una
    respuesta válida.
  - 28 restantes: mezcla de símbolos de biblioteca estándar de C++ (`std::`, RTTI) que cuentan como
    "exportados" en `libcocos2d.so` por casualidad de layout, no relevantes de listar acá.
- **15** símbolos `UND` de `libgame_logic.so` están en `libcocosdenshion.so`: exactamente los 15 métodos de
  `CocosDenshion::SimpleAudioEngine` (`playEffect`, `playBackgroundMusic`, `setEffectsVolume`, etc.) — el
  juego llama al audio en C++ directo, no por JNI. Esto es la base de la Fase 5 del plan (reimplementación
  nativa de audio).

## 5. Lo que queda por resolver vía `default_dynlib` (reimpl de libc/GLES/etc.)

Descontando la resolución cruzada de §4, quedan:

- `libcocosdenshion.so`: 27 símbolos — pura libc/pthread (`malloc`, `free`, `sprintf`, `strcmp`,
  `pthread_mutex_lock`, `__android_log_print`, etc.). Ya cubierto por `source/reimpl/*` +
  `lib/libc_bridge`.
- `libcocos2d.so`: 165 símbolos — libc/pthread + **zlib** (`crc32`, `deflate*`, `inflate*`, `gzopen`,
  `gzread`, `uncompress` — ya cubierto por el `z` en `target_link_libraries`) + **OpenGL ES 1.1 de función
  fija completo**:
  ```
  glAlphaFunc glBindBuffer glBindFramebufferOES glBindTexture glBlendFunc glBufferData glBufferSubData
  glCheckFramebufferStatusOES glClear glClearColor glClearDepthf glColor4f glColorMask glColorPointer
  glCompressedTexImage2D glDeleteBuffers glDeleteFramebuffersOES glDeleteTextures glDepthFunc glDepthMask
  glDisable glDisableClientState glDrawArrays glDrawElements glEnable glEnableClientState
  glFramebufferTexture2DOES glFrustumf glGenBuffers glGenFramebuffersOES glGenTextures
  glGenerateMipmapOES glGetError glGetFloatv glGetIntegerv glGetString glLoadIdentity glMatrixMode
  glMultMatrixf glOrthof glPixelStorei glPointSizePointerOES glPopMatrix glPushMatrix glReadPixels
  glRotatef glScissor glTexCoordPointer glTexEnvi glTexImage2D glTexParameteri glTranslatef
  glVertexPointer glViewport
  ```
  **Riesgo concreto para la Fase 4, punto 6:** esto es la pila de matrices/función-fija de ES1
  (`glLoadIdentity`, `glMultMatrixf`, `glPushMatrix`/`glPopMatrix`, `glRotatef`/`glTranslatef`,
  `glTexEnvi`, `glAlphaFunc`) — vitaGL apunta sobre todo a un pipeline moderno tipo ES2; hay que confirmar
  antes de escribir código que `source/reimpl/egl.c`/`source/utils/glutil.c` (o vitaGL directamente) cubren
  esta capa de compatibilidad ES1, o si hace falta agregar una emulación de matrix-stack (patrón habitual
  en otros ports de motores ES1 a vitaGL).
- `libgame_logic.so`: 90 símbolos — mismo perfil libc/pthread que los anteriores, sin nada nuevo fuera de
  lo ya cubierto por el boilerplate.

## 6. Pistas de rutas de archivos (afecta directamente la Fase 2 del plan — **corrección importante**)

```bash
strings -a bin/libgame_logic.so | grep -E "^Data/|^Data_640_384/|^Data_960_576/"
```

- `libgame_logic.so` contiene **125 literales completos** con el prefijo `Data/`
  (ej. `Data/Animations/big_guard_final/big_guard_final_01.plist`).
- **Cero** ocurrencias de `Data_640_384/` o `Data_960_576/` como literal completo en el binario.

Esto **corrige la sección de assets del plan principal**: el código compilado sólo pide rutas bajo un
directorio llamado literalmente `Data`, no `Assets` (como decía la v1 del plan) y no `Data_960_576`
(como sugería la v2, razonando solo por calce de resolución de pantalla). **La carpeta en la tarjeta debe
llamarse `Data/`, no `Assets/`.**

Aparte, comparando tamaños de archivo:

```bash
unzip -l original/main.1.org.ubisoft.premium.POPClassic.zip | grep "Logo/logo.png$"
# Data/Logo/logo.png            123079 B
# Data_640_384/Logo/logo.png     55420 B
# Data_960_576/Logo/logo.png     89080 B
ls -la bin/popclassic/Logo/logo.png
# 89080 B
```

`bin/popclassic/` ya fue extraído del bucket **`Data_960_576`** (buen calce con los 960×544 de la Vita en
cuanto a contenido/calidad de textura), pero debe **renombrarse/moverse a una carpeta llamada `Data/`** en
el destino final, porque el binario no sabe buscar en `Data_960_576/`. Es decir: contenido de
`Data_960_576`, nombre de carpeta `Data`.

También aparecen los nombres de archivo de guardado (`/pop_save_normal`, `/pop_save_profile`,
`/pop_save_survivor`, `/pop_save_time_trial`, `/pop_save_level_specific`, `/pop_save_final`,
`/pop_save_updateV1[_fixed]`), con `/` inicial — consistentes con una ruta tipo
`getFilesDir() + "/pop_save_normal"` en Android (`/data/data/<pkg>/files/...`). Confirma que hay que
interceptar esas rutas (Fase 4, punto 2) y redirigirlas a `ux0:data/popclassic/save/`.

## 7. Acción inmediata sobre `plan_portabilidad.md`

Los hallazgos de §6 contradicen la Fase 2 tal como está redactada hoy (usa `Assets/` como nombre de
carpeta y sugiere evaluar usar el bucket `Data_960_576` completo). Corresponde actualizar esa fase para
reflejar: carpeta destino `Data/`, contenido tomado de `Data_960_576` del `.obb` (que es lo que ya hay en
`bin/popclassic/`), sin necesidad de volver a extraer nada de resolución.
