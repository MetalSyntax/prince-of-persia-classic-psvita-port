// Host-side unit test for source/audio_path.h (the exact header the Vita
// build compiles). Run via extras/tests/run_tests.sh before every build.
//
// Modes:
//   ./test_audio_path                 -> fixed unit cases
//   ./test_audio_path <list> <root>   -> additionally sanitize every raw path
//                                        in <list> (one per line, as extracted
//                                        from the game .so strings) and check
//                                        the resulting file exists under
//                                        <root> (the local ux0_data mirror).

#include "../../source/audio_path.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static int failures = 0;

static void expect_eq(const char *raw, const std::string &got, const std::string &want) {
    if (got != want) {
        printf("FAIL: sanitize(\"%s\")\n  got:  %s\n  want: %s\n", raw ? raw : "(null)", got.c_str(), want.c_str());
        failures++;
    } else {
        printf("ok: %s -> %s\n", raw ? raw : "(null)", got.c_str());
    }
}

int main(int argc, char **argv) {
    const std::string P = DATA_PATH; // "ux0:data/popclassic/"

    // Real request shapes seen in the game .so string tables
    expect_eq("Extra/Audio/Music/POP_BGM_Menu.mp3",
              sanitize_audio_path("Extra/Audio/Music/POP_BGM_Menu.mp3"),
              P + "Data/Audio/Music/POP_BGM_Menu.ogg");
    expect_eq("Extra/Audio/Music/95_boss_fight_2.m4a",
              sanitize_audio_path("Extra/Audio/Music/95_boss_fight_2.m4a"),
              P + "Data/Audio/Music/95_boss_fight_2.ogg");
    // Audio request with a .mp4 extension (it's a sound, not a video)
    expect_eq("Extra/Audio/SFX/Enemies/Jaffar/94_jaffar_fight.mp4",
              sanitize_audio_path("Extra/Audio/SFX/Enemies/Jaffar/94_jaffar_fight.mp4"),
              P + "Data/Audio/SFX/Enemies/Jaffar/94_jaffar_fight.ogg");
    // Spaces in filenames must survive
    expect_eq("Extra/Audio/SFX/Footstep/step concrete_4.mp3",
              sanitize_audio_path("Extra/Audio/SFX/Footstep/step concrete_4.mp3"),
              P + "Data/Audio/SFX/Footstep/step concrete_4.ogg");
    // Tolerated prefix variants
    expect_eq("assets/Extra/Audio/Ambiance/POP_AMB_Level_1.mp3",
              sanitize_audio_path("assets/Extra/Audio/Ambiance/POP_AMB_Level_1.mp3"),
              P + "Data/Audio/Ambiance/POP_AMB_Level_1.ogg");
    expect_eq("/Extra/Audio/Music/28_Heroic.mp3",
              sanitize_audio_path("/Extra/Audio/Music/28_Heroic.mp3"),
              P + "Data/Audio/Music/28_Heroic.ogg");
    // Already-translated input stays stable (idempotent)
    expect_eq("Data/Audio/Music/POP_BGM_Menu.ogg",
              sanitize_audio_path("Data/Audio/Music/POP_BGM_Menu.ogg"),
              P + "Data/Audio/Music/POP_BGM_Menu.ogg");
    // Directory with a dot in it must not confuse the extension swap
    expect_eq("Extra/Audio/Music/Short/24_Accident.mp3",
              sanitize_audio_path("Extra/Audio/Music/Short/24_Accident.mp3"),
              P + "Data/Audio/Music/Short/24_Accident.ogg");
    // Degenerate inputs must not crash and must produce *some* path
    expect_eq(NULL, sanitize_audio_path(NULL), P + "Data/.ogg");
    expect_eq("", sanitize_audio_path(""), P + "Data/.ogg");

    // Fallback folder translation
    expect_eq("(fallback)",
              audio_fallback_path(P + "Data/Audio/Music/x.ogg"),
              P + "Data_960_576/Audio/Music/x.ogg");

    // Optional exhaustive pass over the real request list
    if (argc == 3) {
        std::ifstream list(argv[1]);
        std::string root = argv[2]; // local mirror of ux0:data/popclassic/
        std::string raw;
        int checked = 0, missing = 0;
        while (std::getline(list, raw)) {
            if (raw.empty()) continue;
            std::string s = sanitize_audio_path(raw.c_str());
            if (s.compare(0, P.size(), P) != 0) {
                printf("FAIL: %s did not map under DATA_PATH: %s\n", raw.c_str(), s.c_str());
                failures++;
                continue;
            }
            std::string local = root + "/" + s.substr(P.size());
            FILE *f = fopen(local.c_str(), "rb");
            if (!f) {
                printf("MISSING ASSET: %s -> %s\n", raw.c_str(), local.c_str());
                missing++;
                failures++;
            } else {
                fclose(f);
            }
            checked++;
        }
        printf("asset check: %d requests, %d missing\n", checked, missing);
    }

    if (failures) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
