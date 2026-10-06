# vitaGL vendorizado en este repo

Upstream: https://github.com/Rinnegatamante/vitaGL
Commit base: cd3791e29ff7f1c0ab349f12c7231f4871ce6a75
Vendorizado el: 2026-09-02

## Por que vendorizado y no submodulo

Este arbol tiene modificaciones locales IMPRESCINDIBLES para compilar contra
el vitasdk de este proyecto. Un submodulo solo guarda el SHA de upstream, asi
que un clon fresco no compilaba. Ver la lista de cambios abajo.

`samples/` se elimino (28MB, solo lo usa el target `make samples`, que este
proyecto nunca invoca). Los artefactos de build (`*.o`, `libvitaGL.a`) estan
gitignorados: los genera `build_vitagl.sh` + `make` desde CMakeLists.txt.

## Modificaciones locales respecto de cd3791e29ff7f1c0ab349f12c7231f4871ce6a75

```
 Makefile        |   2 +-
 source/egl.c    | 402 --------------------------------------------------------
 source/shared.h |  26 ++++
 source/vgl.c    |   2 +
 4 files changed, 29 insertions(+), 403 deletions(-)
```

Detalle:

- `source/shader.h`/`source/shared.h`: agrega defines `SCE_GXM_*` que faltan en
  este vitasdk. SIN ESTO vitaGL NO COMPILA.
- `source/egl.c`: eliminado. Evita simbolos EGL duplicados contra el
  `source/reimpl/egl.c` de este port.
- `Makefile`: `git rev-parse` con fallback a "unknown" (ya no hay repo git aca).
- `source/vgl.c`: ver el diff completo abajo.
- `build_vitagl.sh`: script propio de este port (NO es de upstream). Lo invoca
  CMakeLists.txt para hacer `make clean` solo cuando cambian los flags.

## Diff completo contra upstream

