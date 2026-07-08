#ifndef AUDIO_H
#define AUDIO_H

#include <falso_jni/FalsoJNI.h>
#include <vitasdk.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init();
void audio_shutdown();

// Used by video.cpp: cutscenes have their own audio track, so the game's own
// BGM is paused (not stopped -- resumed exactly where it left off) for the
// duration of video playback instead of fighting it for the same output.
void audio_pause_bgm_for_video();
void audio_resume_bgm_after_video();

// Cocos2dxMusic (BGM)
void Cocos2dxMusic_playBackgroundMusic(jmethodID id, va_list args);
void Cocos2dxMusic_stopBackgroundMusic(jmethodID id, va_list args);
void Cocos2dxMusic_pauseBackgroundMusic(jmethodID id, va_list args);
void Cocos2dxMusic_resumeBackgroundMusic(jmethodID id, va_list args);
void Cocos2dxMusic_rewindBackgroundMusic(jmethodID id, va_list args);
jboolean Cocos2dxMusic_isBackgroundMusicPlaying(jmethodID id, va_list args);
jfloat Cocos2dxMusic_getBackgroundMusicVolume(jmethodID id, va_list args);
void Cocos2dxMusic_setBackgroundMusicVolume(jmethodID id, va_list args);
void Cocos2dxMusic_preloadBackgroundMusic(jmethodID id, va_list args);

// Cocos2dxSound (SFX)
jint Cocos2dxSound_playEffect(jmethodID id, va_list args);
void Cocos2dxSound_stopEffect(jmethodID id, va_list args);
void Cocos2dxSound_stopAllEffects(jmethodID id, va_list args);
void Cocos2dxSound_pauseEffect(jmethodID id, va_list args);
void Cocos2dxSound_resumeEffect(jmethodID id, va_list args);
void Cocos2dxSound_pauseAllEffects(jmethodID id, va_list args);
void Cocos2dxSound_resumeAllEffects(jmethodID id, va_list args);
jint Cocos2dxSound_preloadEffect(jmethodID id, va_list args);
void Cocos2dxSound_unloadEffect(jmethodID id, va_list args);
jfloat Cocos2dxSound_getEffectsVolume(jmethodID id, va_list args);
void Cocos2dxSound_setEffectsVolume(jmethodID id, va_list args);

#ifdef __cplusplus
}
#endif

#endif
