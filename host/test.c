/* Host tests for the engine: maths against libm, tuning, envelopes, the .sy1 reader, and a fuzz of random
 * patches through every effect (no NaN, no runaway level). Run with `make test`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "s1_synth.h"
#include "s1_patch.h"
#include "s1_math.h"

#define SR 44100
#define DELAY_LEN 32768u
#define CHORUS_LEN 4096u

static s1_synth synth;
static int16_t delay_buf[2 * DELAY_LEN];
static float chorus_buf[2 * CHORUS_LEN];
static float L[SR * 4], R[SR * 4];
static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void setup(const int32_t *values)
{
    s1_config cfg = {SR, 16, delay_buf, DELAY_LEN, chorus_buf, CHORUS_LEN};
    s1_init(&synth, &cfg);
    if (values)
        s1_set_patch(&synth, values);
}

static void render(int from, int frames)
{
    int pos;
    for (pos = 0; pos < frames; pos += 256)
        s1_render(&synth, L + from + pos, R + from + pos, frames - pos < 256 ? frames - pos : 256);
}

/* a plain patch: osc1 only, filter open, no modulation, no effects */
static void plain(int32_t *v, int shape)
{
    int i;
    for (i = 0; i < S1_NPARAMS; i++)
        v[i] = s1_param_def[i];
    v[S1P_OSC1_SHAPE] = shape;
    v[S1P_OSC_MIX] = 0;
    v[S1P_FILTER_TYPE] = 0;
    v[S1P_FILTER_FREQ] = 127;
    v[S1P_FILTER_RES] = 0;
    v[S1P_FILTER_AMOUNT] = 64;
    v[S1P_FILTER_KBD_TRACK] = 64;
    v[S1P_AMP_ATTACK] = 0;
    v[S1P_AMP_SUSTAIN] = 127;
    v[S1P_AMP_RELEASE] = 20;
    v[S1P_AMP_VEL_SENS] = 0;
    v[S1P_LFO1_ON] = v[S1P_LFO2_ON] = 0;
    v[S1P_OSC_KEY_SHIFT] = 0;
    v[S1P_OSC_FINE_TUNE] = 64;
}

/* frequency from the upward zero crossings of L[a..b) */
static double zero_cross_hz(int a, int b)
{
    double first = -1, last = -1;
    int i, n = 0;
    for (i = a + 1; i < b; i++)
        if (L[i - 1] < 0 && L[i] >= 0) {
            const double t = (i - 1) + (double)(-L[i - 1]) / (double)(L[i] - L[i - 1]);
            if (first < 0) first = t;
            last = t;
            n++;
        }
    return n > 1 ? (n - 1) * (double)SR / (last - first) : 0;
}

static void test_math(void)
{
    double worst = 0;
    float x;
    for (x = -20; x < 20; x += 0.0137f) { double e = fabs(s1_exp2f(x) / exp2((double)x) - 1); if (e > worst) worst = e; }
    CHECK(worst < 2e-6, "exp2f rel error %g", worst);
    worst = 0;
    for (x = 1e-4f; x < 1e5f; x *= 1.013f) { double e = fabs((double)s1_log2f(x) - log2((double)x)); if (e > worst) worst = e; }
    CHECK(worst < 2e-6, "log2f abs error %g", worst);
    worst = 0;
    for (x = 0; x <= 1; x += 0.00013f) { double e = fabs((double)s1_sin2pi(x) - sin(2 * M_PI * (double)x)); if (e > worst) worst = e; }
    CHECK(worst < 1e-5, "sin2pi abs error %g", worst);
    worst = 0;
    for (x = 0.001f; x < 1.5f; x += 0.0011f) { double e = fabs((double)s1_tanf(x) / tan((double)x) - 1); if (e > worst) worst = e; }
    CHECK(worst < 1e-4, "tanf rel error %g", worst);
    CHECK(fabs((double)s1_powf(320.0f, 0.5f) - sqrt(320.0)) < 1e-3, "powf");
}

static void test_tuning(void)
{
    static const int keys[] = {21, 33, 45, 57, 69, 81, 93, 105};
    int32_t v[S1_NPARAMS];
    unsigned k;
    int shape;
    for (shape = 0; shape < 4; shape++)
        for (k = 0; k < sizeof keys / sizeof *keys; k++) {
            const double want = 440.0 * pow(2.0, (keys[k] - 69) / 12.0);
            double got, cents;
            plain(v, shape);
            setup(v);
            s1_note_on(&synth, keys[k], 100);
            render(0, SR * 2);
            got = zero_cross_hz(SR / 2, SR * 2);
            cents = 1200.0 * log2(got / want);
            /* (a pulse or saw near Nyquist has few harmonics left: crossings still mark the fundamental) */
            CHECK(fabs(cents) < 0.5, "shape %d key %d: %.3f Hz, want %.3f (%.2f cents)", shape, keys[k], got, want, cents);
        }
}

