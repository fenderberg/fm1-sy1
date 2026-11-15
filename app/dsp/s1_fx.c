/* Global section: effect (10 types) -> pan -> equalizer -> tempo delay -> chorus.
 * Laws marked (m) are oxo_synth2's measurements of Synth1. */
#include "s1_fx.h"
#include "s1_math.h"

#define P(name) (s->val[S1P_##name])

/* ---------------------------------------------------------------- filters --- */
static void biquad_set(s1_biquad *q, float w_cycles, float qf, int highpass)   /* RBJ; w = f / fs */
{
    const float w = s1_minf(w_cycles, 0.49f);
    const float al = s1_sin_cycles(w) / (2.0f * qf), c = s1_cos_cycles(w), a0 = 1.0f / (1.0f + al);
    const float k = highpass ? (1.0f + c) * 0.5f : (1.0f - c) * 0.5f;
    q->b0 = k * a0;
    q->b1 = (highpass ? -2.0f * k : 2.0f * k) * a0;
    q->b2 = k * a0;
    q->a1 = -2.0f * c * a0;
    q->a2 = (1.0f - al) * a0;
}

static void peak_set(s1_biquad *q, float w_cycles, float pole_q, float gain)
{
    /* the analog prototype (s^2 + s G w0 / Q + w0^2) / (s^2 + s w0 / Q + w0^2) (m), as an RBJ peak with
     * Q' = Q / sqrt(G) */
    const float w = s1_minf(w_cycles, 0.49f), a = s1_powf(gain, 0.5f);
    const float al = s1_sin_cycles(w) / (2.0f * pole_q / a), c = s1_cos_cycles(w), a0 = 1.0f / (1.0f + al / a);
    q->b0 = (1.0f + al * a) * a0;
    q->b1 = -2.0f * c * a0;
    q->b2 = (1.0f - al * a) * a0;
    q->a1 = -2.0f * c * a0;
    q->a2 = (1.0f - al / a) * a0;
}

static inline float biquad_run(s1_biquad *q, int ch, float x)
{
    float *z = q->z[ch];
    const float y = q->b0 * x + z[0];
    z[0] = q->b1 * x - q->a1 * y + z[1];
    z[1] = q->b2 * x - q->a2 * y;
    return y;
}

static void onepole_set(s1_onepole *f, float hz, int highpass, float inv_sr)   /* bilinear, not prewarped (m) */
{
    const float k = S1_PI * hz * inv_sr;
    f->hp = (uint8_t)highpass;
    f->bypass = 0;
    f->g = (highpass ? 1.0f : k) / (1.0f + k);
    f->b = (1.0f - k) / (1.0f + k);
}

static inline float onepole_run(s1_onepole *f, int ch, float x)
{
    float y;
    if (f->bypass)
        return x;
    y = f->g * (f->hp ? x - f->x1[ch] : x + f->x1[ch]) + f->b * f->y1[ch];
    f->x1[ch] = x;
    f->y1[ch] = y + 1e-20f;
    return y;
}

/* ------------------------------------------------------------------ reset --- */
void s1_fx_reset(s1_synth *s)
{
    uint32_t i;
    int c, k;
    for (c = 0; c < 2; c++) {
        s->fx_fb[c] = s->fx_apfb[c] = s->fx_hold[c] = s->fx_held[c] = s->fx_env[c] = 0.0f;
        for (k = 0; k < 12; k++)
            s->fx_ap[c][k] = 0.0f;
        for (k = 0; k < 2; k++)
            s->ck.fx_hp.z[c][k] = s->ck.fx_lp.z[c][k] = s->ck.eq.z[c][k] = 0.0f;
        s->ck.eq_tone.x1[c] = s->ck.eq_tone.y1[c] = s->ck.dly_tone.x1[c] = s->ck.dly_tone.y1[c] = 0.0f;
    }
    s->fx_hold_n = 0;
    s->fx_pos = 0.0f;
    s->fx_dir = 1.0f;
    if (s->cfg.delay_buf)
        for (i = 0; i < 2u * s->cfg.delay_len; i++)
            s->cfg.delay_buf[i] = 0;
    if (s->cfg.chorus_buf)
        for (i = 0; i < 2u * s->cfg.chorus_len; i++)
            s->cfg.chorus_buf[i] = 0.0f;
    s->dly_sl = s->dly_sr = -1.0f;   /* the next patch's time, no glide */
    s->dly_glide = 1.0f - s1_expf(-1.0f / (0.215f * s->cfg.sample_rate));   /* (m) a new time glides, tau 0.215 s */
}

