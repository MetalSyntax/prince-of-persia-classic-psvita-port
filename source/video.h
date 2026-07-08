#ifndef VIDEO_H
#define VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

void video_init();
void video_shutdown();

// Plays the cutscene the game requested (raw is whatever path/string the
// game's playVideo JNI call carried -- see java.c). Blocks until the video
// finishes, the player skips it (Cross/Start), or it fails to open/decode.
// Always returns -- never hangs -- so the caller can unconditionally fire
// onVideoCompleted() afterwards regardless of outcome (the hang this
// guards against is the one fixed in plan_portabilidad.md §9.20).
void video_play(const char *raw);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_H
