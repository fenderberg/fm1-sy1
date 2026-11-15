/* Synth1 patches as a SysEx file for SY1:
 *
 *   sy1pack OUT.syx [-first N] PATCH.sy1 ...
 *
 * One frame per patch, for the slots N, N + 1, ... (N from 1, as the FM-1's screen counts them; default 1).
 * Send OUT.syx to the FM-1 over USB MIDI with any SysEx tool, with a pause of 200 ms or more between frames:
 * each one is a write to the FM-1's flash, and it answers F0 7D 53 59 31 41 <slot> <0 = stored> F7. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sy1frame.h"

int main(int argc, char **argv)
{
    static char text[65536];
    uint8_t frame[SY1_FRAME_MAX];
    FILE *out;
    int i, slot = 0, done = 0, bad = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: sy1pack OUT.syx [-first N] PATCH.sy1 ...\n");
        return 2;
    }
    out = fopen(argv[1], "wb");
    if (!out) {
        perror(argv[1]);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        s1_patch p;
        FILE *f;
        size_t n;
        if (!strcmp(argv[i], "-first") && i + 1 < argc) {
            slot = atoi(argv[++i]) - 1;
            if (slot < 0)
                slot = 0;
            continue;
        }
        f = fopen(argv[i], "rb");
        if (!f) {
            perror(argv[i]);
            bad++;
            continue;
        }
        n = fread(text, 1, sizeof text, f);
        fclose(f);
        if (s1_patch_parse(&p, text, (uint32_t)n) == 0) {
            fprintf(stderr, "%s: no parameter lines, not a .sy1 patch (skipped)\n", argv[i]);
            bad++;
            continue;
        }
        if (slot >= 1024) {
            fprintf(stderr, "%s: the bank is full at 1024 patches (skipped)\n", argv[i]);
            bad++;
            continue;
        }
        fwrite(frame, 1, sy1_frame(&p, (uint32_t)slot, frame), out);
        printf("%4d  %s%s\n", slot + 1, p.name, p.version < 109 ? "  (old version, converted)" : "");
        slot++;
        done++;
    }
    fclose(out);
    printf("%d patches in %s%s\n", done, argv[1], bad ? " (some files were skipped)" : "");
    return done ? 0 : 1;
}