/* ------------------------------------------------------------------- cook --- */
/* (m) the chorus's base delay per time knob, whole samples at 44.1 kHz */
static const uint16_t cho_time[128] = {
    2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 17, 19, 22, 24,
    26, 29, 32, 35, 38, 41, 45, 49, 53, 57, 62, 67, 72, 77, 83, 90,
    96, 103, 111, 119, 127, 136, 145, 155, 166, 177, 188, 201, 214, 228, 242, 257,
    273, 290, 308, 327, 347, 368, 389, 412, 437, 462, 489, 517, 546, 577, 609, 643,
    666, 677, 687, 697, 708, 718, 729, 739, 750, 760, 770, 781, 791, 802, 812, 822,
    833, 843, 854, 864, 875, 885, 895, 906, 916, 927, 937, 947, 958, 968, 979, 989,
    1000, 1010, 1020, 1031, 1041, 1052, 1062, 1072, 1083, 1093, 1104, 1114, 1125, 1135, 1145, 1156,
    1166, 1177, 1187, 1197, 1208, 1218, 1229, 1239, 1250, 1260, 1270, 1281, 1291, 1302, 1312, 1323,
};

static float log_interp(const float *x, const float *y, int n, float v)
{
    int i = 0;
    float t;
    while (i < n - 2 && v > x[i + 1])
        i++;
    t = s1_clampf((v - x[i]) / (x[i + 1] - x[i]), 0.0f, 1.0f);
    return s1_exp2f(s1_log2f(y[i]) * (1.0f - t) + s1_log2f(y[i + 1]) * t);
}

