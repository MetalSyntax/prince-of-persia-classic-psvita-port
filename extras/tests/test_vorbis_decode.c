// Host-side decode test: fully decodes every .ogg passed on the command line
// with the SAME vendored stb_vorbis and the SAME configuration the Vita build
// ships (STB_VORBIS_NO_STDIO + *_memory APIs only), so a file that fails here
// would also fail on console, and any accidental use of the stdio API in
// audio code would not compile there either.
//
// Usage: find <Data/Audio> -name '*.ogg' -print0 | xargs -0 ./test_vorbis_decode

#include <stdio.h>
#include <stdlib.h>

#define STB_VORBIS_NO_STDIO
#include "../../lib/stb/stb_vorbis.c"

static unsigned char *slurp(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb"); // host-side stdio only, to feed decode_memory
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
        unsigned char *ogg = slurp(argv[i], &len);
        if (!ogg) {
            printf("FAIL (read): %s\n", argv[i]);
            failures++;
            continue;
        }
        int channels = 0, sample_rate = 0;
        short *output = NULL;
        int samples = stb_vorbis_decode_memory(ogg, (int)len, &channels, &sample_rate, &output);
        free(ogg);
        if (samples <= 0 || !output) {
            printf("FAIL: %s (samples=%d)\n", argv[i], samples);
            failures++;
            continue;
        }
        if (sample_rate <= 0 || channels <= 0 || channels > 2) {
            printf("SUSPECT: %s rate=%d ch=%d\n", argv[i], sample_rate, channels);
            failures++;
        }
        total_samples += samples;
        free(output);
    }

    printf("decoded %d file(s), %lld samples total, %d failure(s)\n",
           argc - 1, total_samples, failures);
    return failures ? 1 : 0;
}
