#ifndef VIDEO_PATH_H
#define VIDEO_PATH_H

// Pure path-translation helper for video playback, mirroring audio_path.h.
// The game requests Android asset paths for cutscenes, e.g.:
//     Extra/Video/High/PoP_V1_1.mp4
//     assets/Extra/Video/High/PoP_end.mp4
// while the unpacked assets on the memory card live under
//     ux0:data/popclassic/Data/Video/High/...

#include <string>

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/popclassic/"
#endif

// Maps any path the game may request to the real file under DATA_PATH
// "Data/Video/High/". Anchors on the "Video/" component so it tolerates
// "Extra/", "assets/Extra/", leading slashes, or an already-translated
// "Data/Video/High/..." input -- same convention as sanitize_audio_path.
static inline std::string sanitize_video_path(const char *raw) {
    std::string p = raw ? raw : "";

    std::string rel;
    size_t video = p.find("Video/");
    if (video != std::string::npos) {
        rel = p.substr(video); // "Video/High/x.mp4"
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

    // Requests may come in without the "High/" resolution folder or with a
    // bare filename (e.g. just "PoP_V1_1.mp4") if the enum->path mapping the
    // game does internally doesn't preserve the full Android asset path --
    // normalize to always land under Video/High/.
    if (rel.compare(0, 6, "Video/") != 0) {
        rel = "Video/High/" + rel;
    } else if (rel.compare(0, 11, "Video/High/") != 0) {
        rel = "Video/High/" + rel.substr(6);
    }

    return std::string(DATA_PATH) + "Data/" + rel;
}

#endif // VIDEO_PATH_H