void s1_fx_cook(s1_synth *s)
{
    const float sr = s->cfg.sample_rate, isr = s->inv_sr;
    const int type = P(EFFECT_TYPE), c1 = P(EFFECT_CTRL1), c2 = P(EFFECT_CTRL2), lv = P(EFFECT_LEVEL);
    const float lp_w = 400.0f * s1_powf(40.0f, (float)c2 / 127.0f) * isr;

    /* ---- effect ---- */
    if (type == 0) {          /* a.d.1 */
        s->ck.fx_drive = s1_t_ad1_drive[c1];
        s->ck.fx_out = s1_t_ad1_out[c1] * (float)(lv + 2) / 129.0f;
        biquad_set(&s->ck.fx_hp, 96.28f * isr, 2.37f, 1);
        biquad_set(&s->ck.fx_lp, lp_w, 3.0f, 0);
    } else if (type == 1) {   /* a.d.2 */
        const float x = (float)c1 / 4.0f;
        int i = (int)x;
        float t;
        if (i > 31)
            i = 31;
        t = s1_minf(1.0f, ((float)c1 - 4.0f * (float)i) / (i == 31 ? 3.0f : 4.0f));
        s->ck.fx_drive = s1_exp2f(s1_log2f(s1_t_ad2_drive[i]) * (1.0f - t) + s1_log2f(s1_t_ad2_drive[i + 1]) * t);
        s->ck.fx_out = (float)(lv + 2) / 129.0f;
        biquad_set(&s->ck.fx_hp, 100.0f * isr, 2.29f, 1);
        biquad_set(&s->ck.fx_lp, lp_w, 3.0f, 0);
    } else if (type == 2) {   /* d.d.: a triangle fold */
        s->ck.fx_peak = s1_t_dd_peak[c1];
        s->ck.fx_drive = s1_t_dd_slope[c1] / s1_t_dd_peak[c1];
        s->ck.fx_out = (float)(lv + 2) / 129.0f;
        biquad_set(&s->ck.fx_hp, 100.0f * isr, 2.29f, 1);
        biquad_set(&s->ck.fx_lp, lp_w, 3.0f, 0);
    } else if (type == 3) {   /* decimator */
        const float k = s1_tanf(S1_PI * s1_minf(14016.0f * isr, 0.45f));
        s->ck.fx_period = c1 + 1;
        s->ck.fx_q = 0.00643f * s1_exp2f((float)(c2 / 8 - 8));
        s->ck.fx_mix = lv >= 127 ? 1.0f : (float)lv / 128.0f;
        s->ck.fx_drive = k / (1.0f + k);
        s->ck.fx_a = (1.0f - k) / (1.0f + k);
    } else if (type == 4) {   /* ring modulator, 1 Hz .. 8 kHz */
        s->ck.fx_mix = (float)lv / 127.0f;
        s->ck.fx_inc = s1_powf(8000.0f, (float)c1 / 127.0f) * isr;
    } else if (type == 5) {   /* compressor */
        static const float cx[11] = {0, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127};
        static const float ct[11] = {0.0001f, 0.000643f, 0.001455f, 0.002735f, 0.004578f, 0.010796f,
                                     0.022192f, 0.041823f, 0.074208f, 0.125771f, 0.199574f};
        s->ck.fx_a = 1.0f - s1_expf(-1.0f / (log_interp(cx, ct, 11, (float)c2) * sr));
        s->ck.fx_gmax4 = s1_db(-4.0f * (10.0f + 0.3125f * (float)c1));
        s->ck.fx_out = s1_db(30.0f * ((float)lv / 127.0f - 1.0f));
    } else {                  /* phasers: 2, 4, 8, 12 stages */
        static const uint8_t stages[4] = {2, 4, 8, 12};
        s->ck.fx_stages = stages[s1_clampi(type - 6, 0, 3)];
        s->ck.fx_h = 0.02625f * (float)c1;
        s->ck.fx_step = 4.0f * s->ck.fx_h * 0.6418f * s1_exp2f((float)(c2 - 64) / 12.9f) * isr;
        s->ck.fx_k = 0.99f * (float)(lv - 64) / 64.0f;
    }

    /* ---- equalizer: (m) 50 Hz .. 16 kHz, +-25 dB, Q 3.18 x 2^((q - 64) / 38.16); the tone always on ---- */
    {
        const float gain = s1_db((float)(P(EQ_LEVEL) - 64) * 50.0f / 127.0f);
        const float q = 3.18f * s1_exp2f((float)(P(EQ_Q) - 64) / 38.16f);
        s->ck.eq_on = P(EQ_LEVEL) != 64;
        peak_set(&s->ck.eq, 50.0f * s1_powf(320.0f, (float)P(EQ_FREQ) / 127.0f) * isr, gain < 1.0f ? q * gain : q, gain);
        onepole_set(&s->ck.eq_tone, s1_t_eq_tone_hz[P(EQ_TONE)], P(EQ_TONE) > 64, isr);
    }

    /* ---- delay ---- */
    {
        static const float beats[20] = {0.0f, 1.0f / 24, 1.0f / 12, 1.0f / 8, 1.0f / 6, 0.25f, 1.0f / 3,
                                        0.375f, 0.5f, 2.0f / 3, 0.75f, 0.875f, 1.0f, 4.0f / 3,
                                        1.5f, 1.75f, 2.0f, 3.0f, 3.5f, 4.0f};
        const float beat = 60.0f / s1_maxf(s->tempo, 20.0f);
        const float max_t = s->cfg.delay_len > 8 ? (float)(s->cfg.delay_len - 4) : 1.0f;
        const int sp = P(DELAY_SPREAD) - 64, sd = sp < 0 ? (-sp > 63 ? 63 : -sp) : (sp > 63 ? 63 : sp);
        /* (m) one side later by 100 ms x 10^((|v - 64| - 63) / 21) */
        const float off = sd ? s1_floorf(0.1f * sr * s1_exp2f((float)(sd - 63) / 21.0f * 3.32192809f)) : 0.0f;
        const float min_t = s1_floorf(1e-4f * sr);
        float base = beats[s1_clampi(P(DELAY_TIME), 0, 19)] * beat * sr;
        while (base + off > max_t)   /* longer than the line (the FM-1's memory): the next shorter octave */
            base *= 0.5f;
        base = 4.0f * s1_floorf(base * 0.25f);   /* (m) whole multiples of 4 samples */
        s->ck.dly_tl = s1_maxf(min_t, base + (sp > 0 ? off : 0.0f));
        s->ck.dly_tr = s1_maxf(min_t, base + (sp < 0 ? off : 0.0f));
        s->ck.dly_fb = (float)P(DELAY_FEEDBACK) / 127.0f;
        s->ck.dly_wet = s1_minf(1.0f, (float)P(DELAY_DRY_WET) / 64.5f);
        s->ck.dly_dry = s1_minf(1.0f, (float)(127 - P(DELAY_DRY_WET)) / 63.0f);
        if (P(DELAY_TONE) == 64)
            s->ck.dly_tone.bypass = 1;
        else
            onepole_set(&s->ck.dly_tone, s1_t_delay_tone_hz[P(DELAY_TONE)], P(DELAY_TONE) > 64, isr);
    }

    /* ---- chorus: (m) delay = base x (1 + D sin), D = (100^(depth / 127) - 1) / 99, rate 2.94 Hz x
     * 2^((rate - 64) / 8.89), comb feedback 0.99 x (fb - 64) / 64, wet = level / 127 over the dry ---- */
    {
        static const float fx[4] = {10, 36, 124, 400}, fa[4] = {1.0f, 0.98f, 0.92f, 0.62f};
        const float hz = P(CHORUS_RATE) <= 0 ? 0.01f : 2.9409f * s1_exp2f((float)(P(CHORUS_RATE) - 64) / 8.89f);
        float swing = 1.0f;
        if (hz > fx[0]) {   /* the swing shrinks at audio rates */
            int i = 0;
            float t;
            while (i < 2 && hz > fx[i + 1])
                i++;
            t = s1_clampf(s1_log2f(hz / fx[i]) / s1_log2f(fx[i + 1] / fx[i]), 0.0f, 1.0f);
            swing = fa[i] + (fa[i + 1] - fa[i]) * t;
        }
        s->ck.cho_base = s1_floorf((float)cho_time[P(CHORUS_TIME)] * sr / 44100.0f + 0.5f);
        s->ck.cho_depth = s->ck.cho_base * (s1_powf(100.0f, (float)P(CHORUS_DEPTH) / 127.0f) - 1.0f) / 99.0f * swing;
        s->ck.cho_fb = 0.99f * (float)(P(CHORUS_FEEDBACK) - 64) / 64.0f;
        s->ck.cho_level = (float)P(CHORUS_LEVEL) / 127.0f;
        s->ck.cho_inc = hz * isr;
        s->ck.cho_theta_r = P(CHORUS_TYPE) == 1 ? 0.0f : 0.25f;
    }
}

