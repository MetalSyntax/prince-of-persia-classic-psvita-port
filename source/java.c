#include <falso_jni/FalsoJNI_Impl.h>
#include "audio.h"

/*
 * JNI Methods
*/

NameToMethodID nameToMethodId[] = {
    // Cocos2dxMusic
    { 10, "playBackgroundMusic", METHOD_TYPE_VOID },
    { 11, "stopBackgroundMusic", METHOD_TYPE_VOID },
    { 12, "pauseBackgroundMusic", METHOD_TYPE_VOID },
    { 13, "resumeBackgroundMusic", METHOD_TYPE_VOID },
    { 14, "rewindBackgroundMusic", METHOD_TYPE_VOID },
    { 15, "isBackgroundMusicPlaying", METHOD_TYPE_BOOLEAN },
    { 16, "getBackgroundMusicVolume", METHOD_TYPE_FLOAT },
    { 17, "setBackgroundMusicVolume", METHOD_TYPE_VOID },
    { 18, "preloadBackgroundMusic", METHOD_TYPE_VOID },

    // Cocos2dxSound
    { 20, "playEffect", METHOD_TYPE_INT },
    { 21, "stopEffect", METHOD_TYPE_VOID },
    { 22, "stopAllEffects", METHOD_TYPE_VOID },
    { 23, "pauseEffect", METHOD_TYPE_VOID },
    { 24, "resumeEffect", METHOD_TYPE_VOID },
    { 25, "pauseAllEffects", METHOD_TYPE_VOID },
    { 26, "resumeAllEffects", METHOD_TYPE_VOID },
    { 27, "preloadEffect", METHOD_TYPE_INT },
    { 28, "unloadEffect", METHOD_TYPE_VOID },
    { 29, "getEffectsVolume", METHOD_TYPE_FLOAT },
    { 30, "setEffectsVolume", METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { 15, Cocos2dxMusic_isBackgroundMusicPlaying },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {
    { 16, Cocos2dxMusic_getBackgroundMusicVolume },
    { 29, Cocos2dxSound_getEffectsVolume },
};
MethodsInt methodsInt[] = {
    { 20, Cocos2dxSound_playEffect },
    { 27, Cocos2dxSound_preloadEffect },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 10, Cocos2dxMusic_playBackgroundMusic },
    { 11, Cocos2dxMusic_stopBackgroundMusic },
    { 12, Cocos2dxMusic_pauseBackgroundMusic },
    { 13, Cocos2dxMusic_resumeBackgroundMusic },
    { 14, Cocos2dxMusic_rewindBackgroundMusic },
    { 17, Cocos2dxMusic_setBackgroundMusicVolume },
    { 18, Cocos2dxMusic_preloadBackgroundMusic },
    { 21, Cocos2dxSound_stopEffect },
    { 22, Cocos2dxSound_stopAllEffects },
    { 23, Cocos2dxSound_pauseEffect },
    { 24, Cocos2dxSound_resumeEffect },
    { 25, Cocos2dxSound_pauseAllEffects },
    { 26, Cocos2dxSound_resumeAllEffects },
    { 28, Cocos2dxSound_unloadEffect },
    { 30, Cocos2dxSound_setEffectsVolume },
};

/*
 * JNI Fields
*/

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT }, 
		{ 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