static float peak(int a, int b)
{
    float p = 0;
    int i;
    for (i = a; i < b; i++) {
        if (fabsf(L[i]) > p) p = fabsf(L[i]);
        if (fabsf(R[i]) > p) p = fabsf(R[i]);
    }
    return p;
}

static void test_levels_and_env(void)
{
    int32_t v[S1_NPARAMS];
    float p;
    /* (measured law) a sine at gain 64, velocity 127 peaks at 0.2 */
    plain(v, 0);
    v[S1P_AMP_GAIN] = 64;
    setup(v);
    s1_note_on(&synth, 60, 127);
    render(0, SR);
    p = peak(SR / 2, SR);
    CHECK(fabsf(p - 0.2f) < 0.004f, "sine peak %.4f, want 0.200", (double)p);
    /* release: silent and the voice freed */
    s1_note_off(&synth, 60);
    render(0, SR * 2);
    CHECK(peak(SR, SR * 2) < 1e-4f, "release tail %.6f", (double)peak(SR, SR * 2));
    CHECK(s1_active_voices(&synth) == 0, "voice not freed after release");
    /* a slow attack is still rising after 100 ms */
    v[S1P_AMP_ATTACK] = 100;
    setup(v);
    s1_note_on(&synth, 60, 127);
    render(0, SR / 10);
    CHECK(peak(0, SR / 10) < 0.15f, "slow attack reached %.3f in 100 ms", (double)peak(0, SR / 10));
    /* sustain 0 decays to silence while held */
    v[S1P_AMP_ATTACK] = 0;
    v[S1P_AMP_DECAY] = 30;
    v[S1P_AMP_SUSTAIN] = 0;
    setup(v);
    s1_note_on(&synth, 60, 127);
    render(0, SR * 3);
    CHECK(peak(SR * 2, SR * 3) < 1e-3f, "sustain 0 still at %.5f", (double)peak(SR * 2, SR * 3));
    /* a closed lowpass darkens a saw: much lower level than open */
    plain(v, 1);
    setup(v);
    s1_note_on(&synth, 72, 127);
    render(0, SR);
    p = peak(SR / 2, SR);
    v[S1P_FILTER_FREQ] = 20;
    setup(v);
    s1_note_on(&synth, 72, 127);
    render(0, SR);
    CHECK(peak(SR / 2, SR) < 0.25f * p, "lowpass at 20: %.4f against %.4f open", (double)peak(SR / 2, SR), (double)p);
}

static void test_polyphony_and_modes(void)
{
    int32_t v[S1_NPARAMS];
    int i;
    plain(v, 1);
    v[S1P_POLYPHONY] = 4;
    setup(v);
    for (i = 0; i < 8; i++)
        s1_note_on(&synth, 48 + i, 100);
    render(0, 4096);
    CHECK(s1_active_voices(&synth) == 4, "polyphony 4 gives %d voices", s1_active_voices(&synth));
    v[S1P_PLAY_MODE] = 1;   /* mono */
    setup(v);
    for (i = 0; i < 5; i++)
        s1_note_on(&synth, 48 + i, 100);
    render(0, 4096);
    CHECK(s1_active_voices(&synth) == 1, "mono gives %d voices", s1_active_voices(&synth));
    v[S1P_PLAY_MODE] = 0;
    v[S1P_POLYPHONY] = 16;
    v[S1P_UNISON_ON] = 1;
    v[S1P_UNISON_VOICES] = 4;
    setup(v);
    s1_note_on(&synth, 60, 100);
    render(0, 4096);
    CHECK(s1_active_voices(&synth) == 4, "unison 4 gives %d voices", s1_active_voices(&synth));
    /* the arpeggiator plays and stops */
    plain(v, 1);
    v[S1P_ARP_ON] = 1;
    v[S1P_ARP_TYPE] = 2;
    v[S1P_ARP_BEAT] = 14;
    setup(v);
    s1_note_on(&synth, 60, 100);
    s1_note_on(&synth, 64, 100);
    render(0, SR);
    CHECK(peak(0, SR) > 0.05f, "arpeggiator silent");
    s1_note_off(&synth, 60);
    s1_note_off(&synth, 64);
    render(0, SR * 2);
    CHECK(s1_active_voices(&synth) == 0, "arpeggiator left %d voices", s1_active_voices(&synth));
    /* sustain pedal holds, then lets go */
    plain(v, 1);
    setup(v);
    s1_control_change(&synth, 64, 127);
    s1_note_on(&synth, 60, 100);
    s1_note_off(&synth, 60);
    render(0, SR);
    CHECK(s1_active_voices(&synth) == 1, "pedal did not hold");
    s1_control_change(&synth, 64, 0);
    render(0, SR * 2);
    CHECK(s1_active_voices(&synth) == 0, "pedal did not let go");
}

