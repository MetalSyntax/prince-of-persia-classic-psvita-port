#ifndef AUDIO_H
#define AUDIO_H

#include <falso_jni/FalsoJNI.h>
#include <vitasdk.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init();
void audio_shutdown();

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
