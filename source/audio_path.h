#ifndef AUDIO_PATH_H
#define AUDIO_PATH_H

// Pure path-translation helpers for the audio engine. No Vita/SoLoud
// dependencies on purpose: the exact same code is compiled and unit-tested
// on the host (see extras/tests/test_audio_path.cpp) before every build.
//
// The game requests Android asset paths like:
//     Extra/Audio/Music/POP_BGM_Menu.mp3
//     Extra/Audio/SFX/Enemies/Jaffar/94_jaffar_fight.mp4   (audio, not video!)
//     Extra/Audio/Music/95_boss_fight_2.m4a
// while the unpacked assets on the memory card are .ogg files under
//     ux0:data/popclassic/Data/Audio/...

#include <string>

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/popclassic/"
#endif

// Replace whatever extension the request has (.mp3/.m4a/.mp4/none) with .ogg.
static inline std::string audio_swap_ext_to_ogg(const std::string &p) {
    size_t slash = p.find_last_of('/');
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return p + ".ogg";
    return p.substr(0, dot) + ".ogg";
}

// Map any path the game may request to the real .ogg under DATA_PATH "Data/".
// Anchors on the "Audio/" component so it tolerates "Extra/", "assets/Extra/",
// leading slashes, or an already-translated "Data/Audio/..." input.
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

    return std::string(DATA_PATH) + "Data/" + audio_swap_ext_to_ogg(rel);
}

// Alternate location some assets ship in (resolution-suffixed data folder).
// Tried only after the primary sanitized path fails to open.
static inline std::string audio_fallback_path(const std::string &sanitized) {
    std::string s = sanitized;
    size_t pos = s.find("Data/");
    if (pos != std::string::npos)
        s.replace(pos, 5, "Data_960_576/");
    return s;
}

#endif // AUDIO_PATH_H