/* ----------------------------------------------------------------- effect --- */
static inline float ad2(float u)   /* (m) a.d.2's transfer: odd, tabulated every 0.05, flat above 10 */
{
    const float a = s1_absf(u) * 20.0f;
    float y;
    if (a >= 200.0f)
        y = s1_t_ad2_curve[200];
    else {
        const int i = (int)a;
        y = s1_t_ad2_curve[i] + (s1_t_ad2_curve[i + 1] - s1_t_ad2_curve[i]) * (a - (float)i);
    }
    return u < 0.0f ? -y : y;
}

static inline float fold(float v)   /* triangle fold: slope 1 through 0, peaks +-1, period 4 */
{
    float a = s1_absf(v) + 1.0f, ph;
    if (a >= 4.0f)
        a -= 4.0f * s1_floorf(a * 0.25f);
    ph = a < 2.0f ? a - 1.0f : 3.0f - a;
    return v < 0.0f ? -ph : ph;
}

static void effect(s1_synth *s, float *l, float *r, int n)
{
    const int type = P(EFFECT_TYPE);
    float *ch[2];
    int i, c;
    ch[0] = l;
    ch[1] = r;
    if (type <= 2) {   /* distortions: drive -> curve -> highpass -> lowpass -> level, no dry (m) */
        const float drive = s->ck.fx_drive, out = s->ck.fx_out, peak = s->ck.fx_peak;
        const float bias = 0.35f, bias0 = ad2(0.35f);
        for (c = 0; c < 2; c++)
            for (i = 0; i < n; i++) {
                const float u = drive * ch[c][i];
                /* a.d.1 here is a.d.2's curve driven off centre: the original's two-stage circuit is not modelled */
                const float z = type == 0 ? ad2(u + bias) - bias0 : type == 1 ? ad2(u) : peak * fold(u);
                ch[c][i] = out * biquad_run(&s->ck.fx_lp, c, biquad_run(&s->ck.fx_hp, c, z + 1e-20f));
            }
    } else if (type == 3) {   /* decimator: hold every ctl1 + 1 samples, quantise, a 14 kHz lowpass (m) */
        const float q = s->ck.fx_q, iq = 1.0f / q, m = s->ck.fx_mix, b = s->ck.fx_drive, a = s->ck.fx_a;
        const int period = s->ck.fx_period;
        for (i = 0; i < n; i++) {
            if (s->fx_hold_n == 0) {
                s->fx_hold[0] = s1_floorf(l[i] * iq) * q;
                s->fx_hold[1] = s1_floorf(r[i] * iq) * q;
            }
            if (++s->fx_hold_n >= period)
                s->fx_hold_n = 0;
            for (c = 0; c < 2; c++) {
                float w = s->fx_hold[c];
                if (period > 1) {
                    const float y = b * (w + s->fx_held[c]) + a * s->fx_apfb[c];
                    s->fx_held[c] = w;
                    s->fx_apfb[c] = y;
                    w = y;
                }
                ch[c][i] = (1.0f - m) * ch[c][i] + m * w;
            }
        }
    } else if (type == 4) {   /* ring modulator */
        const float m = s->ck.fx_mix, inc = s->ck.fx_inc;
        for (i = 0; i < n; i++) {
            const float g = 1.0f - m + m * s1_sin2pi(s->fx_phase);
            s->fx_phase += inc;
            if (s->fx_phase >= 1.0f)
                s->fx_phase -= 1.0f;
            l[i] *= g;
            r[i] *= g;
        }
    } else if (type == 5) {   /* compressor: a leveller, gain (Gmax^-4 + (env / 0.2604)^4)^(-1/4) (m) */
        const float a = s->ck.fx_a, g4 = s->ck.fx_gmax4, out = s->ck.fx_out;
        for (c = 0; c < 2; c++) {
            float env = s->fx_env[c];
            for (i = 0; i < n; i++) {
                float e;
                env += a * (s1_absf(ch[c][i]) - env);
                e = env * (1.0f / 0.2604f);
                e *= e;
                ch[c][i] *= out * s1_powf(g4 + e * e, -0.25f);
            }
            s->fx_env[c] = env + 1e-20f;
        }
    } else {   /* phasers: allpasses swept up and down around 254.6 Hz x 2^2.32, feedback, 50 % mix (m) */
        const int stages = s->ck.fx_stages;
        const float h = s->ck.fx_h, k = s->ck.fx_k;
        float t, a;
        int st, live = 0;
        for (i = 0; i < n && !live; i++)
            live = l[i] != 0.0f || r[i] != 0.0f;
        if (live && h > 0.0f) {   /* the sweep only moves while signal flows */
            s->fx_pos += s->fx_dir * s->ck.fx_step * (float)n;
            if (s->fx_pos > 2.32f + h) {
                s->fx_dir = -1.0f;
                s->fx_pos = 2.0f * (2.32f + h) - s->fx_pos;
            } else if (s->fx_pos < 2.32f - h && s->fx_dir < 0.0f) {
                s->fx_dir = 1.0f;
                s->fx_pos = 2.0f * (2.32f - h) - s->fx_pos;
            }
        }
        t = S1_PI * 254.6f * s1_exp2f(s->fx_pos) * s->inv_sr;
        a = (t - 1.0f) / (t + 1.0f);
        for (c = 0; c < 2; c++) {
            float *ap = s->fx_ap[c], fbk = s->fx_apfb[c];
            for (i = 0; i < n; i++) {
                const float x = ch[c][i];
                float y = x + k * fbk + 1e-20f;
                for (st = 0; st < stages; st++) {
                    const float o = a * y + ap[st];
                    ap[st] = y - a * o;
                    y = o;
                }
                fbk = y;
                ch[c][i] = 0.5f * (x + y);
            }
            s->fx_apfb[c] = fbk;
        }
    }
}

