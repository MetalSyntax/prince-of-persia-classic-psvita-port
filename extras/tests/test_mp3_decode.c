// Host-side decode test: fully decodes every .mp3 passed on the command line
// with the SAME vendored minimp3 the Vita build ships (mp3dec_load_buf, the
// same *_buf memory API audio.cpp uses for SFX), so a file that fails here
// would also fail on console.
//
// Usage: find <Data/Audio> -name '*.mp3' -print0 | xargs -0 ./test_mp3_decode

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "../../lib/minimp3/minimp3_ex.h"

static unsigned char *slurp(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb"); // host-side stdio only, to feed load_buf
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    int failures = 0;
    long long total_samples = 0;

    for (int i = 1; i < argc; i++) {
        long len = 0;
        unsigned char *mp3 = slurp(argv[i], &len);
        if (!mp3) {
            printf("FAIL (read): %s\n", argv[i]);
            failures++;
            continue;
        }
        mp3dec_t dec;
        mp3dec_file_info_t info;
        memset(&info, 0, sizeof(info));
        int ret = mp3dec_load_buf(&dec, mp3, (size_t)len, &info, NULL, NULL);
        free(mp3);
        if (ret != 0 || !info.buffer || info.samples == 0) {
            printf("FAIL: %s (ret=%d samples=%zu)\n", argv[i], ret, info.samples);
            failures++;
            free(info.buffer);
            continue;
        }
        if (info.hz <= 0 || info.channels <= 0 || info.channels > 2) {
            printf("SUSPECT: %s rate=%d ch=%d\n", argv[i], info.hz, info.channels);
            failures++;
        }
        total_samples += (long long)info.samples;
        free(info.buffer);
    }

    printf("decoded %d file(s), %lld samples total, %d failure(s)\n",
           argc - 1, total_samples, failures);
    return failures ? 1 : 0;
}
