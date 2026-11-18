/* A Synth1-compatible synthesizer engine in portable C, sized for the M-VAVE FM-1:
 * float only, no libm, no allocation, fixed memory. It plays .sy1 patches (s1_patch.h).
 *
 * The parameter laws follow the measurements published by oxo_synth2 (MIT); the signal
 * path is a lighter reimplementation of the same structure (see README.md for what is
 * simplified). Nothing of the original Synth1 is used. */
#pragma once
#include <stdint.h>
#include "s1_tables.h"

#ifndef S1_MAX_VOICES
#define S1_MAX_VOICES 16
#endif
#define S1_CHUNK 16          /* control rate: envelopes, LFOs, pitch and cutoff move every 16 frames */
#define S1_DETUNE_OSCS 8

typedef struct {
    float sample_rate;       /* 44100 on the FM-1 */
    int max_voices;          /* 1 .. S1_MAX_VOICES: the CPU's limit, under the patch's polyphony */
    int16_t *delay_buf;      /* 2 * delay_len samples, or 0: no delay */
    uint32_t delay_len;      /* frames per channel, a power of two (the longest echo) */
    float *chorus_buf;       /* 2 * chorus_len samples, or 0: no chorus */
    uint32_t chorus_len;     /* frames per channel, a power of two, 4096 or more at 44.1 kHz */
} s1_config;

typedef struct { uint8_t stage; float level; } s1_env;
typedef struct { float att, dec, rel, sus; } s1_envc;
typedef struct { float b0, b1, b2, a1, a2, z[2][2]; } s1_biquad;
typedef struct { float g, b, x1[2], y1[2]; uint8_t hp, bypass; } s1_onepole;

typedef struct {
    uint8_t active, gate, note, vel, fresh, slot_i, uni_k;
    uint64_t age;
    float glide, glide_aim;          /* the pitch now (semitones), and how far past the target a glide aims */
    float uni_semis, pan_l, pan_r;
    float vel_gain, fvel;
    uint32_t ph1, ph2, phsub, phc[S1_DETUNE_OSCS], noise;
    s1_env aenv, fenv, menv;
    float gain;                      /* amp gain at the end of the last chunk */
    float fco;                       /* filter coefficient at the end of the last chunk */
    float low, band;                 /* 12 dB filters */
    float st[4], fb, agc_env, lad_gain;   /* 24 dB ladder */
    float lim_env;
} s1_voice;

typedef struct {
    float phase, prev, cur, value;
    uint32_t rng;
} s1_lfo;

typedef struct s1_synth {
    s1_config cfg;
    float inv_sr;
    int32_t base[S1_NPARAMS];        /* the patch */
    int32_t val[S1_NPARAMS];         /* what plays: the patch plus the MIDI controllers' routing */
    int routed[2];
    uint8_t dirty;
    float tempo;
    float volume;

    /* derived from val[] when it changes (s1_synth.c cook) */
    struct {
        s1_envc amp, fil, mod;
        float amp_gain, vel_k, mix_a, mix_b, main_w, sub_w, width, fm_beta, fm_scale, sat_a;
        float det_ratio[S1_DETUNE_OSCS], det_gain;
        float svf_d, svf_g, lad_k, kbd, env_depth, mod_amt;
        float base_off, osc2_off, tune, glide_a;
        float pan_l, pan_r;
        float lfo_inc[2];
        float ring_gain;
        /* effects */
        s1_biquad fx_hp, fx_lp, eq;
        s1_onepole eq_tone, dly_tone;
        float fx_drive, fx_out, fx_peak, fx_mix, fx_inc, fx_q, fx_a, fx_gmax4, fx_h, fx_step, fx_k;
        int fx_period, fx_stages;
        float dly_tl, dly_tr, dly_fb, dly_wet, dly_dry;
        float cho_base, cho_depth, cho_fb, cho_level, cho_inc, cho_theta_r;
        uint8_t eq_on;
    } ck;

    /* per chunk */
    float bend_semis, lfo_pitch_all, lfo_pitch2, lfo_cut, lfo_amp, lfo_pw, lfo_fm, lfo_pan;
    uint8_t has_cut, has_pw, has_fm, has_pan;
    float cutoff, cutoff_target, cutoff_rate;

    s1_voice voice[S1_MAX_VOICES];
    s1_lfo lfo[2];
    int last_voice;
    uint64_t now;                      /* sample time, shared by voices that start together */

    /* keys */
    uint8_t key_down[128], sustained[128], key_vel[128];
    uint8_t sustain;
    uint8_t hold, drone_replace;
    uint8_t latched[128];              /* 1: held by SEQ; 2: STACK note toggled off until key-up */
    uint8_t held[32];
    int held_n;
    int bend, pressure;
    uint8_t cc[128];

    /* arpeggiator */
    uint8_t arp_keys[32];
    int arp_n, arp_step, arp_dir, arp_vel, arp_playing;
    int arp_count, arp_gate;
    float arp_acc;
    uint8_t arp_ext;                   /* 1: MIDI clock steps the arpeggiator (s1_clock) */
    uint32_t clk_pos;                  /* position in the repeating grid of all arp divisions */
    uint32_t rng;

    /* effects' state */
    float fx_fb[2], fx_ap[2][12], fx_apfb[2], fx_hold[2], fx_held[2], fx_env[2], fx_phase, fx_pos, fx_dir;
    int fx_hold_n;
    uint32_t dly_pos;
    float dly_sl, dly_sr, dly_glide;
    uint32_t cho_pos;
    float cho_phase;
} s1_synth;

void s1_init(s1_synth *s, const s1_config *cfg);
void s1_set_patch(s1_synth *s, const int32_t values[S1_NPARAMS]);   /* silences the voices, empties the delay */
void s1_set_param(s1_synth *s, int id, int32_t value);
int32_t s1_get_param(const s1_synth *s, int id);
void s1_set_tempo(s1_synth *s, float bpm);
void s1_set_volume(s1_synth *s, float gain);

void s1_note_on(s1_synth *s, int key, int velocity);
void s1_note_off(s1_synth *s, int key);
enum { S1_CLOCK_TICK, S1_CLOCK_START, S1_CLOCK_OFF };
void s1_clock(s1_synth *s, int what);   /* MIDI clock: a tick (24 a quarter note), Start, or no clock any more.
                                        * With a clock the arpeggiator steps on its grid, counted from Start */
void s1_set_hold(s1_synth *s, int on);   /* arp: replace chord after key-up; otherwise drone */
void s1_control_change(s1_synth *s, int cc, int value);
void s1_pitch_bend(s1_synth *s, int value14);
void s1_pressure(s1_synth *s, int value);
void s1_all_notes_off(s1_synth *s, int immediate);
int s1_active_voices(const s1_synth *s);

/* n stereo frames, full scale 1.0 (one voice peaks near 0.2, as Synth1's does) */
void s1_render(s1_synth *s, float *left, float *right, int n);