```diff
diff --git a/Makefile b/Makefile
index fabb381..37523df 100644
--- a/Makefile
+++ b/Makefile
@@ -13,7 +13,7 @@ CC      = $(PREFIX)-gcc
 CXX     = $(PREFIX)-g++
 AR      = $(PREFIX)-gcc-ar
 CFLAGS  = -g -Wl,-q -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -Wno-incompatible-pointer-types -Wno-stringop-overflow -mfp16-format=ieee \
-	-DVGL_GIT_HASH='"$(shell git rev-parse --short HEAD)"' -Isource
+	-DVGL_GIT_HASH='"$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)"' -Isource
 ASFLAGS = $(CFLAGS)
 
 ifeq ($(SOFTFP_ABI),1)
diff --git a/source/egl.c b/source/egl.c
deleted file mode 100644
index 60fd386..0000000
--- a/source/egl.c
+++ /dev/null
@@ -1,402 +0,0 @@
-/*
- * This file is part of vitaGL
- * Copyright 2017, 2018, 2019, 2020 Rinnegatamante
- *
- * This program is free software: you can redistribute it and/or modify
- * it under the terms of the GNU Lesser General Public License as published
- * by the Free Software Foundation, version 3 of the License, or (at your
- * option) any later version.
- *
- * This program is distributed in the hope that it will be useful, but
- * WITHOUT ANY WARRANTY; without even the implied warranty of
- * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
- * General Public License for more details.
- *
- * You should have received a copy of the GNU General Public License
- * along with this program. If not, see <http://www.gnu.org/licenses/>.
- */
-
-#include "shared.h"
-#include "vitaGL.h"
-
-#define DISPLAY_PHYSICAL_WIDTH (110.7f)
-#define DISPLAY_PHYSICAL_HEIGHT (62.7f)
-
-// Default bogus values used for default objects
-enum {
-	EGL_NULL_CTX,
-	EGL_DEFAULT_CTX,
-	EGL_DEFAULT_SURF,
-	EGL_DEFAULT_DISP,
-	EGL_DEFAULT_CFG
-};
-
-#ifdef LOG_ERRORS
-static char *get_egl_error_literal(uint32_t code) {
-	switch (code) {
-	case EGL_BAD_PARAMETER:
-		return "EGL_BAD_PARAMETER";
-	case EGL_BAD_ATTRIBUTE:
-		return "EGL_BAD_ATTRIBUTE";
-	case EGL_BAD_CONTEXT:
-		return "EGL_BAD_CONTEXT";
-	case EGL_BAD_SURFACE:
-		return "EGL_BAD_SURFACE";
-	default:
-		return "Unknown Error";
-	}
-}
-#endif
-
-#define SET_EGL_ERROR(x, y) \
-	vgl_log("%s:%d: %s set %s\n", __FILE__, __LINE__, __func__, get_egl_error_literal(x)); \
-	egl_error = x; \
-	return y;
-	
-#define EGL_RET(y) \
-	egl_error = EGL_SUCCESS; \
-	return y;
-
-static EGLint egl_error = EGL_SUCCESS;
-static EGLenum rend_api = EGL_OPENGL_ES_API;
-
-// EGL implementation
-
-EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
-	if (major)
-		*major = 2;
-	if (minor)
-		*minor = 2;
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value) {
-	switch (attribute) {
-	case EGL_CONFIG_ID:
-		*value = 0;
-		break;
-	case EGL_CONTEXT_CLIENT_TYPE:
-		*value = rend_api;
-		break;
-	case EGL_CONTEXT_CLIENT_VERSION:
-		*value = 2;
-		break;
-	case EGL_RENDER_BUFFER:
-		*value = EGL_BACK_BUFFER;
-		break;
-	default:
-		SET_EGL_ERROR(EGL_BAD_ATTRIBUTE, EGL_FALSE)
-	}
-	
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface eglSurface, EGLint attribute, EGLint *value) {
-	switch (attribute) {
-	case EGL_CONFIG_ID:
-		*value = 0;
-		break;
-	case EGL_WIDTH:
-		*value = DISPLAY_WIDTH;
-		break;
-	case EGL_HEIGHT:
-		*value = DISPLAY_HEIGHT;
-		break;
-	case EGL_TEXTURE_FORMAT:
-		*value = EGL_TEXTURE_RGBA;
-		break;
-	case EGL_TEXTURE_TARGET:
-		*value = EGL_TEXTURE_2D;
-		break;
-	case EGL_SWAP_BEHAVIOR:
-		*value = EGL_BUFFER_PRESERVED;
-		break;
-	case EGL_LARGEST_PBUFFER:
-	case EGL_MIPMAP_TEXTURE:
-		*value = EGL_FALSE;
-		break;
-	case EGL_MIPMAP_LEVEL:
-		*value = 0;
-		break;
-	case EGL_MULTISAMPLE_RESOLVE:
-		*value = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
-		break;
-	case EGL_HORIZONTAL_RESOLUTION:
-		*value = (EGLint)((DISPLAY_WIDTH_FLOAT / DISPLAY_PHYSICAL_WIDTH) * EGL_DISPLAY_SCALING);
-		break;
-	case EGL_VERTICAL_RESOLUTION:
-		*value = (EGLint)((DISPLAY_HEIGHT_FLOAT / DISPLAY_PHYSICAL_HEIGHT) * EGL_DISPLAY_SCALING);
-		break;
-	case EGL_PIXEL_ASPECT_RATIO:
-		*value = EGL_DISPLAY_SCALING;
-		break;
-	case EGL_RENDER_BUFFER:
-		*value = EGL_BACK_BUFFER;
-		break;
-	case EGL_VG_COLORSPACE:
-		*value = EGL_VG_COLORSPACE_LINEAR;
-		break;
-	case EGL_VG_ALPHA_FORMAT:
-		*value = EGL_VG_ALPHA_FORMAT_NONPRE;
-		break;
-	case EGL_TIMESTAMPS_ANDROID:
-		*value = EGL_FALSE;
-		break;
-	default:
-		SET_EGL_ERROR(EGL_BAD_ATTRIBUTE, EGL_FALSE)
-	}
-
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute, EGLint *value) {
-	switch (attribute) {
-	case EGL_ALPHA_SIZE:
-		*value = 8;
-		break;
-	case EGL_ALPHA_MASK_SIZE:
-		*value = 8;
-		break;
-	case EGL_BIND_TO_TEXTURE_RGB:
-		*value = EGL_TRUE;
-		break;
-	case EGL_BIND_TO_TEXTURE_RGBA:
-		*value = EGL_TRUE;
-		break;
-	case EGL_BLUE_SIZE:
-		*value = 8;
-		break;
-	case EGL_BUFFER_SIZE:
-		*value = 32;
-		break;
-	case EGL_COLOR_BUFFER_TYPE:
-		*value = EGL_RGB_BUFFER;
-		break;
-	case EGL_CONFIG_CAVEAT:
-		*value = EGL_NONE;
-		break;
-	case EGL_CONFIG_ID:
-		*value = 0;
-		break;
-	case EGL_CONFORMANT:
-		*value = 0;
-		break;
-	case EGL_DEPTH_SIZE:
-		*value = 32;
-		break;
-	case EGL_GREEN_SIZE:
-		*value = 8;
-		break;
-	case EGL_LEVEL:
-		*value = 0;
-		break;
-	case EGL_LUMINANCE_SIZE:
-		*value = 0;
-		break;
-	case EGL_MAX_PBUFFER_WIDTH:
-		*value = 0;
-		break;
-	case EGL_MAX_PBUFFER_HEIGHT:
-		*value = 0;
-		break;
-	case EGL_MAX_PBUFFER_PIXELS:
-		*value = 0;
-		break;
-	case EGL_MAX_SWAP_INTERVAL:
-		*value = 65535;
-		break;
-	case EGL_MIN_SWAP_INTERVAL:
-		*value = 1;
-		break;
-	case EGL_NATIVE_RENDERABLE:
-		*value = EGL_FALSE;
-		break;
-	case EGL_NATIVE_VISUAL_ID:
-		*value = 0;
-		break;
-	case EGL_NATIVE_VISUAL_TYPE:
-		*value = 0;
-		break;
-	case EGL_RED_SIZE:
-		*value = 8;
-		break;
-	case EGL_RENDERABLE_TYPE:
-		*value = EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_BIT;
-		break;
-	case EGL_SAMPLE_BUFFERS:
-		*value = 0;
-		break;
-	case EGL_SAMPLES:
-		*value = 1;
-		break;
-	case EGL_STENCIL_SIZE:
-		*value = 8;
-		break;
-	case EGL_SURFACE_TYPE:
-		*value = EGL_WINDOW_BIT;
-		break;
-	case EGL_TRANSPARENT_TYPE:
-		*value = EGL_NONE;
-		break;
-	case EGL_TRANSPARENT_RED_VALUE:
-		*value = 0;
-		break;
-	case EGL_TRANSPARENT_GREEN_VALUE:
-		*value = 0;
-		break;
-	case EGL_TRANSPARENT_BLUE_VALUE:
-		*value = 0;
-		break;
-	default:
-		SET_EGL_ERROR(EGL_BAD_ATTRIBUTE, EGL_FALSE)
-	}
-
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
-#ifndef SKIP_ERROR_HANDLING
-	if (!num_config) {
-		SET_EGL_ERROR(EGL_BAD_PARAMETER, EGL_FALSE)
-	}
-#endif
-	*num_config = 1;
-	if (configs)
-		*configs = (EGLConfig)EGL_DEFAULT_CFG;
-
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglGetConfigs(EGLDisplay display, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
-#ifndef SKIP_ERROR_HANDLING
-	if (!num_config) {
-		SET_EGL_ERROR(EGL_BAD_PARAMETER, EGL_FALSE)
-	}
-#endif
-	*num_config = 1;
-	if (configs && config_size > 0) {
-		*configs = (EGLConfig)EGL_DEFAULT_CFG;
-	}
-
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval) {
-	vsync_interval = interval;
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
-	vglSwapBuffers(GL_FALSE);
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglBindAPI(EGLenum api) {
-	switch (api) {
-	case EGL_OPENGL_API:
-	case EGL_OPENGL_ES_API:
-		rend_api = api;
-		egl_error = EGL_SUCCESS;
-		break;
-	default:
-		SET_EGL_ERROR(EGL_BAD_PARAMETER, EGL_FALSE);
-	}
-	
-	EGL_RET(EGL_TRUE)
-}
-
-EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
-	EGL_RET((EGLContext)EGL_DEFAULT_CTX)
-}
-
-EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
-#ifndef SKIP_ERROR_HANDLING
-	if (ctx != (EGLContext)EGL_DEFAULT_CTX) {
-		SET_EGL_ERROR(EGL_BAD_CONTEXT, EGL_FALSE);
-	}
-#endif
-	EGL_RET(EGL_TRUE)
-}
-
-EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, void * win, const EGLint *attrib_list) {
-	EGL_RET((EGLSurface)EGL_DEFAULT_SURF)
-}
-
-EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
-#ifndef SKIP_ERROR_HANDLING
-	if (surface != (EGLSurface)EGL_DEFAULT_SURF) {
-		SET_EGL_ERROR(EGL_BAD_SURFACE, EGL_FALSE);
-	}
-#endif
-	EGL_RET(EGL_TRUE)
-}
-
-EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
-	EGL_RET(EGL_TRUE)
-}
-
-EGLenum eglQueryAPI(void) {
-	EGL_RET(rend_api)
-}
-
-EGLBoolean eglTerminate(EGLDisplay dpy) {
-	EGL_RET(EGL_TRUE)
-}
-
-EGLContext eglGetCurrentContext(void) {
-	EGL_RET((EGLContext)EGL_DEFAULT_CTX)
-}
-
-EGLint eglGetError(void) {
-	EGLint ret = egl_error;
-	EGL_RET(ret)
-}
-
-char const * eglQueryString(EGLDisplay display, EGLint name) {
-	switch (name) {
-	case EGL_CLIENT_APIS:
-		EGL_RET("OpenGL OpenGL_ES");
-	case EGL_VENDOR:
-		EGL_RET("Rinnegatamante");
-	case EGL_VERSION:
-		EGL_RET("2.2 VitaGL");
-	case EGL_EXTENSIONS:
-		EGL_RET("EGL_KHR_image "
-			"EGL_KHR_image_base "
-			"EGL_KHR_image_pixmap "
-			"EGL_KHR_gl_texture_2D_image "
-			"EGL_KHR_gl_texture_cubemap_image "
-			"EGL_KHR_gl_renderbuffer_image "
-			"EGL_KHR_fence_sync "
-			"EGL_NV_system_time "
-			"EGL_ANDROID_image_native_buffer ");
-	default:
-		SET_EGL_ERROR(EGL_BAD_PARAMETER, NULL);
-	}
-}
-
-void (*eglGetProcAddress(char const *procname))(void) {
-	EGL_RET(vglGetProcAddress(procname));
-}
-
-EGLDisplay eglGetDisplay(NativeDisplayType native_display) {
-	if (native_display == EGL_DEFAULT_DISPLAY) {
-		EGL_RET((EGLDisplay)EGL_DEFAULT_DISP);
-	} else {
-		EGL_RET(EGL_NO_DISPLAY)
-	}
-}
-
-EGLSurface eglGetCurrentSurface(EGLint readdraw) {
-	EGL_RET((EGLSurface)EGL_DEFAULT_SURF)
-}
-
-EGLuint64 eglGetSystemTimeFrequencyNV(void) {
-	EGL_RET((EGLuint64)sceRtcGetTickResolution())
-}
-
-EGLuint64 eglGetSystemTimeNV(void) {
-	SceRtcTick t;
-	sceRtcGetCurrentTick(&t);
-	EGL_RET(t.tick)
-}
diff --git a/source/shared.h b/source/shared.h
index 3f7e5a4..9952036 100644
--- a/source/shared.h
+++ b/source/shared.h
@@ -275,6 +275,32 @@ extern int NEW_DISPLAY_HEIGHT; // Requested new display height in pixels
 #include <psp2/common_dialog.h>
 #include <psp2/display.h>
 #include <psp2/gxm.h>
+
+#ifndef SCE_GXM_INITIALIZE_FLAG_DEFAULT
+#define SCE_GXM_INITIALIZE_FLAG_DEFAULT 0x00000000u
+#endif
+#ifndef SCE_GXM_INITIALIZE_FLAG_PB_LPDDR
+#define SCE_GXM_INITIALIZE_FLAG_PB_LPDDR 0x00000001u
+#endif
+#ifndef SCE_GXM_INITIALIZE_FLAG_SHARED_SYNC
+#define SCE_GXM_INITIALIZE_FLAG_SHARED_SYNC 0x00000002u
+#endif
+#ifndef SCE_GXM_INITIALIZE_FLAG_SHAREDPB_CREATE
+#define SCE_GXM_INITIALIZE_FLAG_SHAREDPB_CREATE 0x00000004u
+#endif
+#ifndef SCE_GXM_INITIALIZE_FLAG_SHAREDPB_OPEN
+#define SCE_GXM_INITIALIZE_FLAG_SHAREDPB_OPEN 0x00000008u
+#endif
+#ifndef SCE_GXM_INITIALIZE_FLAG_EXTENDED_FORMAT
+#define SCE_GXM_INITIALIZE_FLAG_EXTENDED_FORMAT 0x00000010u
+#endif
+#ifndef SCE_GXM_TEXTURE_BASE_FORMAT_ETC1
+#define SCE_GXM_TEXTURE_BASE_FORMAT_ETC1 0x84000000
+#endif
+#ifndef SCE_GXM_TEXTURE_FORMAT_ETC1_1BGR
+#define SCE_GXM_TEXTURE_FORMAT_ETC1_1BGR SCE_GXM_TEXTURE_BASE_FORMAT_ETC1
+#endif
+
 #include <psp2/io/stat.h>
 #include <psp2/kernel/clib.h>
 #include <psp2/kernel/dmac.h>
diff --git a/source/vgl.c b/source/vgl.c
index 512347a..d9e53e0 100644
--- a/source/vgl.c
+++ b/source/vgl.c
@@ -840,5 +840,7 @@ void vglSetShaderCachePath(const char *path) {
 }
 
 void vglSetShaderAssociationPath(const char *path) {
+#ifdef HAVE_RAZOR
 	shark_set_shader_association_path(path);
+#endif
 }
```