/* ------------------------------------------------------------------ delay --- */
static inline float dly_read(const int16_t *b, uint32_t pos, uint32_t mask, float d)
{
    const uint32_t di = (uint32_t)d;
    const float f = d - (float)di;
    const float x0 = (float)b[(pos - di) & mask], x1 = (float)b[(pos - di - 1u) & mask];
    return (x0 + (x1 - x0) * f) * (1.0f / 8192.0f);
}
static inline int16_t dly_pack(float x)   /* 16 bits, +-4 full scale */
{
    const float y = x * 8192.0f;
    return (int16_t)(y > 32767.0f ? 32767.0f : y < -32767.0f ? -32767.0f : y);
}

static void delay(s1_synth *s, float *l, float *r, int n)
{
    int16_t *bl = s->cfg.delay_buf, *br = s->cfg.delay_buf + s->cfg.delay_len;
    const uint32_t mask = s->cfg.delay_len - 1u;
    const float tl = s->ck.dly_tl, tr = s->ck.dly_tr, fb = s->ck.dly_fb, wet = s->ck.dly_wet, dry = s->ck.dly_dry;
    const float glide = s->dly_glide;
    const int type = P(DELAY_TYPE);   /* 0 stereo, 1 cross, 2 ping-pong */
    uint32_t pos = s->dly_pos;
    int i;
    if (s->dly_sl < 0.0f) {
        s->dly_sl = tl;
        s->dly_sr = tr;
    }
    for (i = 0; i < n; i++) {
        float el, er, wl, wr;
        s->dly_sl += (tl - s->dly_sl) * glide;
        s->dly_sr += (tr - s->dly_sr) * glide;
        el = onepole_run(&s->ck.dly_tone, 0, dly_read(bl, pos - 1u, mask, s->dly_sl - 1.0f));
        er = onepole_run(&s->ck.dly_tone, 1, dly_read(br, pos - 1u, mask, s->dly_sr - 1.0f));
        switch (type) {
        case 1: wl = r[i] + fb * er; wr = l[i] + fb * el; break;   /* (m) a left input echoes right first */
        case 2: wl = 0.5f * (l[i] + r[i]) + fb * er; wr = el; break;
        default: wl = l[i] + fb * el; wr = r[i] + fb * er; break;
        }
        bl[pos & mask] = dly_pack(wl);
        br[pos & mask] = dly_pack(wr);
        pos++;
        l[i] = dry * l[i] + wet * el;
        r[i] = dry * r[i] + wet * er;
    }
    s->dly_pos = pos;
}

