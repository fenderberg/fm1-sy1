/* Single-precision maths for the engine: no libm, no double (the FM-1's FPU is float-only).
 * Accurate to about 1e-6 relative, which is far below what the parameter laws need. */
#pragma once
#include <stdint.h>

#define S1_PI 3.14159265358979f
#define S1_TWO_PI 6.28318530717959f

typedef union { float f; uint32_t u; int32_t i; } s1_bits;

static inline float s1_absf(float x) { s1_bits b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
static inline float s1_minf(float a, float b) { return a < b ? a : b; }
static inline float s1_maxf(float a, float b) { return a > b ? a : b; }
static inline float s1_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int s1_clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static inline float s1_floorf(float x) /* |x| < 2^31 */
{
    float f = (float)(int32_t)x;
    return f > x ? f - 1.0f : f;
}

/* 2^x */
static inline float s1_exp2f(float x)
{
    s1_bits b;
    float fl, f, p;
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    fl = s1_floorf(x + 0.5f);
    f = (x - fl) * 0.693147181f; /* |f| <= 0.3466: e^f by Taylor, degree 7 */
    p = 1.0f / 5040.0f;
    p = p * f + 1.0f / 720.0f;
    p = p * f + 1.0f / 120.0f;
    p = p * f + 1.0f / 24.0f;
    p = p * f + 1.0f / 6.0f;
    p = p * f + 0.5f;
    p = p * f + 1.0f;
    p = p * f + 1.0f;
    b.f = p;
    b.i += (int32_t)fl << 23;
    return b.f;
}

/* log2(x), x > 0 and normal */
static inline float s1_log2f(float x)
{
    s1_bits b;
    int32_t e;
    float t, t2, p;
    b.f = x;
    e = (int32_t)(b.u >> 23) - 127;
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u; /* mantissa in [1, 2) */
    if (b.f > 1.41421356f) {
        b.f *= 0.5f;
        e++;
    }
    t = (b.f - 1.0f) / (b.f + 1.0f); /* |t| <= 0.1716 */
    t2 = t * t;
    p = 1.0f / 9.0f;
    p = p * t2 + 1.0f / 7.0f;
    p = p * t2 + 1.0f / 5.0f;
    p = p * t2 + 1.0f / 3.0f;
    p = p * t2 + 1.0f;
    return (float)e + p * t * 2.88539008f; /* 2 / ln 2 */
}

static inline float s1_expf(float x) { return s1_exp2f(x * 1.44269504f); }
static inline float s1_powf(float a, float b) { return a > 0.0f ? s1_exp2f(b * s1_log2f(a)) : 0.0f; }
static inline float s1_db(float db) { return s1_exp2f(db * 0.166096405f); } /* 10^(db / 20) */

/* sin(2 pi ph), ph in [0, 1] */
static inline float s1_sin2pi(float ph)
{
    float x = ph - 0.5f, a = s1_absf(x), r, r2, s;
    r = S1_TWO_PI * (a < 0.25f ? a : 0.5f - a);
    r2 = r * r;
    s = 1.0f / 362880.0f;
    s = s * r2 - 1.0f / 5040.0f;
    s = s * r2 + 1.0f / 120.0f;
    s = s * r2 - 1.0f / 6.0f;
    s = s * r2 + 1.0f;
    s *= r;
    return x > 0.0f ? -s : s;
}
/* any phase in cycles */
static inline float s1_sin_cycles(float c) { return s1_sin2pi(c - s1_floorf(c)); }
static inline float s1_cos_cycles(float c) { return s1_sin_cycles(c + 0.25f); }
/* tan(x), 0 <= x < pi / 2 */
static inline float s1_tanf(float x)
{
    float c = x * (1.0f / S1_TWO_PI);
    return s1_sin2pi(c) / s1_sin2pi(c + 0.25f);
}