static void test_parser(void)
{
    static const char text[] = "My Bass\r\ncolor=red\r\nver=112\r\n0,1\r\n19,300\r\n9,-12\r\n98,10\r\n200,5\r\nbad line\r\n45,x\r\n";
    static const char old[] = "Old one\n0,2\n27,64\n";
    s1_patch p;
    int n = s1_patch_parse(&p, text, sizeof text - 1);
    CHECK(n == 4, "parsed %d lines, want 4", n);
    CHECK(!strcmp(p.name, "My Bass"), "name '%s'", p.name);
    CHECK(p.version == 112, "version %d", p.version);
    CHECK(p.values[0] == 1 && p.values[9] == -12 && p.values[98] == 10, "values");
    CHECK(p.values[19] == 127, "out of range value not clamped: %d", (int)p.values[19]);
    CHECK(p.values[45] == s1_param_def[45], "bad line changed a value");
    n = s1_patch_parse(&p, old, sizeof old - 1);
    CHECK(p.version == 0 && p.values[S1P_UNISON_VOICES] == 2 && p.values[S1P_DELAY_TONE] == 64, "legacy conversion");
    CHECK(p.values[27] == s1_legacy_tab[4][64], "legacy sustain remap");
}

static void test_fuzz(void)
{
    int32_t v[S1_NPARAMS];
    uint32_t rng = 12345;
    int t, i, bad = 0, loud = 0, type;
    float worst = 0;
    for (t = 0; t < 400; t++) {
        for (i = 0; i < S1_NPARAMS; i++) {
            rng = rng * 1664525u + 1013904223u;
            v[i] = s1_param_min[i] + (int32_t)((rng >> 8) % (uint32_t)(s1_param_max[i] - s1_param_min[i] + 1));
        }
        v[S1P_CTRL_SRC1] = 45057; v[S1P_CTRL_ASSIGN1] = 44; v[S1P_CTRL_SRC2] = 45057; v[S1P_CTRL_ASSIGN2] = 43;
        type = t % 10;
        v[S1P_EFFECT_TYPE] = type;
        v[S1P_EFFECT_ON] = t % 3 != 0;
        setup(v);
        s1_set_tempo(&synth, 60.0f + (float)(t % 120));
        s1_note_on(&synth, 36 + t % 48, 1 + (t * 7) % 127);
        s1_note_on(&synth, 43 + t % 48, 100);
        s1_control_change(&synth, 1, t % 128);
        s1_pitch_bend(&synth, (t * 97) % 16384);
        render(0, SR / 2);
        s1_note_off(&synth, 36 + t % 48);
        s1_note_off(&synth, 43 + t % 48);
        render(SR / 2, SR / 2);
        for (i = 0; i < SR; i++) {
            if (isnan(L[i]) || isnan(R[i]) || isinf(L[i]) || isinf(R[i])) { bad++; break; }
            if (fabsf(L[i]) > worst) worst = fabsf(L[i]);
            if (fabsf(R[i]) > worst) worst = fabsf(R[i]);
        }
        if (peak(0, SR) > 16.0f) {
            if (loud < 5)
                printf("  loud: patch %d effect %d %s peak %.1f (filter %d res %d fb %d cho fb %d)\n", t, type,
                       v[S1P_EFFECT_ON] ? "on" : "off", (double)peak(0, SR), (int)v[S1P_FILTER_TYPE],
                       (int)v[S1P_FILTER_RES], (int)v[S1P_DELAY_FEEDBACK], (int)v[S1P_CHORUS_FEEDBACK]);
            loud++;
        }
    }
    CHECK(bad == 0, "%d of 400 random patches gave NaN or inf", bad);
    CHECK(loud == 0, "%d of 400 random patches ran away (worst peak %.1f)", loud, (double)worst);
    printf("  fuzz: worst peak %.2f over 400 random patches\n", (double)worst);
}