/* ----------------------------------------------------------------- chorus --- */
static inline float cho_read(const float *b, uint32_t pos, uint32_t mask, float d)
{
    const uint32_t di = (uint32_t)d;
    const float f = d - (float)di;
    const float x0 = b[(pos - di) & mask], x1 = b[(pos - di - 1u) & mask];
    return x0 + (x1 - x0) * f;
}

static void chorus(s1_synth *s, float *l, float *r, int n)
{
    float *bl = s->cfg.chorus_buf, *br = s->cfg.chorus_buf + s->cfg.chorus_len;
    const uint32_t mask = s->cfg.chorus_len - 1u;
    const int quad = P(CHORUS_TYPE) >= 4;
    const float base = s->ck.cho_base, depth = s->ck.cho_depth, fb = s->ck.cho_fb, level = s->ck.cho_level;
    const float max_d = (float)(s->cfg.chorus_len - 3u), inv_n = 1.0f / (float)n;
    const float th[4] = {0.0f, quad ? 0.25f : s->ck.cho_theta_r, 0.5f, 0.75f};
    const float ph0 = s->cho_phase, ph1 = ph0 + s->ck.cho_inc * (float)n;
    float d[4], dd[4];
    uint32_t pos = s->cho_pos;
    int i, k;
    for (k = 0; k < (quad ? 4 : 2); k++) {   /* the taps' delays at the chunk's ends, ramped between */
        const float a = s1_clampf(base + depth * s1_sin_cycles(ph0 - th[k] + 1.0f), 0.0f, max_d);
        const float b = s1_clampf(base + depth * s1_sin_cycles(ph1 - th[k] + 1.0f), 0.0f, max_d);
        d[k] = a;
        dd[k] = (b - a) * inv_n;
    }
    for (i = 0; i < n; i++) {
        float wl, wr, fl, fr;
        bl[pos & mask] = l[i];   /* written first: a delay of 0 reads the input */
        br[pos & mask] = r[i];
        if (quad) {   /* (m) L>L 0, R>L 90, L>R 180, R>R 270 degrees */
            const float ll = cho_read(bl, pos, mask, d[0]), rl = cho_read(br, pos, mask, d[1]);
            const float lr = cho_read(bl, pos, mask, d[2]), rr = cho_read(br, pos, mask, d[3]);
            wl = ll + rl;
            wr = lr + rr;
            fl = ll;
            fr = rr;
            d[2] += dd[2];
            d[3] += dd[3];
        } else {
            wl = fl = cho_read(bl, pos, mask, d[0]);
            wr = fr = cho_read(br, pos, mask, d[1]);
        }
        d[0] += dd[0];
        d[1] += dd[1];
        bl[pos & mask] = l[i] + fb * fl + 1e-20f;
        br[pos & mask] = r[i] + fb * fr + 1e-20f;
        pos++;
        l[i] += level * wl;
        r[i] += level * wr;
    }
    s->cho_pos = pos;
    s->cho_phase = ph1 - s1_floorf(ph1);
}

