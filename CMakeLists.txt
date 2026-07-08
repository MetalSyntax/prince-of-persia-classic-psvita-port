cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(so_loader C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
# "SND" marker + bumped version identify the audio build against the earlier
# silent-but-stable one (same TITLEID: installs over it, keeps saves).
# v01.11 = custom sceAudioOut mixer; SoLoud was removed (see Fixes_Log #10).
# v01.12 = verbose sequential logging always on, soft-limited audio output,
#          native SceAvPlayer cutscene playback (see Fixes_Log #11).
# v01.13 = fixed a v01.12 regression: FALSOJNI_DEBUGLEVEL=0 logged every
#          primitive JNI call (100s/sec) each paying a full file open+write+
#          close, tanking the framerate and starving the audio thread of CPU
#          (see Fixes_Log #12).
# v01.14 = v01.13 wasn't enough: a pre-existing, always-on FalsoJNI error path
#          (GetFloatArrayRegion/GetArrayLength on an untracked array, once
#          per touch-active frame) fired hundreds of times/session once it
#          started reaching the log file in v01.12. Fixed at the source
#          (length==0 and NULL-array probes are legitimate JNI no-ops, not
#          errors) plus consecutive-duplicate suppression in the logger as
#          defense in depth, plus restored a real missing method
#          registration (preloadEffect via CallStaticVoidMethod) found along
#          the way (see Fixes_Log #13).
# v01.15 = made source/main.c's "input tick" debug log edge-triggered (only
#          on a touch/pad state CHANGE, not once per frame with any active
#          touch) -- the logger's duplicate suppression from v01.14 only
#          collapsed exact repeats, so jitter in touch.reportNum between
#          frame (see Fixes_Log #14).
# v01.16 = Fixed zero-volume bug on all SFX (footsteps, hits, environment). The Android Cocos2dx 2.0
#          JNI signature for playEffect is (Ljava/lang/String;Z)I, meaning it does NOT pass pitch,
#          pan, or gain. The variadic va_arg was reading garbage uninitialized memory from the stack
#          for these missing parameters, resulting in a gain of 0.0f, which silenced all sound
#          effects. They are now hardcoded to 1.0f/0.0f/1.0f (see Fixes_Log #15).
set(VITA_APP_NAME "Prince of Persia Classic SND")
set(VITA_TITLEID "POPC00001")
set(VITA_VPKNAME "popclassic_video_fix_nologs")
set(VITA_VERSION "01.16")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/popclassic/" CACHE STRING "Path to data (with trailing /)")
add_definitions(-DDATA_PATH="${DATA_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

# stb_vorbis decodes only from memory buffers loaded via sceIo (audio.cpp):
# compiling out its stdio API makes the FILE*-mixing crash class impossible
# to reintroduce by accident (see Docs/Fixes_Log.md #10).
add_definitions(-DSTB_VORBIS_NO_STDIO)

option(EMULATOR_BUILD "Relax hardware-only safety checks (e.g. kubridge presence) for Vita3K testing" OFF)
if (EMULATOR_BUILD)
  add_definitions(-DEMULATOR_BUILD)
endif()

set(SHADER_FORMAT "GLSL" CACHE STRING "Preferred shader format (one of 'GLSL', 'CG', 'GXP')")
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" ON)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Verbose sequential logging (module load, our own audio/video/scene
# milestones -- see Docs/Fixes_Log.md #11). This used to be tied to
# CMAKE_BUILD_TYPE STREQUAL "Debug", but build_and_install.sh always builds
# Release, so in practice l_debug/l_info/l_success were NEVER compiled in and
# only l_error/l_fatal ever reached the log file -- exactly the "only shows
# crashes" complaint. Decoupled from build type: -O3 is hardcoded in
# CMAKE_C_FLAGS below regardless, so there's no perf reason to tie this to
# Debug. Turn OFF only if the log file is getting too large to page through.
#
# Deliberately does NOT touch FALSOJNI_DEBUGLEVEL (left at its own default,
# FALSOJNI_DEBUG_INFO): that "ALL" tier logs literally every primitive JNI
# call (NewStringUTF, FindClass, GetEnv, DeleteLocalRef...) which FIRES
# CONSTANTLY during normal engine operation. A first attempt at enabling it
# produced 1600+ log lines in the first few seconds alone, each paying a full
# sceIoOpen+Write+Close -- this made the game unplayably slow and, by
# stalling the main thread under the shared log mutex, starved the
# real-time audio mixer thread of CPU right when it needed its ~46ms window,
# which is what made the audio sound harder/more distorted too (see
# Fixes_Log.md #12). None of that per-primitive tracing is needed: every
# JNI entry point INTO our own code (audio, video, dialogs, bitmaps, etc.)
# already logs itself directly via l_debug in java.c/audio.cpp/video.cpp,
# independent of FalsoJNI's internal dispatcher.
option(ENABLE_VERBOSE_LOG "Log the full sequential execution trace (module load, audio/video/scene events) to the log file" OFF)
if (ENABLE_VERBOSE_LOG)
  add_definitions(-DDEBUG_SOLOADER)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wl,--allow-multiple-definition -O3 -g -ffast-math -mfloat-abi=softfp -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++20 -Wno-write-strings -Wno-psabi")

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/audio.cpp
               source/video.cpp
               source/dynlib.c
               source/java.c
               source/patch.c
               source/reimpl/bits/_ctype.c
               source/reimpl/egl.c
               source/reimpl/errno.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/pthr.c
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/dialog.c
               source/utils/glutil.c
               source/utils/init.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/utils.c
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c
               lib/stb/stb_vorbis.c)

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceAvPlayer_stub
                      SceSysmodule_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      vorbisidec
                      ogg
                      )

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                ${CMAKE_SOURCE_DIR}/extras/fonts/DejaVuSans.ttf DejaVuSans.ttf
                )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