static int gated(void)
{
    int k, n = 0;
    for (k = 0; k < S1_MAX_VOICES; k++)
        n += synth.voice[k].active && synth.voice[k].gate;
    return n;
}

static void test_performance_hold(void)
{
    int32_t v[S1_NPARAMS];
    int k;
    plain(v, 1);
    setup(v);
    s1_note_on(&synth, 60, 100);
    s1_set_hold(&synth, 1);             /* capture a chord already under the fingers */
    s1_note_on(&synth, 64, 80);
    s1_note_off(&synth, 60);
    s1_note_off(&synth, 64);
    render(0, SR / 4);
    CHECK(gated() == 2 && peak(0, SR / 4) > 0.05f, "STACK did not hold a sounding chord");
    s1_note_on(&synth, 60, 100);        /* play a droning note again: remove it */
    s1_note_off(&synth, 60);
    CHECK(gated() == 1 && !synth.latched[60], "STACK did not toggle its note off");
    s1_note_on(&synth, 67, 100);
    s1_set_hold(&synth, 0);
    CHECK(gated() == 1 && synth.key_down[67], "hold off released a physical key or kept a latch");
    s1_note_off(&synth, 67);
    render(0, SR);
    CHECK(s1_active_voices(&synth) == 0, "drone off left a voice");

    setup(v);
    synth.drone_replace = 1;
    s1_set_hold(&synth, 1);
    s1_note_on(&synth, 60, 100);
    s1_note_on(&synth, 64, 100);
    s1_note_off(&synth, 60);
    s1_note_off(&synth, 64);
    s1_note_on(&synth, 65, 100);
    s1_note_on(&synth, 69, 100);
    CHECK(gated() == 2 && !synth.latched[60] && !synth.latched[64], "REPLACE kept the old chord");
    s1_note_off(&synth, 65);
    s1_note_on(&synth, 72, 100);        /* overlapping physical gestures add notes */
    CHECK(gated() == 3 && synth.latched[65], "REPLACE replaced before all fingers were up");
    s1_note_off(&synth, 69);
    s1_note_off(&synth, 72);

    s1_set_param(&synth, S1P_ARP_ON, 1);
    CHECK(synth.arp_n == 3 && synth.hold, "drone -> arp lost the held chord");
    render(0, SR / 4);
    CHECK(synth.arp_playing >= 0, "held arp did not run");
    s1_note_on(&synth, 48, 110);
    s1_note_on(&synth, 55, 90);
    s1_note_off(&synth, 48);
    s1_note_off(&synth, 55);
    CHECK(synth.arp_n == 2 && synth.arp_keys[0] == 48 && synth.arp_keys[1] == 55,
          "arp hold did not replace the chord");
    s1_set_param(&synth, S1P_ARP_ON, 0);
    CHECK(gated() == 2 && synth.arp_n == 0 && synth.hold, "arp -> drone lost its chord");

    /* Pedal-up must leave the latch; hold-off must leave the pedal. */
    s1_control_change(&synth, 64, 127);
    s1_note_on(&synth, 62, 100);
    s1_note_off(&synth, 62);
    s1_control_change(&synth, 64, 0);
    CHECK(synth.latched[62] && gated() == 1, "pedal-up cancelled a drone");
    s1_control_change(&synth, 64, 127);
    s1_note_on(&synth, 70, 100);
    s1_note_off(&synth, 70);
    s1_set_hold(&synth, 0);
    CHECK(gated() == 1 && synth.sustained[70], "hold-off cancelled the pedal");
    s1_control_change(&synth, 64, 0);
    CHECK(gated() == 0, "pedal-up after hold-off left a note");

    /* Clearing is independent of polyphony and of how many STACK notes were stolen. */
    s1_set_hold(&synth, 1);
    synth.drone_replace = 0;
    for (k = 36; k < 84; k++) {
        s1_note_on(&synth, k, 100);
        s1_note_off(&synth, k);
    }
    s1_control_change(&synth, 120, 0);
    CHECK(!synth.hold && !synth.sustain && gated() == 0, "panic left hold, pedal or notes enabled");
    for (k = 0; k < 128; k++)
        CHECK(!synth.latched[k] && !synth.key_down[k], "panic left note %d latched", k);
}

int main(void)
{
    test_math();
    test_parser();
    test_tuning();
    test_levels_and_env();
    test_polyphony_and_modes();
    test_performance_hold();
    test_fuzz();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
