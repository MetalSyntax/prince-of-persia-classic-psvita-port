# Enlaces JNI (Java Native Interface) y FalsoJNI

Dado que la PS Vita no ejecuta una máquina virtual de Android (Dalvik/ART), el cargador debe emular las estructuras básicas de JNI (`JavaVM` y `JNIEnv`) y redirigir las llamadas del motor a través de una tabla de funciones simuladas (FalsoJNI).

## Resolución de Puntos de Entrada de Cocos2d-x

Las llamadas nativas del ciclo de vida y eventos del motor Cocos2d-x se localizan en las librerías dinámicas y se resuelven de la siguiente manera:

```c
int (* JNI_OnLoad)(void *vm, void *reserved) = (void *)so_symbol(&game_mod, "JNI_OnLoad");

void (* Engine_Init)(void *env, void *obj, int w, int h) = 
    (void *)so_symbol(&game_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");

void (* Engine_Render)(void *env, void *obj) = 
    (void *)so_symbol(&game_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
```

## Simulación de Entrada (Touches & Keys)

Para simular interacciones táctiles de Android usando la pantalla táctil de la PS Vita (`sceTouch`):

```c
// Al presionar la pantalla
Engine_TouchesBegin(fake_env, NULL, touch_id, x, y);

// Al mover el dedo
Engine_TouchesMove(fake_env, NULL, touch_id, x, y);

// Al levantar el dedo
Engine_TouchesEnd(fake_env, NULL, touch_id, x, y);
```

Para botones físicos mapeados a KeyEvents de Android:

```c
// Enviar KeyEvent (por ejemplo: 4 = KEYCODE_BACK) al motor
Engine_KeyDown(fake_env, NULL, 4);
```
