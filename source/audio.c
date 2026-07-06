#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

static int bgm_port = -1;
static int sfx_port = -1;

void audio_init() {
    // Open Audio Ports
    bgm_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 4096, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    sfx_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, 1024, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    sceClibPrintf("Audio Initialized: BGM port %i, SFX port %i\n", bgm_port, sfx_port);
}

void audio_shutdown() {
    if (bgm_port >= 0) {
        sceAudioOutReleasePort(bgm_port);
        bgm_port = -1;
    }
    if (sfx_port >= 0) {
        sceAudioOutReleasePort(sfx_port);
        sfx_port = -1;
    }
}

// Cocos2dxMusic (BGM)
void Cocos2dxMusic_playBackgroundMusic(jmethodID id, va_list args) {

    jstring j_path = va_arg(args, jstring);
    jboolean isLoop = (jboolean)va_arg(args, int);
    sceClibPrintf("Cocos2dxMusic_playBackgroundMusic(%p, %i)\n", j_path, isLoop);
    // TODO: Implement Tremor vorbis decoding and thread streaming
}

void Cocos2dxMusic_stopBackgroundMusic(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_stopBackgroundMusic()\n");
}

void Cocos2dxMusic_pauseBackgroundMusic(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_pauseBackgroundMusic()\n");
}

void Cocos2dxMusic_resumeBackgroundMusic(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_resumeBackgroundMusic()\n");
}

void Cocos2dxMusic_rewindBackgroundMusic(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_rewindBackgroundMusic()\n");
}

jboolean Cocos2dxMusic_isBackgroundMusicPlaying(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_isBackgroundMusicPlaying()\n");
    return JNI_FALSE;
}

jfloat Cocos2dxMusic_getBackgroundMusicVolume(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxMusic_getBackgroundMusicVolume()\n");
    return 1.0f;
}

void Cocos2dxMusic_setBackgroundMusicVolume(jmethodID id, va_list args) {

    jfloat volume = (jfloat)va_arg(args, double);
    sceClibPrintf("Cocos2dxMusic_setBackgroundMusicVolume(%f)\n", (float)volume);
}

void Cocos2dxMusic_preloadBackgroundMusic(jmethodID id, va_list args) {

    jstring j_path = va_arg(args, jstring);
    sceClibPrintf("Cocos2dxMusic_preloadBackgroundMusic(%p)\n", j_path);
}


// Cocos2dxSound (SFX)
jint Cocos2dxSound_playEffect(jmethodID id, va_list args) {

    jstring j_path = va_arg(args, jstring);
    jboolean isLoop = (jboolean)va_arg(args, int);
    jfloat pitch = (jfloat)va_arg(args, double);
    jfloat pan = (jfloat)va_arg(args, double);
    jfloat gain = (jfloat)va_arg(args, double);
    sceClibPrintf("Cocos2dxSound_playEffect(%p, %i, %f, %f, %f)\n", j_path, isLoop, (float)pitch, (float)pan, (float)gain);
    // TODO: Implement Tremor vorbis decoding and output to SFX port
    return 1;
}

void Cocos2dxSound_stopEffect(jmethodID id, va_list args) {

    jint soundId = va_arg(args, jint);
    sceClibPrintf("Cocos2dxSound_stopEffect(%i)\n", soundId);
}

void Cocos2dxSound_stopAllEffects(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxSound_stopAllEffects()\n");
}

void Cocos2dxSound_pauseEffect(jmethodID id, va_list args) {

    jint soundId = va_arg(args, jint);
    sceClibPrintf("Cocos2dxSound_pauseEffect(%i)\n", soundId);
}

void Cocos2dxSound_resumeEffect(jmethodID id, va_list args) {

    jint soundId = va_arg(args, jint);
    sceClibPrintf("Cocos2dxSound_resumeEffect(%i)\n", soundId);
}

void Cocos2dxSound_pauseAllEffects(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxSound_pauseAllEffects()\n");
}

void Cocos2dxSound_resumeAllEffects(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxSound_resumeAllEffects()\n");
}

jint Cocos2dxSound_preloadEffect(jmethodID id, va_list args) {

    jstring j_path = va_arg(args, jstring);
    sceClibPrintf("Cocos2dxSound_preloadEffect(%p)\n", j_path);
    return 1;
}

void Cocos2dxSound_unloadEffect(jmethodID id, va_list args) {

    jstring j_path = va_arg(args, jstring);
    sceClibPrintf("Cocos2dxSound_unloadEffect(%p)\n", j_path);
}

jfloat Cocos2dxSound_getEffectsVolume(jmethodID id, va_list args) {
    sceClibPrintf("Cocos2dxSound_getEffectsVolume()\n");
    return 1.0f;
}

void Cocos2dxSound_setEffectsVolume(jmethodID id, va_list args) {

    jfloat volume = (jfloat)va_arg(args, double);
    sceClibPrintf("Cocos2dxSound_setEffectsVolume(%f)\n", (float)volume);
}
