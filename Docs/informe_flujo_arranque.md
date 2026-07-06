# Informe de ingeniería inversa del flujo de arranque — Fase 3 del plan de portabilidad

Entregable de la **Fase 3** de `plan_portabilidad.md`. Se decompiló `classes.dex` del APK
(`original/Prince of Persia Classic 2.1.apk`) con `jadx` (instalado vía Homebrew) para leer la secuencia
exacta de llamadas nativas que hace la app real antes de renderizar el primer frame. Esto reemplaza la
suposición/orden "razonado desde los símbolos" de la Fase 1 por la secuencia real del código Java.

```bash
jadx -d /tmp/pop_decomp "original/Prince of Persia Classic 2.1.apk"
```

## 1. Orden real de `System.loadLibrary()` — corrige la Fase 1

`org/ubisoft/premium/POPClassic/POPClassic.java` (la Activity principal, subclase de
`Cocos2dxActivity`):

```java
static {
    System.loadLibrary("cocos2d");
    System.loadLibrary("cocosdenshion");
    System.loadLibrary("game_logic");
}
```

**El orden real es `cocos2d` → `cocosdenshion` → `game_logic`**, no `cocosdenshion` → `cocos2d` →
`game_logic` como se dedujo en la Fase 1 solo mirando el grafo de dependencias (`NEEDED`/`SONAME`). Esto
no rompe nada porque ni `libcocos2d.so` ni `libcocosdenshion.so` se necesitan mutuamente (confirmado en la
Fase 1: ninguno de los dos tiene al otro en su `NEEDED`), así que cualquier orden entre esos dos funciona
para la resolución de símbolos de `so_util`. Pero si en algún momento se agrega lógica que dependa de qué
móduo se "registra primero" (por ejemplo si se investiga más a fondo si `JNI_OnLoad` de `libcocos2d.so`
hace un `RegisterNatives` explícito — ver §4), **usar este orden real**, no el deducido:
`libcocos2d.so` → `libcocosdenshion.so` → `libgame_logic.so`.

## 2. Secuencia completa de inicialización nativa

`POPClassic.onCreate()` → `super.onCreate()` (crea acelerómetro, `Cocos2dxMusic`, `Cocos2dxSound`,
`Cocos2dxBitmap`, handler de diálogos, vistas de ads) → `setPackageName(getApplication().getPackageName())`
(definido en `Cocos2dxActivity.java`, llamado explícitamente por la subclase). Dentro de
`setPackageName()`, en este orden exacto:

```java
nativeSetPaths(apkFilePath, appInfo.sourceDir, device);   // 1
nativeSetPackageName(packageName2);                        // 2
nativeSetIsGoogleLauncherBuild(Config.isGoogleLauncherBuild());  // 3
GetConfig(apkFilePath, "DEVICE_SLEEP")                      // 4  (y más GetConfig, ver §3)
...
nativeSetNumOfCPUCores(getNumCores());                      // 5
nativeSetDensityScaleValue(dScale);                         // 6
nativeSetDevicePixelsPerInch(metrics.ydpi);                 // 7
SetControlInVisible();  // solo si config.hardKeyboardHidden == 1   // 8 (condicional)
```

Y **por separado, más tarde**, cuando `GLSurfaceView` crea la superficie GL (ciclo de vida asíncrono, no
parte de `onCreate`):

```java
// Cocos2dxRenderer.onSurfaceCreated():
nativeInit(this.screenWidth, this.screenHeight);
```

### Firmas exactas (de las declaraciones `native` en Java, confirman lo inferido en la Fase 1)

```c
void  nativeSetPaths(char* apkFilePath, char* apkSourceDir, char* device);  // 3 strings, no 1
void  nativeSetPackageName(char* packageName);
void  nativeSetIsGoogleLauncherBuild(bool isGoogleLauncherBuild);
bool  GetConfig(char* apkFilePath, char* key);
void  nativeSetNumOfCPUCores(int cores);
void  nativeSetDensityScaleValue(float scale);
void  nativeSetDevicePixelsPerInch(float ydpi);
void  SetControlVisible(void);
void  SetControlInVisible(void);
void  nativeInit(int screenWidth, int screenHeight);   // NO es (void), toma ancho/alto
```

