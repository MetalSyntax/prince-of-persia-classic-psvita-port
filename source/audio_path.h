/**
 * @file  audio_path.h
 * @brief Pure path-translation helpers mapping Android asset-request paths
 *        to the real .mp3 files on the memory card.
 *        @note See docs/comments/audio_path.h.md for design rationale.
 */

#ifndef AUDIO_PATH_H
#define AUDIO_PATH_H

//! @see docs/comments/audio_path.h.md#path-translation-design

#include <string>

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/popclassic/"
#endif

/** @brief Replace whatever extension the request has (.mp3/.m4a/.mp4/none)
 *         with .mp3. */
static inline std::string audio_swap_ext_to_mp3(const std::string &p) {
    size_t slash = p.find_last_of('/');
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return p + ".mp3";
    return p.substr(0, dot) + ".mp3";
}

/** @brief Map any path the game may request to the real .mp3 under
 *         DATA_PATH "Data/". Anchors on the "Audio/" component so it
 *         tolerates "Extra/", "assets/Extra/", leading slashes, or an
 *         already-translated "Data/Audio/..." input. */
static inline std::string sanitize_audio_path(const char *raw) {
    std::string p = raw ? raw : "";

    std::string rel;
    size_t audio = p.find("Audio/");
    if (audio != std::string::npos) {
        rel = p.substr(audio); // "Audio/Music/x.mp3"
    } else {
        size_t start = p.find_first_not_of('/');
        rel = (start == std::string::npos) ? "" : p.substr(start);
        static const char *prefixes[] = { "assets/", "Extra/", "Data/" };
        for (const char *pre : prefixes) {
            size_t n = std::string(pre).size();
            if (rel.compare(0, n, pre) == 0) {
                rel = rel.substr(n);
                break;
            }
        }
    }

    return std::string(DATA_PATH) + "Data/" + audio_swap_ext_to_mp3(rel);
}

/** @brief Alternate location some assets ship in (resolution-suffixed data
 *         folder). Tried only after the primary sanitized path fails to
 *         open. */
static inline std::string audio_fallback_path(const std::string &sanitized) {
    std::string s = sanitized;
    size_t pos = s.find("Data/");
    if (pos != std::string::npos)
        s.replace(pos, 5, "Data_960_576/");
    return s;
}

#endif // AUDIO_PATH_H
