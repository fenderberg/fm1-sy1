/* Host tool: render a .sy1 patch to a WAV file with the engine exactly as the FM-1 runs it.
 *
 *   s1render PATCH.sy1 OUT.wav [-n "60,64,67"] [-v 100] [-hold 2.0] [-tail 2.0] [-bpm 120]
 *            [-voices 16] [-set ID=VALUE ...] [-bench]
 *
 * PATCH may be "init" for Synth1's init patch. -bench prints the engine's speed instead of writing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "s1_synth.h"
#include "s1_patch.h"

#define SR 44100
#define BLOCK 256
#define DELAY_LEN 32768u   /* as on the FM-1 */
#define CHORUS_LEN 4096u

static s1_synth synth;
static int16_t delay_buf[2 * DELAY_LEN];
static float chorus_buf[2 * CHORUS_LEN];

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24 & 255, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv)
{
    s1_patch patch;
    s1_config cfg = {SR, 16, delay_buf, DELAY_LEN, chorus_buf, CHORUS_LEN};
    const char *notes = "48,60,64,67";
    float hold = 2.0f, tail = 2.0f, bpm = 120.0f, peak = 0.0f;
    double sumsq = 0.0;
    int vel = 100, bench = 0, i, total, hold_frames, pos, nkeys = 0, keys[32], lines = 99, nan = 0;
    int sets[64][2], nsets = 0;
    int16_t *pcm;
    float l[BLOCK], r[BLOCK];
    clock_t t0;

    if (argc < 3) {
        fprintf(stderr, "usage: s1render PATCH.sy1|init OUT.wav [-n notes] [-v vel] [-hold s] [-tail s] [-bpm t] [-voices n] [-set id=v] [-bench]\n");
        return 2;
    }
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-bench")) bench = 1;
        else if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", argv[i]); return 2; }
        else if (!strcmp(argv[i], "-n")) notes = argv[++i];
        else if (!strcmp(argv[i], "-v")) vel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-hold")) hold = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-tail")) tail = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-bpm")) bpm = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-voices")) cfg.max_voices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-set") && nsets < 64) {
            if (sscanf(argv[++i], "%d=%d", &sets[nsets][0], &sets[nsets][1]) == 2) nsets++;
        } else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (!strcmp(argv[1], "init"))
        s1_patch_init(&patch);
    else {
        static char text[65536];
        FILE *f = fopen(argv[1], "rb");
        size_t n;
        if (!f) { perror(argv[1]); return 1; }
        n = fread(text, 1, sizeof text, f);
        fclose(f);
        lines = s1_patch_parse(&patch, text, (uint32_t)n);
        if (lines == 0) { fprintf(stderr, "%s: no parameter lines, not a .sy1 patch\n", argv[1]); return 1; }
    }
    for (i = 0; i < nsets; i++)
        if (sets[i][0] >= 0 && sets[i][0] < S1_NPARAMS)
            patch.values[sets[i][0]] = sets[i][1];
    while (*notes && nkeys < 32) {
        keys[nkeys++] = atoi(notes);
        while (*notes && *notes != ',') notes++;
        if (*notes) notes++;
    }

    s1_init(&synth, &cfg);
    s1_set_tempo(&synth, bpm);
    s1_set_patch(&synth, patch.values);

    hold_frames = (int)(hold * SR) / BLOCK * BLOCK;
    total = hold_frames + (int)(tail * SR) / BLOCK * BLOCK;
    pcm = malloc((size_t)total * 2 * sizeof *pcm);
    if (!pcm) return 1;
    for (i = 0; i < nkeys; i++)
        s1_note_on(&synth, keys[i], vel);
    t0 = clock();
    for (pos = 0; pos < total; pos += BLOCK) {
        if (pos == hold_frames)
            for (i = 0; i < nkeys; i++)
                s1_note_off(&synth, keys[i]);
        s1_render(&synth, l, r, BLOCK);
        for (i = 0; i < BLOCK; i++) {
            float a = l[i], b = r[i];
            if (isnan(a) || isnan(b) || isinf(a) || isinf(b)) { nan++; a = b = 0; }
            if (fabsf(a) > peak) peak = fabsf(a);
            if (fabsf(b) > peak) peak = fabsf(b);
            sumsq += (double)a * a + (double)b * b;
            a = a > 1 ? 1 : a < -1 ? -1 : a;
            b = b > 1 ? 1 : b < -1 ? -1 : b;
            pcm[2 * (pos + i)] = (int16_t)(a * 32767.0f);
            pcm[2 * (pos + i) + 1] = (int16_t)(b * 32767.0f);
        }
    }
    {
        const double secs = (double)(clock() - t0) / CLOCKS_PER_SEC, audio = (double)total / SR;
        printf("%-24s ver %3d  %2d lines  peak %.3f  rms %.4f  %s%.0fx realtime\n", patch.name, patch.version, lines,
               peak, sqrt(sumsq / (2.0 * total)), nan ? "NaN! " : "", secs > 0 ? audio / secs : 0.0);
    }
    if (!bench) {
        FILE *f = fopen(argv[2], "wb");
        const uint32_t bytes = (uint32_t)total * 4u;
        if (!f) { perror(argv[2]); return 1; }
        fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SR); put32(f, SR * 4); put16(f, 4); put16(f, 16);
        fwrite("data", 1, 4, f); put32(f, bytes);
        fwrite(pcm, 1, bytes, f);
        fclose(f);
    }
    free(pcm);
    return nan ? 1 : 0;
}