**Corrección a la Fase 1:** `nativeSetPaths` toma **tres** strings, no solo la ruta base como se asumió.
Esto hay que replicarlo en `source/main.c` (Fase 4): llamar con `(ruta_base, ruta_base, "PSVita")` o
valores equivalentes razonables, ya que `appInfo.sourceDir`/`device` probablemente solo se usan para
logging o para decidir casos especiales de dispositivo (ver el caso especial `"GT-P1000T"` en el cálculo
de `dScale`, irrelevante en Vita).

## 3. Valores reales de configuración de esta build (resueltos desde `AndroidManifest.xml`)

```xml
<meta-data android:name="isCompletePackage" android:value="false"/>
<meta-data android:name="isGoogleLauncher" android:value="true"/>
```

Con `isCompletePackage=false` y `isGoogleLauncher=true`, la rama que se ejecuta en el APK real es:

```java
apkFilePath = Environment.getExternalStorageDirectory() + "/Android/obb/" + packageName2;
// = /storage/emulated/0/Android/obb/org.ubisoft.premium.POPClassic  (una CARPETA, no el .obb en sí)
```

Es decir, el motor recibe la **carpeta** estándar de OBB de Android, no el archivo `.obb` directamente ni
una carpeta de assets ya extraídos.

## 4. Riesgo abierto e importante: el motor probablemente lee el `.obb` como ZIP, no como carpeta plana

Evidencia:

- `nativeSetPaths` recibe una carpeta que en Android **contiene el `.obb` como archivo ZIP**
  (`main.1.org.ubisoft.premium.POPClassic.obb`), no archivos sueltos.
- `libcocos2d.so` tiene el string literal `.obb` y símbolos `UND` de zlib (`inflate*`, `deflate*`,
  `crc32`, `gzopen`/`gzread` — ver informe de la Fase 1, §5), consistentes con parsear un ZIP a mano
  (patrón común en 2012 para leer assets de la expansión sin extraerlos, ahorrando espacio en el
  dispositivo).
- Pero también existen (Fase 1, §6) 125 literales de ruta plana con prefijo `Data/` — lo cual es
  compatible tanto con "ruta dentro del ZIP" (los ZIP tienen rutas internas idénticas:
  `Data/Animations/...`) como con "ruta de archivo plano si alguien pre-extrajo".

**No se puede resolver con certeza solo con análisis estático sin desensamblar la lógica de
`CCFileUtils`/lectura de archivos en ARM.** Es un patrón común en muchos motores de esa época intentar
primero `fopen()` directo sobre la ruta compuesta y, si falla, caer a lectura dentro del ZIP — si es así,
extraer los assets a `Data/` como archivos sueltos (que es lo que ya se hizo en la Fase 2) funcionaría sin
más cambios. Si en cambio el motor **siempre** busca y abre el `.obb` como ZIP sin intentar `fopen()`
plano primero, va a fallar contra `ux0:data/popclassic/Data/...` como carpeta de archivos sueltos.

**Plan de mitigación para la Fase 4/9 (no bloquea seguir con Fase 4):**

1. Primera prueba (la más simple, ya tenemos los assets así): pasar como primer argumento de
   `nativeSetPaths` la ruta `ux0:data/popclassic/` (que contiene `Data/` con archivos sueltos) y, con
   `source/reimpl/io.c` logueando cada `fopen()`/`open()` que el juego intenta, ver en consola si:
   - intenta `ux0:data/popclassic/Data/Animations/...` directo → **assets sueltos funcionan, no hay que
     hacer nada más**.
   - intenta abrir un archivo `*.obb` dentro de esa ruta → hay que decidir entre (a) generar un `.obb`/zip
     válido con los assets ya extraídos (con `zip -0` o similar, sin comprimir para simplificar), y dejar
     que el motor lo abra con su propio parser de zlib, o (b) interceptar/hookear en `source/patch.c` la
     función específica de apertura del `.obb` (una vez identificada por dirección con `so_symbol`/log de
     crash) para redirigirla a lectura de archivo plano.
2. Si aparece necesidad de (b), es un hook puntual, no una reescritura: mismo patrón que
   `backstab-vita/loader/patch.c` (`hook_addr` sobre una función interna del `.so`).

## 5. Falta un archivo más en el layout de assets: `appConfig.txt`

