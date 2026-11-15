/* The engine's global section, after the voices: effect -> pan -> EQ -> delay -> chorus (s1_fx.c). */
#pragma once
#include "s1_synth.h"

void s1_fx_reset(s1_synth *s);                               /* empties the delay and chorus lines */
void s1_fx_cook(s1_synth *s);                                /* coefficients from val[] */
void s1_fx_process(s1_synth *s, float *l, float *r, int n);  /* one chunk, in place */
