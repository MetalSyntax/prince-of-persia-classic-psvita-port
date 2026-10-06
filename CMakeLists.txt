#[[
  @file  CMakeLists.txt
  @brief Build configuration for the PS Vita port: compiler flags, target
         sources, VPK packaging, and the FTP deploy/dump helper targets.
  @note  See docs/comments/CMakeLists.txt.md for design rationale.
]]

cmake_minimum_required(VERSION 3.14)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for legacy SDK scripts")

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(so_loader C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration.
# "SND" marker + bumped version identify the audio build against the earlier
# silent-but-stable one (same TITLEID: installs over it, keeps saves).
# @see docs/comments/CMakeLists.txt.md#version-history-v0111v0120
set(VITA_APP_NAME "Prince of Persia Classic")
set(VITA_TITLEID "POPC00001")
set(VITA_VPKNAME "popclassic")
set(VITA_VERSION "01.45")
# NOTE: NO -s NP_COMMUNICATION_ID here on purpose. Rinnegatamante's trophy
# ports (Eldritch, Fahrenheit, FFIV, World of Goo, Late Shift) all ship
# param.sfo WITHOUT NP_COMMUNICATION_ID (only TITLE_ID + ATTRIBUTE2=12) and
# pass the bare 9-char base ("ELDR00001") to sceNpTrophyCreateContext.
# Shipping the field broke sceNpTrophySetupDialogInit on real HW (0x800204C4
# -> SETUP_REQUIRED, no system notification); see Docs/Fixes_Log.md.
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

## vitaGL vendorizada de Zenonia 3/4 (2026 master + mods de compat, system_app deshabilitada)
## SOFTFP_ABI=1 es OBLIGATORIO para evitar pantalla negra en PS Vita.
set(VITAGL_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vitaGL)
set(VITAGL_MAKE_FLAGS_STR "SOFTFP_ABI=1 NO_DEBUG=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1 HAVE_GLSL_UBOS=1 SAMPLERS_SPEEDHACK=1 DRAW_SPEEDHACK=2")
separate_arguments(VITAGL_MAKE_FLAGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS_STR}")
include(ExternalProject)
ExternalProject_Add(
  vitaGL_vendored
  SOURCE_DIR ${VITAGL_VENDOR_DIR}
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make ${VITAGL_MAKE_FLAGS}
  INSTALL_COMMAND ""
)
add_library(vitaGL_vendored_lib STATIC IMPORTED)
set_target_properties(vitaGL_vendored_lib PROPERTIES IMPORTED_LOCATION ${VITAGL_VENDOR_DIR}/libvitaGL.a)
add_dependencies(vitaGL_vendored_lib vitaGL_vendored)

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.3.15" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/popclassic/" CACHE STRING "Path to data (with trailing /)")
add_definitions(-DDATA_PATH="${DATA_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

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

# @see docs/comments/CMakeLists.txt.md#enable_verbose_log-rationale
# ON for the v01.41 "build con logs": trophy diagnostics (sceNpTrophy return
# codes, queue events, TRP game info) only exist behind DEBUG_SOLOADER.
option(ENABLE_VERBOSE_LOG "Log the full sequential execution trace (module load, audio/video/scene events) to the log file" ON)
if (ENABLE_VERBOSE_LOG)
  add_definitions(-DDEBUG_SOLOADER)
endif()

option(NO_SPLASHSCREEN "Disable the vitaGL boot splashscreen" ON)
if (NO_SPLASHSCREEN)
  add_definitions(-DSKIP_SPLASHSCREEN)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wl,--allow-multiple-definition -O3 -g -ffast-math -mfloat-abi=softfp -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++20 -Wno-write-strings -Wno-psabi")

# @see docs/comments/CMakeLists.txt.md#cmake_c_flags_debug--cmake_cxx_flags_debug--the-silent--o0-bug
set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG -D_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG -D_DEBUG")

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/audio.cpp
               source/video.cpp
               source/dynlib.c
               source/java.c
               source/patch.c
               source/trophies.c
               source/vitagl_no_splash.c
               source/input.c
               source/overlay.c
               source/vita_menu.c
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
               lib/so_util/so_util.c)

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           PUBLIC ${VITAGL_VENDOR_DIR}/source
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      stdc++
                      vitaGL_vendored_lib
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
                      SceNet_stub
                      SceNetCtl_stub
                      SceNpTrophy_stub
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
                ${CMAKE_SOURCE_DIR}/extras/trophy/POPC00001_00/TROPHY.TRP sce_sys/trophy/POPC00001_00/TROPHY.TRP
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