`GetConfig(apkFilePath, key)` es nativa; ambos `libcocos2d.so` y `libgame_logic.so` contienen el string
literal `"appConfig.txt"`. En el APK real está en `assets/appConfig.txt` (fuera de la carpeta `Data`/OBB,
directo en la raíz de `assets/`). Las claves que el código Java consulta contra este archivo:
`DEVICE_SLEEP`, `ENABLE_FLURRY`, `ENABLE_APPCIRCLE`, `BUILD_FREEMIUM`, `ENABLE_CROSSPROMOTION`,
`ENABLE_APPRATER`, `SFR_OPERATOR`, `NOOK_BUILD`.

**Acción para la Fase 2 (ya ejecutada, hay que retocar):** copiar `assets/appConfig.txt` del APK a
`ux0_data/popclassic/appConfig.txt` (mismo nivel que los `.so`, no dentro de `Data/`, dado que
`apkFilePath` es la carpeta base, no la carpeta de datos gráficos). Ante la duda por el riesgo del §4,
copiarlo también dentro de `Data/appConfig.txt` — es un archivo de texto de unos pocos bytes, no cuesta
nada tenerlo en los dos lugares hasta confirmar en consola cuál ruta se usa.

Todos los flags anteriores son de servicios que no aplican a Vita (Flurry analytics, AppCircle ads,
cross-promotion, app rater, operador SFR, build Nook). **Recomendación:** en vez de replicar el contenido
real del `appConfig.txt` del APK, generar uno propio con todos esos flags en `false`/`0` para que el
código correspondiente ni se ejecute (evita tener que stubear las clases `CCFlurryUtils`, `CCShareUtils`,
etc. identificadas en la Fase 1 — si `GetConfig` devuelve `false` para todas, el juego nunca las llama).

## 6. Códigos de tecla que el juego ya sabe manejar (afecta la Fase 4/10 — mapeo de controles)

`Cocos2dxGLSurfaceView.onKeyDown/onKeyUp` **solo** reenvía al renderer (`handleKeyDown`/`handleKeyUp`,
que llaman a `nativeKeyDown`/`nativeKeyUp`) estos códigos de tecla de Android; cualquier otro código se
descarta:

| KeyEvent | Código | Uso probable |
|---|---:|---|
| `KEYCODE_BACK` | 4 | Volver / salir |
| `KEYCODE_MENU` | 82 | Menú |
| `KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT` | 19/20/21/22 | Movimiento / navegación de menú |
| `KEYCODE_DPAD_CENTER` | 23 | Confirmar |
| `KEYCODE_BUTTON_X` | 99 | Acción (probable ataque o salto) |
| `KEYCODE_BUTTON_Y` | 100 | Acción secundaria |
| `KEYCODE_BUTTON_L1` | 102 | Acción/gatillo izquierdo |
| `KEYCODE_BUTTON_R1` | 103 | Acción/gatillo derecho |

Nótese que **no** están `BUTTON_A`(96), `BUTTON_B`(97) ni `BUTTON_Z`(101) — el juego fue probado con un
gamepad que solo exponía X/Y/L1/R1 (o el desarrollador solo mapeó esos). Para la Fase 4/10, el loader debe
traducir el D-Pad y los botones físicos de la Vita a exactamente estos códigos vía `nativeKeyDown`/
`nativeKeyUp`; el significado gameplay real de cada botón (qué hace X vs Y) solo se puede confirmar
probando en consola, no por este análisis estático.

## 7. Acciones concretas que esto agrega/corrige en `plan_portabilidad.md`

1. **Fase 2:** agregar `appConfig.txt` (propio, con todos los flags en `false`) al layout, en la raíz de
   `popclassic/` y, por las dudas, también dentro de `Data/`.
2. **Fase 4:** usar las firmas reales de §2 para `nativeSetPaths` (3 strings) y `nativeInit` (recibe
   `screenWidth`/`screenHeight`, se llama en el "surface created", no en el arranque general) y respetar
   el orden completo de llamadas de §2, no solo "cargar los 3 `.so` y listo".
3. **Fase 4/9:** validar empíricamente el riesgo del §4 (assets sueltos vs. `.obb` como ZIP) apenas se
   tenga un primer build corriendo con logging de `io.c` activado, antes de invertir tiempo en más
   reversing manual de la lógica de `CCFileUtils`.
4. **Fase 4/10:** usar la tabla de key codes del §6 para el mapeo de controles físicos de la Vita, en vez
   de solo mapear START→`KEYCODE_BACK` como hacía el borrador original.