/* ---------------------------------------------------------------- process --- */
void s1_fx_process(s1_synth *s, float *l, float *r, int n)
{
    float gl = s->ck.pan_l, gr = s->ck.pan_r;
    const float vol = s->volume;
    int i;
    if (P(EFFECT_ON))
        effect(s, l, r, n);
    if (s->has_pan) {   /* (m) LFO on pan: position q = 1 - (1 - |s|)^3, the near side up to 1.5, the far to 0 */
        const float sv = s->lfo_pan, u = 1.0f - s1_absf(sv), cube = u * u * u, q = sv >= 0.0f ? 1.0f - cube : cube - 1.0f;
        gl *= q >= 0.0f ? 1.0f + 0.5f * q : 1.0f + q;
        gr *= q >= 0.0f ? 1.0f - q : 1.0f - 0.5f * q;
    }
    for (i = 0; i < n; i++) {
        float a = l[i] * gl, b = r[i] * gr;
        if (s->ck.eq_on) {
            a = biquad_run(&s->ck.eq, 0, a + 1e-20f);
            b = biquad_run(&s->ck.eq, 1, b + 1e-20f);
        }
        l[i] = onepole_run(&s->ck.eq_tone, 0, a);
        r[i] = onepole_run(&s->ck.eq_tone, 1, b);
    }
    if (P(DELAY_ON) && s->cfg.delay_buf)
        delay(s, l, r, n);
    if (P(CHORUS_ON) && s->cfg.chorus_buf)
        chorus(s, l, r, n);
    else {   /* (m) its LFO runs while it is off too */
        s->cho_phase += s->ck.cho_inc * (float)n;
        s->cho_phase -= s1_floorf(s->cho_phase);
    }
    if (vol != 1.0f)
        for (i = 0; i < n; i++) {
            l[i] *= vol;
            r[i] *= vol;
        }
}
