/**
 * @file  video.h
 * @brief Cutscene video playback interface for the PS Vita so-loader.
 *        @note See docs/comments/video.h.md for design rationale.
 */

#ifndef VIDEO_H
#define VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

void video_init();
void video_shutdown();

/** @brief Plays the cutscene the game requested; blocks until it finishes,
 *         is skipped (Cross/Start), or fails to open/decode -- but always
 *         returns.
 *  @note See docs/comments/video.h.md#video_play--guaranteed-return-no-hang-contract */
void video_play(const char *raw);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_H
