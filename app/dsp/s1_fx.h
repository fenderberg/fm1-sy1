/* The engine's global section, after the voices: effect -> pan -> EQ -> delay -> chorus (s1_fx.c). */
#pragma once
#include "s1_synth.h"

/* Calculates the actual delay in samples. Returns 1 if the requested time had to be halved. */
int s1_delay_times(int division, int spread, float tempo, float sample_rate, uint32_t length,
                   float *left, float *right);

void s1_fx_reset(s1_synth *s);                               /* empties the delay and chorus lines */
void s1_fx_cook(s1_synth *s);                                /* coefficients from val[] */
void s1_fx_process(s1_synth *s, float *l, float *r, int n);  /* one chunk, in place */
