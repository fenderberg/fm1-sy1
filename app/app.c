/* SY1: the app. The engine (dsp/) plays in the audio interrupt; the main loop reads the panel, draws the screen,
 * keeps the patches and hands the engine its changes through a small queue. Part of the unity build
 * (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "s1_synth.h"
#include "s1_patch.h"
#include "s1_fx.h"
#include "bank.h"
#include "s1_math.h"

static int ux_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

#define SR 44100.0f
#define MAX_VOICES_DEFAULT 8             /* until measured on the FM-1; GLOBAL > VOICES changes it, an overload lowers it */
#define OUT_GAIN 2.0f                    /* one voice peaks near 0.2 of full scale, as Synth1's does */
#define DELAY_LEN 32768u                 /* 0.74 s a side, 16-bit: 128 KiB of the pool (longer echoes play an octave shorter) */
#define CHORUS_LEN 4096u

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x53593131u            /* "SY11" */
enum { DISPLAY_NORD, DISPLAY_GREEN, DISPLAY_LCD, DISPLAY_COUNT };
#define SET_LEGACY_BYTES 12u
#define SET_V2_BYTES 16u                 /* 0.3: up to patch_b */
#define SET_V3_BYTES 20u                 /* up to octave_b */
typedef struct {
    uint32_t magic;
    uint16_t patch;                      /* the list's index: the built-in ones, then the user's slots */
    int8_t octave;                       /* -3 .. +3 */
    uint8_t voices;                      /* 1 .. S1_MAX_VOICES */
    uint16_t tempo;                      /* 30 .. 300 */
    uint8_t category, drone_replace;     /* uses the old reserved bytes: settings stay compatible. category: the
                                          * browser's, of the part the panel is on (category_b: the other part's) */
    uint8_t display;                     /* appended: old 12-byte settings migrate without losing patches */
    uint8_t bi;                          /* bit 0: bitimbral; bits 1-2: its output (LAY_*); bit 3: the panel is on part R */
    uint16_t patch_b;                    /* part R's patch (the list's index) */
    uint8_t ch_l, ch_r;                  /* appended again: the parts' MIDI channels in bitimbral, 1 .. 16 (0: 3 and 4) */
    uint8_t category_b;                  /* the browser's category of the part the panel is not on */
    int8_t octave_b;                     /* and that part's octave. Bitimbral: octave, voices, category and
                                          * drone_replace above are the part's the panel is on; these the other's */
    uint8_t voices_b, drone_b, spare[2]; /* appended a third time: its voice limit and drone mode */
} settings_t;
static settings_t set;
enum { LAY_SPLIT, LAY_STEREO, LAY_DOUBLE, LAY_COUNT };
#define BI_ON (set.bi & 1u)
#define BI_LAYOUT ((uint32_t)(set.bi >> 1) & 3u)
#define CH_L (set.ch_l ? set.ch_l : 3u)   /* the Circuit Tracks' MIDI 1 */
#define CH_R (set.ch_r ? set.ch_r : 4u)   /* and MIDI 2 */

static void settings_defaults(void)
{
    set = (settings_t){0};
    set.magic = SET_MAGIC;
    set.patch = 0;
    set.octave = 0;
    set.voices = MAX_VOICES_DEFAULT;
    set.tempo = 120;
    set.category = CAT_ALL;
    set.drone_replace = 0;
}

/* ----------------------------------------------------------------- engine --- */
static s1_synth synth;
static int16_t delay_buf[2u * DELAY_LEN] PLAT_POOL;
static float chorus_buf[2u * CHORUS_LEN] PLAT_POOL;
static float buf_l[APP_BLOCK], buf_r[APP_BLOCK];
/* Bitimbral: a second part, R, beside part L (synth). Each has its own patch, arp, hold and effects; R renders on
 * the second core when it runs (fm1.toml cpu2), else after L on the first. Standard mode never renders R. */
#define DELAY_LEN_B 16384u               /* 0.37 s a side: 64 KiB, what is left of the pool (longer echoes an octave shorter) */
static s1_synth synth_b;
static s1_synth *const PART[2] = {&synth, &synth_b};
static int16_t delay_buf_b[2u * DELAY_LEN_B] PLAT_POOL;
static float chorus_buf_b[2u * CHORUS_LEN] PLAT_POOL;
static float buf2_l[APP_BLOCK], buf2_r[APP_BLOCK];
static volatile uint8_t w_ch[2] = {2, 3};   /* the parts' channels in bitimbral, 0 .. 15 */
static volatile uint8_t w_bi, w_layout, w_focus;   /* the main loop's: bitimbral on, its output, the part the panel is on */
static uint8_t a_bi, a_layout;           /* as the render has them (taken at EV_MODE: the notes stop there) */
static volatile uint8_t ux_refocus;      /* the panel moved to the other part: the live view starts again */
static uint32_t dual_jobs, dual_self;    /* blocks R rendered on the second core / on the first */
static void part_b_job(uint32_t m) { s1_render(&synth_b, buf2_l, buf2_r, (int)m); }
static float gain_now;
#include "ui/live.h"

/* main loop -> audio interrupt: a ring of small events, and a whole patch at a time */
enum { EV_NOTE_ON, EV_NOTE_OFF, EV_PARAM, EV_PANIC, EV_TEMPO, EV_VOICES, EV_HOLD, EV_DRONE_MODE, EV_MODE };   /* bit 7: part R */
typedef struct { uint8_t type, a; int16_t b; } ev_t;
static ev_t evq[64];
static volatile uint32_t ev_w, ev_r;
static volatile uint8_t ev_overflow;
static int32_t patch_next[2][S1_NPARAMS];
static volatile uint8_t patch_ready[2];
static volatile int32_t want_program[2] = {-1, -1};   /* a program change from MIDI per part, for the main loop */
static volatile uint32_t ext_tempo;          /* MIDI clock's tempo, 0 none yet */
static volatile uint8_t shed;                /* the render ran out of time: the main loop lowers the voices */
static volatile uint32_t midi_notes;         /* notes in from MIDI (the screen's activity mark) */
static uint32_t clk_n, clk_t0, clk_last;
static uint8_t clk_live;                 /* clocks are coming in: the arpeggiators step on them */

static uint8_t cur_part;                 /* the part the panel is on: 0 L, 1 R (always L in standard mode) */
static void ev_push_to(uint32_t part, uint32_t type, uint32_t a, int32_t b);
static void ev_push(uint32_t type, uint32_t a, int32_t b) { ev_push_to(cur_part, type, a, b); }
static void ev_push_to(uint32_t part, uint32_t type, uint32_t a, int32_t b)
{
    const uint32_t w = ev_w;
    if (w - ev_r >= 64u) {
        ev_overflow = 1;                 /* a dropped note-off must never leave a latched sound */
        return;
    }
    evq[w & 63u].type = (uint8_t)(type | part << 7);
    evq[w & 63u].a = (uint8_t)a;
    evq[w & 63u].b = (int16_t)b;
    ev_w = w + 1u;
}

/* --------------------------------------------- the Circuit's eight knobs --- */
/* In bitimbral the Circuit Tracks' macro knobs (its factory MIDI template: these controllers) each move the part's
 * parameters that match the knob's label. The knob in its middle (64) is the patch as loaded or edited; either way
 * from there adds to it, gliding, so a knob far from where the sound is does not jump. A new patch starts from the
 * middle again. Audio interrupt, but macro_param: the main loop's popup uses it too. */
#define NMACRO 8u
typedef struct { uint8_t id; int8_t depth; } mtarget_t;   /* depth: knob units at the knob's end, from its middle */
static const uint8_t MACRO_CC[NMACRO] = {1, 2, 5, 11, 71, 74, 12, 13};
static const char *const MACRO_NAME[NMACRO] = {"OSCILLATOR", "OSC MOD", "AMP ENV", "FILTER ENV", "FILTER FREQ",
                                               "RESONANCE", "MODULATION", "FX"};
static const mtarget_t MACRO[NMACRO][2] = {
    {{S1P_OSC_MIX, 64}, {255, 0}},                       /* OSCILLATOR   the two oscillators' balance */
    {{S1P_OSC1_DETUNE, 64}, {S1P_OSC2_FINE, 24}},        /* OSC MOD      the detuned copies, osc 2 a little off */
    {{S1P_AMP_RELEASE, 64}, {S1P_AMP_DECAY, 48}},        /* AMP ENV      release and decay; the attack stays */
    {{S1P_FILTER_AMOUNT, 64}, {S1P_FILTER_DECAY, 32}},   /* FILTER ENV   its amount, and its decay a little */
    {{S1P_FILTER_FREQ, 64}, {255, 0}},                   /* FILTER FREQ  cutoff */
    {{S1P_FILTER_RES, 64}, {255, 0}},                    /* RESONANCE */
    {{S1P_LFO1_DEPTH, 64}, {255, 0}},                    /* MODULATION   LFO 1's depth (switched on above the middle) */
    {{S1P_DELAY_DRY_WET, 64}, {255, 0}},                 /* FX           the delay's level (switched on above the middle) */
};
static const uint8_t MACRO_ON[NMACRO] = {255, 255, 255, 255, 255, 255, S1P_LFO1_ON, S1P_DELAY_ON};
static struct {
    int32_t base[S1_NPARAMS];            /* the patch with the panel's edits: what the knobs add to */
    float pos[NMACRO];                   /* where each knob is now, gliding to target */
    uint8_t target[NMACRO];
    uint8_t live;                        /* the knobs still gliding */
} mac[2];
static volatile uint32_t macro_seen;     /* the last knob the Circuit moved: a count << 8 | part << 3 | knob */

static int32_t macro_param(const int32_t *base, uint32_t m, uint32_t k, float pos)
{
    const mtarget_t t = MACRO[m][k];
    const float d = (pos - 64.0f) * (float)t.depth * (1.0f / 64.0f);
    return clamp(base[t.id] + (int32_t)(d + (d < 0.0f ? -0.5f : 0.5f)), s1_param_min[t.id], s1_param_max[t.id]);
}

static void macro_apply(uint32_t p, uint32_t m)
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        if (MACRO[m][k].id != 255u) {
            const int32_t v = macro_param(mac[p].base, m, k, mac[p].pos[m]);
            if (s1_get_param(PART[p], MACRO[m][k].id) != v)
                s1_set_param(PART[p], MACRO[m][k].id, v);
        }
    if (MACRO_ON[m] != 255u) {
        const int32_t v = mac[p].pos[m] > 64.5f ? 1 : mac[p].base[MACRO_ON[m]];
        if (s1_get_param(PART[p], MACRO_ON[m]) != v)
            s1_set_param(PART[p], MACRO_ON[m], v);
    }
}

static void macro_patch(uint32_t p, const int32_t *values)   /* a new patch: the knobs count from their middle again */
{
    uint32_t i;
    for (i = 0; i < S1_NPARAMS; i++)
        mac[p].base[i] = values[i];
    for (i = 0; i < NMACRO; i++) {
        mac[p].pos[i] = 64.0f;
        mac[p].target[i] = 64;
    }
    mac[p].live = 0;
}

static void macro_param_set(uint32_t p, uint32_t id, int32_t v)   /* the panel's edit: the knobs add to the new value */
{
    uint32_t m;
    mac[p].base[id] = v;
    s1_set_param(PART[p], (int)id, v);
    for (m = 0; m < NMACRO; m++)
        if ((MACRO[m][0].id == id || MACRO[m][1].id == id || MACRO_ON[m] == id) && mac[p].pos[m] != 64.0f)
            macro_apply(p, m);
}

static int macro_cc(uint32_t p, uint32_t cc, uint32_t value)   /* 1: one of the eight knobs */
{
    uint32_t m;
    for (m = 0; m < NMACRO; m++)
        if (MACRO_CC[m] == cc) {
            mac[p].target[m] = (uint8_t)value;
            mac[p].live |= (uint8_t)(1u << m);
            macro_seen = ((macro_seen >> 8) + 1u) << 8 | p << 3 | m;
            return 1;
        }
    return 0;
}

static void macro_run(uint32_t p)        /* once a render: a knob covers its whole travel in about 0.12 s */
{
    uint32_t m;
    for (m = 0; m < NMACRO && mac[p].live; m++)
        if (mac[p].live & (1u << m)) {
            const float d = (float)mac[p].target[m] - mac[p].pos[m];
            if (d > -6.0f && d < 6.0f) {
                mac[p].pos[m] = (float)mac[p].target[m];
                mac[p].live &= (uint8_t)~(1u << m);
            } else
                mac[p].pos[m] += d < 0.0f ? -6.0f : 6.0f;
            macro_apply(p, m);
        }
}

static void macro_release(void)          /* standard again: both parts as their patches are */
{
    uint32_t p, m;
    for (p = 0; p < 2u; p++) {
        for (m = 0; m < NMACRO; m++) {
            mac[p].pos[m] = 64.0f;
            mac[p].target[m] = 64;
            macro_apply(p, m);
        }
        mac[p].live = 0;
    }
}

static void all_off(void)
{
    s1_all_notes_off(&synth, 1);
    s1_all_notes_off(&synth_b, 1);
}

static void mode_take(void)              /* audio interrupt: the mode as the main loop has it; nothing sounds across it */
{
    all_off();
    if (a_bi && !w_bi) {
        s1_fx_reset(&synth_b);           /* no echo of this session when R comes back */
        macro_release();
    }
    a_bi = w_bi;
    a_layout = w_layout;
}

static void ux_restart(void)             /* audio interrupt: the live view follows another patch or part */
{
    ux_track = -1;
    ux_track_age = 0;
    ux_audio_frames = 2940u;
    ux_render_generation = ux_wanted_generation;
}

static void events(void)                 /* audio interrupt */
{
    uint32_t p;
    for (p = 0; p < 2u; p++)
        if (patch_ready[p]) {
            s1_set_patch(PART[p], patch_next[p]);
            macro_patch(p, patch_next[p]);
            if (p == w_focus)
                ux_restart();
            patch_ready[p] = 0;
        }
    if (ux_refocus) {
        ux_refocus = 0;
        ux_restart();
    }
    if (ev_overflow) {
        ev_r = ev_w;
        ev_overflow = 0;
        mode_take();
        return;
    }
    while (ev_r != ev_w) {
        const ev_t e = evq[ev_r & 63u];
        s1_synth *const part = PART[e.type >> 7];
        ev_r++;
        switch (e.type & 0x7Fu) {
        case EV_NOTE_ON: s1_note_on(part, e.a, e.b); break;
        case EV_NOTE_OFF: s1_note_off(part, e.a); break;
        case EV_PARAM: macro_param_set(e.type >> 7, e.a, e.b); break;
        case EV_PANIC: all_off(); break;
        case EV_TEMPO: s1_set_tempo(&synth, (float)e.b); s1_set_tempo(&synth_b, (float)e.b); break;
        case EV_HOLD: s1_set_hold(part, e.b); break;
        case EV_DRONE_MODE: part->drone_replace = (uint8_t)e.b; break;
        case EV_MODE: mode_take(); break;
        case EV_VOICES:
            s1_all_notes_off(part, 1);
            part->cfg.max_voices = clamp(e.b, 1, S1_MAX_VOICES);
            break;
        }
    }
}

static void midi_in(void)                /* audio interrupt: USB, the TRS jack, Bluetooth */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        const uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        /* Standard: every channel plays L. Bitimbral: channel 3 is part L, channel 4 part R (the Circuit Tracks'
         * MIDI 1 and MIDI 2), the other channels are not ours; DOUBLE: the notes of either play both parts */
        const uint32_t ch = st & 0x0Fu, kind = st & 0xF0u, note = kind == 0x90u || kind == 0x80u;
        const uint32_t mask = st >= 0xF0u ? 0u : !a_bi ? 1u : ch != w_ch[0] && ch != w_ch[1] ? 0u :
            note && a_layout == LAY_DOUBLE ? 3u : ch == w_ch[0] ? 1u : 2u;
        uint32_t p;
        if (st < 0xF0u) {
            for (p = 0; p < 2u; p++) {
                s1_synth *const part = PART[p];
                if (!(mask & (1u << p)))
                    continue;
                switch (kind) {
                case 0x90u:
                    if (d2) {
                        s1_note_on(part, (int)d1, (int)d2);
                        midi_notes++;
                        break;
                    }
                    /* fall through */
                case 0x80u: s1_note_off(part, (int)d1); break;
                case 0xB0u:
                    if (!a_bi || !macro_cc(p, d1, d2))
                        s1_control_change(part, (int)d1, (int)d2);
                    break;
                case 0xC0u: want_program[p] = (int32_t)d1; break;
                case 0xD0u: s1_pressure(part, (int)d1); break;
                case 0xE0u: s1_pitch_bend(part, (int)(d1 | d2 << 7)); break;
                default: break;
                }
            }
            continue;
        }
        switch (st & 0xF0u) {
        default:
            if (st == 0xF8u) {           /* MIDI clock: the tempo over each quarter note (24 ticks) */
                clk_last = synth.now;
                clk_live = 1;
                if (clk_n == 0u)
                    clk_t0 = synth.now;
                else if (clk_n == 24u) {
                    const uint32_t span = synth.now - clk_t0;
                    if (span > 4000u && span < 100000u) {
                        const float exact = 60.0f * SR / (float)span;
                        const uint32_t bpm = (uint32_t)(exact + 0.5f);
                        if (bpm != ext_tempo) {
                            ext_tempo = bpm;     /* (the screen's; the delay and LFOs follow the whole number too) */
                            s1_set_tempo(&synth, (float)bpm);
                            s1_set_tempo(&synth_b, (float)bpm);
                        }
                    }
                    clk_t0 = synth.now;
                    clk_n = 0u;
                }
                clk_n++;
                s1_clock(&synth, S1_CLOCK_TICK);   /* the arpeggiators step on the clock's grid */
                s1_clock(&synth_b, S1_CLOCK_TICK);
            } else if (st == 0xFAu || st == 0xFCu) {
                clk_n = 0u;
                if (st == 0xFAu) {       /* Start: the arpeggiators' patterns from the top, on the bar */
                    s1_clock(&synth, S1_CLOCK_START);
                    s1_clock(&synth_b, S1_CLOCK_START);
                }
                if (st == 0xFCu)
                    all_off();
            }
            break;
        }
    }
    if (clk_live && synth.now - clk_last > 88200u) {  /* unplugged clock: return to the internal tempo */
        ext_tempo = 0;
        clk_live = 0;
        clk_n = 0;
        s1_set_tempo(&synth, (float)set.tempo);
        s1_set_tempo(&synth_b, (float)set.tempo);
        s1_clock(&synth, S1_CLOCK_OFF);
        s1_clock(&synth_b, S1_CLOCK_OFF);
    }
}

/* the output's ceiling: straight up to 0.75 of full scale, then bending toward 1 (a big chord is squeezed, not cut) */
static inline float soft(float x)
{
    const float a = x < 0.0f ? -x : x;
    float y;
    if (a <= 0.75f)
        return x;
    y = 1.0f - 0.0625f / (a - 0.5f);
    return x < 0.0f ? -y : y;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    const float gain = (float)master_q12 * (1.0f / 4096.0f) * 8388607.0f;
    uint32_t pos, i;
    events();
    midi_in();
    macro_run(0);
    macro_run(1);
    for (pos = 0; pos < n; pos += APP_BLOCK) {
        const uint32_t m = n - pos < APP_BLOCK ? n - pos : APP_BLOCK;
        const float step = (gain - gain_now) / (float)m;   /* MASTER glides over the block: no click */
        const int two = a_bi, on2 = two && plat_cpu2_on() && plat_cpu2_run(part_b_job, m);
        s1_render(&synth, buf_l, buf_r, (int)m);
        if (two) {
            if (!on2 || plat_cpu2_wait() != 0) {
                part_b_job(m);           /* one core, or the second dropped the job */
                dual_self++;
            } else
                dual_jobs++;
        }
        for (i = 0; i < m; i++) {
            /* SPLIT: L on the left only, R on the right only, each part summed to mono; else both in stereo */
            const float l = !two ? buf_l[i] : a_layout == LAY_SPLIT ? 0.5f * (buf_l[i] + buf_r[i]) : buf_l[i] + buf2_l[i];
            const float r = !two ? buf_r[i] : a_layout == LAY_SPLIT ? 0.5f * (buf2_l[i] + buf2_r[i]) : buf_r[i] + buf2_r[i];
            gain_now += step;
            lr[2u * (pos + i)] = (int32_t)(soft(l * OUT_GAIN) * gain_now);
            lr[2u * (pos + i) + 1u] = (int32_t)(soft(r * OUT_GAIN) * gain_now);
        }
        gain_now = gain;
    }
    ux_publish(n);
}

void app_panic(void)
{
    all_off();
    gain_now = 0.0f;
}

void app_overload(void) { shed = 1; }

/* ---------------------------------------------------------------- patches --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t held_note[NKEYS];            /* the note each held key started (the octave may change under it) */
    uint32_t page, row;
    uint32_t arp_ms;
    uint8_t arp_down;                   /* tap toggles; hold opens/advances the settings page */
    uint8_t held_part[NKEYS];            /* the parts each held key plays (bit 0 L, bit 1 R) */
    uint32_t oct_ms;                     /* OCT- / OCT+: when the gesture began */
    uint8_t oct_used;                    /* it has acted (both: the mode; bitimbral, held: the octave) */
    int8_t oct_step;                     /* the octave step it made, taken back when the other OCT follows */
    uint8_t status;
    uint8_t redraw;
    int8_t ux_focus;
    uint8_t ux_page, ux_row, ux_rows[8], ux_last_id;
    uint8_t msg_priority;
    uint32_t ref_serial;
} ui;

enum { UX_HEAD = 1, UX_BAR = 2, UX_GRAPH = 4, UX_CELLS = 8, UX_FOOT = 16, UX_ALL = 31 };
static uint32_t shown_voices, shown_cpu, shown_midi;
static uint32_t cpu_high, cpu_sheds;

static s1_patch cur;                     /* the patch playing, with the player's edits */
static uint32_t cur_index;
static uint8_t cur_dirty, set_dirty;
static uint8_t cur_category;
static int32_t saved_values[S1_NPARAMS];
static uint8_t saved_category;
static char saved_name[S1_NAME_LEN];

static void patch_reference(void)
{
    uint32_t i;
    for (i = 0; i < S1_NPARAMS; i++) saved_values[i] = cur.values[i];
    for (i = 0; i < S1_NAME_LEN; i++) saved_name[i] = cur.name[i];
    saved_category = cur_category;
    ui.ref_serial++;
    cur_dirty = 0;
}

/* The other part's patch, as the main loop keeps it. The names above (cur, cur_index ..) are always the part the
 * panel is on; part_swap exchanges the two. */
static struct {
    s1_patch cur;
    uint32_t index;
    uint32_t page, row;                  /* where the panel was on it */
    uint8_t ux_rows[8];
    uint8_t dirty, category, saved_category;
    int32_t saved_values[S1_NPARAMS];
    char saved_name[S1_NAME_LEN];
} other;

static void ux_live_reset(void)
{
    uint32_t j;
    ui.ux_focus = -1;
    ux_live.active = 0;
    ux_live.stage[0] = ux_live.stage[1] = ux_live.stage[2] = 0;
    ux_live.sequence = ux_audio.sequence;
    for (j = 0; j < 32; j++) ux_lfo_history[0][j] = ux_lfo_history[1][j] = 0;
    ux_lfo_position = 0;
}

static void part_swap(void)
{
    uint32_t i;
    const s1_patch p = cur;
    const uint32_t index = cur_index;
    const uint8_t dirty = cur_dirty, category = cur_category, sc = saved_category;
    cur = other.cur; other.cur = p;
    cur_index = other.index; other.index = index;
    cur_dirty = other.dirty; other.dirty = dirty;
    cur_category = other.category; other.category = category;
    saved_category = other.saved_category; other.saved_category = sc;
    for (i = 0; i < S1_NPARAMS; i++) {
        const int32_t v = saved_values[i];
        saved_values[i] = other.saved_values[i]; other.saved_values[i] = v;
    }
    for (i = 0; i < S1_NAME_LEN; i++) {
        const char c = saved_name[i];
        saved_name[i] = other.saved_name[i]; other.saved_name[i] = c;
    }
    {                                    /* each part is a synth of its own: its browser's category, octave, voice
                                          * limit, drone mode and the page the panel was on. The tempo is shared */
        const uint8_t c = set.category, v = set.voices, dr = set.drone_replace;
        const int8_t o = set.octave;
        const uint32_t page = ui.page, row = ui.row;
        set.category = set.category_b; set.category_b = c;
        set.voices = set.voices_b; set.voices_b = v;
        set.drone_replace = set.drone_b; set.drone_b = dr;
        set.octave = set.octave_b; set.octave_b = o;
        ui.page = other.page; other.page = page;
        ui.row = other.row; other.row = row;
        for (i = 0; i < 8u; i++) {
            const uint8_t r = ui.ux_rows[i];
            ui.ux_rows[i] = other.ux_rows[i]; other.ux_rows[i] = r;
        }
    }
    cur_part ^= 1u;
    w_focus = cur_part;
    ui.ref_serial++;
    ux_live_reset();
    ux_wanted_generation++;
    ux_refocus = 1;
    ui.redraw = UX_ALL;
}

static void refresh_dirty(void)
{
    uint32_t i;
    cur_dirty = cur_category != saved_category || !ux_equal(cur.name, saved_name);
    for (i = 0; i < S1_NPARAMS && !cur_dirty; i++) cur_dirty = cur.values[i] != saved_values[i];
}

static uint32_t list_count(void) { return FACTORY500_END; }
static int list_has(uint32_t i) { return i < NFACT || factory500_has(i) || (user_index(i) && bank_used(i - NFACT)); }

static uint8_t patch_category(uint32_t i)
{
    return i < NFACT ? category_guess(FACT[i].name) : factory500_has(i) ?
        FACTORY500[i - FACTORY500_BASE][2] : bank_category[i - NFACT];
}

static int category_matches(uint32_t i)
{
    return list_has(i) && (set.category == CAT_ALL ||
        (set.category == CAT_USER ? user_index(i) : set.category == CAT_FACTORY ? factory500_has(i) : patch_category(i) == set.category));
}

static void to_engine(void)
{
    uint32_t i;
    patch_ready[cur_part] = 0;           /* the interrupt leaves a half-written patch alone */
    for (i = 0; i < S1_NPARAMS; i++)
        patch_next[cur_part][i] = cur.values[i];
    ux_wanted_generation++;
    patch_ready[cur_part] = 1;
}

static void patch_select(uint32_t i)
{
    if (i >= list_count() || !list_has(i))
        i = 0;
    if (i < NFACT)
        fact_load(i, &cur);
    else if (factory500_has(i))
        bank_decode(FACTORY500[i - FACTORY500_BASE], &cur);
    else
        bank_load(i - NFACT, &cur);
    cur_index = i;
    cur_category = patch_category(i);
    if (!category_matches(i)) {
        set.category = CAT_ALL;          /* a MIDI program change stays reachable in the browser */
        set_dirty = 1;
    }
    patch_reference();
    ux_live_reset();
    {
        uint16_t *const kept = cur_part ? &set.patch_b : &set.patch;
        if (*kept != i) {
            *kept = (uint16_t)i;
            set_dirty = 1;
        }
    }
    to_engine();
    ui.redraw = UX_ALL;
}

static void patch_step(int32_t d)        /* to the next patch that exists, d of them on, round the ends */
{
    const uint32_t n = list_count();
    uint32_t i = cur_index;
    int32_t left = d < 0 ? -d : d;
    while (left > 0) {
        uint32_t tried = 0;
        do {
            i = d > 0 ? (i + 1u) % n : (i + n - 1u) % n;
            tried++;
        } while (!category_matches(i) && tried < n);
        if (!category_matches(i)) {
            set.category = CAT_ALL;      /* the manager may have removed the last matching sound */
            set_dirty = 1;
            continue;
        }
        left--;
    }
    if (i != cur_index)
        patch_select(i);
}

static void other_select(uint32_t i)     /* a patch for the part the panel is not on */
{
    part_swap();
    patch_select(i);
    part_swap();
}

static void category_step(int32_t d)
{
    const uint32_t n = list_count();
    uint32_t cat = set.category, tries, i;
    int32_t left = d < 0 ? -d : d;
    while (left-- > 0) {
        for (tries = 0; tries < CAT_COUNT; tries++) {
            cat = d > 0 ? (cat + 1u) % CAT_COUNT : (cat + CAT_COUNT - 1u) % CAT_COUNT;
            set.category = (uint8_t)cat;
            for (i = 0; i < n && !category_matches(i); i++) {}
            if (i < n)
                break;                  /* skip categories with no sounds */
        }
    }
    set_dirty = 1;
    if (!category_matches(cur_index)) {
        for (i = 0; i < n && !category_matches(i); i++) {}
        patch_select(i);
    }
    ui.redraw = UX_ALL;
}

/* ---------------------------------------------------------------- strings --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void num3(uint32_t v, char *b)    /* three digits */
{
    b[0] = (char)('0' + v / 100u % 10u);
    b[1] = (char)('0' + v / 10u % 10u);
    b[2] = (char)('0' + v % 10u);
    b[3] = 0;
}

static void index_text(uint32_t i, char *b)   /* P03: built in; 012: the user's slot 12 */
{
    if (factory500_has(i)) {
        b[0] = 'F'; num3(i - FACTORY500_BASE + 1u, b + 1);
    } else if (i < NFACT) {
        b[0] = 'P';
        b[1] = (char)('0' + (i + 1u) / 10u % 10u);
        b[2] = (char)('0' + (i + 1u) % 10u);
        b[3] = 0;
    } else
        num3(i - NFACT + 1u, b);
}

/* ------------------------------------------------------------------ pages --- */
enum { X_TEMPO = 100, X_VOICES, X_DRONE, X_CATEGORY, X_CPU, X_PEAK, X_SHED, X_RAW, X_DISPLAY, X_BI, X_LAYOUT, X_CHL, X_CHR, X_NONE = 255 };

static const char *const PNAME[S1_NPARAMS] = {
    "OSC1", "OSC2", "PITCH", "FINE", "TRACK", "MIX", "SYNC", "RING", "PW", "SHIFT",
    "M.ENV", "M.AMT", "M.ATK", "M.DEC", "TYPE", "ATTACK", "DECAY", "SUSTN", "RELEAS", "FREQ",
    "RESO", "AMOUNT", "KBD", "SATUR", "VEL.SW", "ATTACK", "DECAY", "SUSTN", "RELEAS", "GAIN",
    "VELOC", "TYPE", "RANGE", "BEAT", "GATE", "TIME", "FEEDBK", "WET", "MODE", "PORTA",
    "BEND", "L1 DST", "L1 WAV", "L1 SPD", "L1 DEP", "FM", "L2 DST", "L2 WAV", "L2 SPD", "L2 DEP",
    "WHEEL1", "WHEEL2", "TIME", "DEPTH", "RATE", "FEEDBK", "LEVEL", "LFO1", "LFO2", "ARP",
    "TONE", "EQ FRQ", "EQ LVL", "EQ Q", "TYPE", "DELAY", "CHORUS", "L1 SYN", "L1 KEY", "L2 SYN",
    "L2 KEY", "M.DEST", "TUNE", "UNISON", "P.AUTO", "U.DET", "DETUNE", "EFFECT", "TYPE", "CTL1",
    "CTL2", "LEVEL", "TYPE", "SPREAD", "U.PAN", "U.PIT", "", "", "", "",
    "PAN", "PHASE", "U.PHAS", "U.VOIC", "POLY", "SUB", "S.WAVE", "S.OCT", "D.TONE",
};

typedef struct { const char *title; uint8_t button, nrows; const uint8_t (*rows)[4]; } page_t;
static const uint8_t R_HOME[][4] = {
    {S1P_FILTER_FREQ, S1P_FILTER_RES, S1P_FILTER_AMOUNT, S1P_AMP_GAIN},
    {S1P_PORTA_TIME, S1P_PAN, X_TEMPO, X_VOICES}};
static const uint8_t R_OSC[][4] = {
    {S1P_OSC1_SHAPE, S1P_OSC2_SHAPE, S1P_OSC_MIX, S1P_OSC_PULSE_WIDTH},
    {S1P_OSC2_PITCH, S1P_OSC2_FINE, S1P_OSC2_KBD_TRACK, S1P_OSC2_SYNC},
    {S1P_OSC2_RING, S1P_OSC1_FM, S1P_OSC1_DETUNE, S1P_OSC_PHASE},
    {S1P_SUB_GAIN, S1P_SUB_SHAPE, S1P_SUB_OCTAVE, S1P_OSC_KEY_SHIFT},
    {S1P_MOD_ENV_ON, S1P_MOD_ENV_DEST, S1P_MOD_ENV_AMOUNT, S1P_OSC_FINE_TUNE},
    {S1P_MOD_ENV_ATTACK, S1P_MOD_ENV_DECAY, X_NONE, X_NONE}};
static const uint8_t R_FILTER[][4] = {
    {S1P_FILTER_TYPE, S1P_FILTER_FREQ, S1P_FILTER_RES, S1P_FILTER_AMOUNT},
    {S1P_FILTER_KBD_TRACK, S1P_FILTER_SAT, S1P_FILTER_VEL_SWITCH, X_NONE}};
static const uint8_t R_ENV[][4] = {
    {S1P_AMP_ATTACK, S1P_AMP_DECAY, S1P_AMP_SUSTAIN, S1P_AMP_RELEASE},
    {S1P_FILTER_ATTACK, S1P_FILTER_DECAY, S1P_FILTER_SUSTAIN, S1P_FILTER_RELEASE},
    {S1P_AMP_GAIN, S1P_AMP_VEL_SENS, X_NONE, X_NONE}};
static const uint8_t R_LFO[][4] = {
    {S1P_LFO1_ON, S1P_LFO1_DEST, S1P_LFO1_TYPE, S1P_LFO1_SPEED},
    {S1P_LFO1_DEPTH, S1P_LFO1_TEMPO_SYNC, S1P_LFO1_KEY_SYNC, S1P_WHEEL_SPEED_SENS},
    {S1P_LFO2_ON, S1P_LFO2_DEST, S1P_LFO2_TYPE, S1P_LFO2_SPEED},
    {S1P_LFO2_DEPTH, S1P_LFO2_TEMPO_SYNC, S1P_LFO2_KEY_SYNC, S1P_WHEEL_AMOUNT_SENS}};
static const uint8_t R_FX[][4] = {
    {S1P_EFFECT_ON, S1P_EFFECT_TYPE, S1P_EFFECT_CTRL1, S1P_EFFECT_CTRL2},
    {S1P_EFFECT_LEVEL, S1P_EQ_TONE, X_NONE, X_NONE},
    {S1P_EQ_FREQ, S1P_EQ_LEVEL, S1P_EQ_Q, X_NONE},
    {S1P_DELAY_ON, S1P_DELAY_TYPE, S1P_DELAY_TIME, S1P_DELAY_FEEDBACK},
    {S1P_DELAY_DRY_WET, S1P_DELAY_SPREAD, S1P_DELAY_TONE, X_NONE},
    {S1P_CHORUS_ON, S1P_CHORUS_TYPE, S1P_CHORUS_TIME, S1P_CHORUS_DEPTH},
    {S1P_CHORUS_RATE, S1P_CHORUS_FEEDBACK, S1P_CHORUS_LEVEL, X_NONE}};
static const uint8_t R_ARP[][4] = {
    {S1P_ARP_ON, S1P_ARP_TYPE, S1P_ARP_OCT_RANGE, S1P_ARP_BEAT},
    {S1P_ARP_GATE, X_TEMPO, X_DRONE, X_NONE}};
static const uint8_t R_VOICE[][4] = {
    {S1P_PLAY_MODE, S1P_PORTA_TIME, S1P_PORTA_AUTO, S1P_BEND_RANGE},
    {S1P_UNISON_ON, S1P_UNISON_VOICES, S1P_UNISON_DETUNE, S1P_UNISON_PAN},
    {S1P_UNISON_PITCH, S1P_UNISON_PHASE, S1P_POLYPHONY, S1P_PAN},
    {X_VOICES, X_TEMPO, X_CATEGORY, X_DRONE},
    {X_CPU, X_PEAK, X_SHED, X_RAW},
    {X_DISPLAY, X_NONE, X_NONE, X_NONE},
    {X_BI, X_LAYOUT, X_CHL, X_CHR}};
#define ROWS(r) (uint8_t)(sizeof r / sizeof r[0]), r
static const page_t PAGES[] = {
    {"PLAY", B_HOME, ROWS(R_HOME)},     {"OSC", B_EDIT, ROWS(R_OSC)},   {"FILTER", B_SEL, ROWS(R_FILTER)},
    {"ENV", B_ENV, ROWS(R_ENV)},        {"LFO", B_LFO, ROWS(R_LFO)},    {"EFFECTS", B_FX, ROWS(R_FX)},
    {"ARP", B_ARP, ROWS(R_ARP)},        {"VOICE", B_GLO, ROWS(R_VOICE)},
};
#define NPAGES ((uint32_t)(sizeof PAGES / sizeof PAGES[0]))

static const char *const LAYOUT[LAY_COUNT] = {"SPLIT", "STEREO", "DOUBLE"};

/* a value as the screen shows it */
static void value_text(uint32_t id, int32_t v, char *t)
{
    static const char *const OSC1[] = {"SIN", "SAW", "PULSE", "TRI"}, *const OSC2[] = {"SAW", "PULSE", "TRI", "NOISE"};
    static const char *const SUB[] = {"SIN", "TRI", "SAW", "SQR"}, *const FILT[] = {"LP12", "LP24", "HP12", "BP12", "LPDL"};
    static const char *const DEST[] = {"OSC2", "OSC12", "FILTER", "AMP", "PW", "FM", "PAN"};
    static const char *const WAVE[] = {"SAW", "TRI", "SQR", "S+H", "RANDOM", "SIN"};
    static const char *const MODE[] = {"POLY", "MONO", "LEGATO"}, *const ARP[] = {"UPDOWN", "UP", "DOWN", "RANDOM"};
    static const char *const FX[] = {"AD1", "AD2", "DD", "DECI", "RING", "COMP", "PH1", "PH2", "PH3", "PH4"};
    static const char *const DLY[] = {"ST", "X", "PP"}, *const CHO[] = {"X1", "X2", "X2", "X4"};
    static const char *const MDEST[] = {"OSC2", "FM", "PW"};
    const char *s = 0;
    t[0] = 0;
    switch (id) {
    case X_DISPLAY: s = v == DISPLAY_LCD ? "LCD" : v == DISPLAY_GREEN ? "GREEN" : "NORD"; break;
    case X_CATEGORY: s = CATEGORY[clamp(v, CAT_BASS, CAT_OTHER)]; break;
    case X_DRONE: s = v ? "REPLACE" : "STACK"; break;
    case X_BI: s = v ? "BITIMB" : "STANDARD"; break;
    case X_LAYOUT: s = LAYOUT[clamp(v, 0, LAY_COUNT - 1)]; break;
    case S1P_OSC1_SHAPE: s = OSC1[v & 3]; break;
    case S1P_OSC2_SHAPE: s = OSC2[clamp(v - 1, 0, 3)]; break;
    case S1P_SUB_SHAPE: s = SUB[v & 3]; break;
    case S1P_SUB_OCTAVE: s = v ? "-1" : "0"; break;
    case S1P_FILTER_TYPE: s = FILT[clamp(v, 0, 4)]; break;
    case S1P_LFO1_DEST: case S1P_LFO2_DEST: s = DEST[clamp(v - 1, 0, 6)]; break;
    case S1P_LFO1_TYPE: case S1P_LFO2_TYPE: s = WAVE[clamp(v, 0, 5)]; break;
    case S1P_PLAY_MODE: s = MODE[clamp(v, 0, 2)]; break;
    case S1P_ARP_TYPE: s = ARP[clamp(v - 1, 0, 3)]; break;
    case S1P_EFFECT_TYPE: s = FX[clamp(v, 0, 9)]; break;
    case S1P_DELAY_TYPE: s = DLY[clamp(v, 0, 2)]; break;
    case S1P_CHORUS_TYPE: s = CHO[clamp(v - 1, 0, 3)]; break;
    case S1P_MOD_ENV_DEST: s = MDEST[clamp(v, 0, 2)]; break;
    default:
        if (id < S1_NPARAMS && s1_param_min[id] == 0 && s1_param_max[id] == 1)
            s = v ? "ON" : "OFF";
        break;
    }
    if (s)
        text_cat(t, s);
    else
        int_text(v, t);
}

static const char *cell_name(uint32_t id)
{
    return id == X_TEMPO ? "TEMPO" : id == X_VOICES ? "VOICES" : id == X_DRONE ? "DRONE" :
        id == X_CATEGORY ? "CAT" : id == X_DISPLAY ? "STYLE" : id == X_BI ? "MODE" : id == X_LAYOUT ? "OUTPUT" : id == X_CHL ? "L CH" : id == X_CHR ? "R CH" : id < S1_NPARAMS ? PNAME[id] : "";
}
static int32_t cell_value(uint32_t id)
{
    if (id == X_DISPLAY) return set.display;
    if (id == X_BI) return (int32_t)BI_ON;
    if (id == X_LAYOUT) return (int32_t)BI_LAYOUT;
    if (id == X_CHL) return (int32_t)CH_L;
    if (id == X_CHR) return (int32_t)CH_R;
    if (id == X_CPU) return (int32_t)shown_cpu;
    if (id == X_PEAK) return (int32_t)cpu_high;
    if (id == X_SHED) return (int32_t)cpu_sheds;
    if (id == X_RAW) return ui.ux_last_id < S1_NPARAMS ? cur.values[ui.ux_last_id] : 0;
    return id == X_TEMPO ? (int32_t)set.tempo : id == X_VOICES ? (int32_t)set.voices :
        id == X_DRONE ? set.drone_replace : id == X_CATEGORY ? cur_category : cur.values[id];
}

#include "ui/values.h"

/* --------------------------------------------------------------------- UI --- */
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    const uint8_t priority = a && (ux_equal(a, "CPU FULL") || ux_equal(a, "SAVE")) ? 2 :
        a && (a[0] == 'L' || a[0] == 'R') && a[1] == ' ' ? 1 :   /* a Circuit knob: its part and label */
        a && (ux_equal(a, "SAVED") || ux_equal(a, "RECEIVED") || ux_equal(a, "REVERT") ||
              ux_equal(a, "ALL NOTES") || ux_equal(a, "BANK") || ux_equal(a, "PART L") || ux_equal(a, "PART R") ||
              ux_equal(a, "BITIMBRAL") || ux_equal(a, "STANDARD") || ux_equal(a, "OUTPUT")) ? 1 : 0;
    /* Speak every event, but don't let a routine parameter update erase a warning. */
    if (ann_on()) {
        t[0] = 0;
        if (a) text_cat(t, a);
        if (b && b[0]) { text_cat(t, " "); text_cat(t, b); }
        ann_say(t, 0, ANN_NOW | (priority == 2 ? ANN_KEEP : 0));
    }
    if (plat_ms() < ui.msg_until && priority < ui.msg_priority) return;
    ui.msg_priority = priority;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw |= UX_BAR;
}

static void say_patch(void)
{
    char t[8];
    index_text(cur_index, t);
    app_say(t, cur.name);
}

static const char *ux_row_title(void);
static const char *ux_play_state(void);

static void say_page(void)
{
    char text[40], n[8];
    text[0] = 0; text_cat(text, ux_row_title()); text_cat(text," ");
    int_text((int32_t)ui.row+1,n);text_cat(text,n);text_cat(text,"/");
    int_text(PAGES[ui.page].nrows,n);text_cat(text,n);
    app_say("PAGE",text);
}

static void speak(void)                  /* explicit WHERE: identity, subpage, values and playing state */
{
    char t[240], n[12], value[32];
    if (ann_on() && ann_where()) {
        uint32_t c;
        const char *unit;
        index_text(cur_index, n);
        t[0] = 0;
        text_cat(t, APP_NAME ". PATCH ");
        text_cat(t, n);
        text_cat(t, " ");
        text_cat(t, cur.name);
        if (factory500_has(cur_index)) {
            const uint32_t source = FACTORY500_BANK[cur_index - FACTORY500_BASE];
            ux_append(t,sizeof t,". BY ");ux_append(t,sizeof t,FACTORY500_SOURCE[source].author);
        }
        text_cat(t, ". PAGE ");
        text_cat(t, PAGES[ui.page].title);
        ux_append(t,sizeof t,". ");ux_append(t,sizeof t,ux_row_title());
        ux_append(t,sizeof t," ");int_text((int32_t)ui.row+1,n);ux_append(t,sizeof t,n);
        for(c=0;c<4;c++) {
            const uint32_t id=PAGES[ui.page].rows[ui.row][c];
            if(id==X_NONE) continue;
            ux_value(id,cell_value(id),value,&unit);
            ux_append(t,sizeof t,". ");ux_append(t,sizeof t,ux_label(id));ux_append(t,sizeof t," ");
            ux_append(t,sizeof t,value);ux_append(t,sizeof t," ");ux_append(t,sizeof t,unit);
        }
        ux_append(t,sizeof t,". ");ux_append(t,sizeof t,ux_play_state());
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* ---------------------------------------------------------- bitimbral --- */
static void mode_push(void)              /* the render takes the mode at an event: every note stops there */
{
    w_bi = (uint8_t)BI_ON;
    w_layout = (uint8_t)BI_LAYOUT;
    w_ch[0] = (uint8_t)(CH_L - 1u);
    w_ch[1] = (uint8_t)(CH_R - 1u);
    ev_push(EV_MODE, 0, 0);
    set_dirty = 1;
    ui.redraw = UX_ALL;
}

static void focus_set(uint32_t part)     /* the panel and the keys on part L (0) or R (1) */
{
    if (!BI_ON)
        return;
    if (part != cur_part) {
        part_swap();
        set.bi = (uint8_t)((set.bi & ~8u) | (cur_part ? 8u : 0u));
        set_dirty = 1;
    }
    app_say(cur_part ? "PART R" : "PART L", cur.name);
}

static void mode_set(int on)             /* standard (one sound, as ever) or bitimbral (L and R) */
{
    if ((int)BI_ON == (on != 0))
        return;
    if (!on && cur_part)
        part_swap();                     /* standard plays part L */
    set.bi = (uint8_t)((set.bi & ~9u) | (on ? 1u : 0u));
    mode_push();
    app_say(on ? "BITIMBRAL" : "STANDARD", on ? LAYOUT[BI_LAYOUT] : "ONE SOUND");
}

static void layout_set(int32_t v)
{
    v = clamp(v, 0, LAY_COUNT - 1);
    if ((uint32_t)v == BI_LAYOUT)
        return;
    set.bi = (uint8_t)((set.bi & ~6u) | (uint32_t)v << 1);
    mode_push();
    app_say("OUTPUT", LAYOUT[v]);
}

static void cell_edit(uint32_t id, int32_t d)
{
    char t[32];
    const char *unit;
    int32_t v;
    if (id == X_NONE || (id >= X_CPU && id <= X_RAW) || d == 0)
        return;
    if (id == X_TEMPO) {
        v = clamp((int32_t)set.tempo + d, 30, 300);
        if (v == (int32_t)set.tempo)
            return;
        set.tempo = (uint16_t)v;
        set_dirty = 1;
        ext_tempo = 0;
        ev_push(EV_TEMPO, 0, v);
    } else if (id == X_VOICES) {
        v = clamp((int32_t)set.voices + (d > 0 ? 1 : -1), 1, S1_MAX_VOICES);
        if (v == (int32_t)set.voices)
            return;
        set.voices = (uint8_t)v;
        set_dirty = 1;
        ev_push(EV_VOICES, 0, v);
    } else if (id == X_DRONE) {
        v = clamp(set.drone_replace + d, 0, 1);
        set.drone_replace = (uint8_t)v;
        set_dirty = 1;
        ev_push(EV_DRONE_MODE, 0, v);
    } else if (id == X_DISPLAY) {
        v = clamp(set.display + d, DISPLAY_NORD, DISPLAY_COUNT - 1);
        if (v == set.display) return;
        set.display = (uint8_t)v;
        set_dirty = 1;
    } else if (id == X_BI) {
        mode_set(d > 0);
        return;
    } else if (id == X_LAYOUT) {
        layout_set((int32_t)BI_LAYOUT + (d > 0 ? 1 : -1));
        return;
    } else if (id == X_CHL || id == X_CHR) {
        uint8_t *const ch = id == X_CHL ? &set.ch_l : &set.ch_r;
        v = clamp((int32_t)(id == X_CHL ? CH_L : CH_R) + (d > 0 ? 1 : -1), 1, 16);
        if (v == (int32_t)(id == X_CHL ? CH_L : CH_R))
            return;
        *ch = (uint8_t)v;
        mode_push();                     /* (the notes stop: none is left hanging on the old channel) */
    } else if (id == X_CATEGORY) {
        v = clamp(cur_category + d, CAT_BASS, CAT_OTHER);
        if (v == cur_category)
            return;
        cur_category = (uint8_t)v;
        refresh_dirty();
        set.category = CAT_ALL;
        set_dirty = 1;
    } else {
        v = clamp(cur.values[id] + d, s1_param_min[id], s1_param_max[id]);
        if (v == cur.values[id])
            return;
        cur.values[id] = v;
        refresh_dirty();
        ev_push(EV_PARAM, id, v);
    }
    ux_value(id, v, t, &unit);
    if (unit[0]) { text_cat(t, " "); text_cat(t, unit); }
    if (ann_on()) app_say(ux_label(id), t);
    ui.redraw = UX_ALL;
}

static void save_patch(void)
{
    uint8_t rec[BANK_REC];
    int slot = user_index(cur_index) ? (int)(cur_index - NFACT) : bank_free_slot(); /* built-ins save a copy */
    if (slot < 0) {
        app_say("SAVE", "BANK FULL");
        return;
    }
    bank_pack(&cur, rec);
    rec[2] = cur_category;
    rec[3] = CAT_MARK;
    ev_push(EV_PANIC, 0, 0);             /* the flash write stops the sound a moment */
    if (bank_write((uint32_t)slot, rec)) {
        app_say("SAVE", "FAILED");
        return;
    }
    cur_index = NFACT + (uint32_t)slot;
    if (!category_matches(cur_index)) set.category = CAT_USER;
    patch_reference();
    *(cur_part ? &set.patch_b : &set.patch) = (uint16_t)cur_index;
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    set_dirty = 0;
    app_say("SAVED", cur.name);
}

static void octave_step(int32_t d)
{
    char t[4];
    const int32_t o = clamp(set.octave + d, -3, 3);
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    set_dirty = 1;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    if ((btn & (1u << B_ARP)) && !(ui.btn & (1u << B_ARP))) {
        ui.arp_down = 1;
        ui.arp_ms = plat_ms();
    }
    if (ui.arp_down == 1 && plat_ms() - ui.arp_ms >= 500u) {
        ui.arp_down = 2;
        if (ui.page == 6u)
            ui.row = (ui.row + 1u) % PAGES[6].nrows;
        else {
            ui.page = 6;
            ui.row = ui.ux_rows[6];
        }
        if (ann_on()) say_page();
        ui.redraw = UX_ALL;
    }
    if (!(btn & (1u << B_ARP)) && ui.arp_down) {
        if (ui.arp_down == 1) {
            cell_edit(S1P_ARP_ON, cur.values[S1P_ARP_ON] ? -1 : 1);
            app_say("ARP", cur.values[S1P_ARP_ON] ? "ON" : "OFF");
        }
        ui.arp_down = 0;
    }
    {   /* OCT- and OCT+ together: standard <-> bitimbral. One alone: the octave; in bitimbral a tap puts the panel on
         * part L (OCT-) or R (OCT+), and holding it half a second moves the octave */
        const uint32_t both = 1u << B_OCTDN | 1u << B_OCTUP, oct = btn & both, was = ui.btn & both;
        if (oct && !was) {
            ui.oct_ms = plat_ms();
            ui.oct_used = 0;
            ui.oct_step = 0;
            if (!BI_ON) {
                const int8_t before = set.octave;
                octave_step(oct & (1u << B_OCTDN) ? -1 : 1);
                ui.oct_step = (int8_t)(set.octave - before);
            }
        }
        if (oct == both && was != both && ui.oct_used != 2u) {
            if (ui.oct_step) {           /* the first of the two had moved the octave */
                set.octave = (int8_t)(set.octave - ui.oct_step);
                ui.oct_step = 0;
            }
            ui.oct_used = 2;
            mode_set(!BI_ON);
        }
        if (BI_ON && oct && oct != both && !ui.oct_used && plat_ms() - ui.oct_ms >= 500u) {
            ui.oct_used = 1;
            octave_step(oct & (1u << B_OCTDN) ? -1 : 1);
        }
        if (!oct && was && was != both && !ui.oct_used && BI_ON)
            focus_set(was & (1u << B_OCTUP) ? 1u : 0u);
    }
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        const uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            const uint32_t note = (uint32_t)clamp(53 + (int32_t)k + 12 * set.octave, 0, 127), v = touch_vel(k);
            const uint32_t parts = BI_ON && BI_LAYOUT == LAY_DOUBLE ? 3u : 1u << cur_part;
            ui.held_note[k] = (uint8_t)note;
            ui.held_part[k] = (uint8_t)parts;
            if (parts & 1u) ev_push_to(0, EV_NOTE_ON, note, (int32_t)(v ? v : 1u));
            if (parts & 2u) ev_push_to(1, EV_NOTE_ON, note, (int32_t)(v ? v : 1u));
            plat_midi_send(0x09u | 0x90u << 8 | note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.held_part[k] & 1u) ev_push_to(0, EV_NOTE_OFF, ui.held_note[k], 0);
            if (ui.held_part[k] & 2u) ev_push_to(1, EV_NOTE_OFF, ui.held_note[k], 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.held_note[k] << 16, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {
        const uint32_t bit = 1u << k;
        uint32_t pg;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_ARP || k == B_OCTDN || k == B_OCTUP)
            continue;
        if (k == B_SEQ) {
            const int on = !PART[cur_part]->hold;
            ev_push(EV_HOLD, 0, on);
            app_say(cur.values[S1P_ARP_ON] ? "HOLD" : "DRONE", on ? "ON" : "OFF");
            continue;
        }
        for (pg = 0; pg < NPAGES && PAGES[pg].button != k; pg++) {}
        if (pg < NPAGES) {                   /* a page; again: its next row */
            if (ui.page == pg)
                ui.row = (ui.row + 1u) % PAGES[pg].nrows;
            else {
                ui.page = pg;
                ui.row = ui.ux_rows[pg];
            }
            if (ann_on())
                say_page();
            ui.redraw = UX_ALL;
        } else if (k == B_SAVE)
            save_patch();
        else if (k == B_PLAY) {              /* silence */
            ev_push(EV_PANIC, 0, 0);
            app_say("ALL NOTES", "OFF");
        } else if (k == B_REC) {             /* the patch as it was saved */
            patch_select(cur_index);
            app_say("REVERT", cur.name);
        } else
            app_say("BUTTON", "NOT USED");
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        patch_step(e);
        say_patch();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        category_step(e);
        app_say(CATEGORY[set.category], cur.name);
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        const uint32_t previous = ui.row;
        ui.row = (uint32_t)clamp((int32_t)ui.row + e, 0, (int32_t)PAGES[ui.page].nrows - 1);
        if (ui.row != previous && ann_on()) say_page();
        ui.redraw = UX_ALL;
    }
    /* K1..K4: the row's four values; a fast turn goes further. */
    if (ui.ux_page != ui.page || ui.ux_row != ui.row) {
        ui.ux_focus = -1;
        ui.ux_page = (uint8_t)ui.page;
        ui.ux_row = (uint8_t)ui.row;
    }
    ui.ux_rows[ui.page] = (uint8_t)ui.row;
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0) {
            if (PAGES[ui.page].rows[ui.row][k] != X_NONE) {
                ui.ux_focus = (int8_t)k;
                ui.ux_last_id = PAGES[ui.page].rows[ui.row][k] < S1_NPARAMS ?
                    PAGES[ui.page].rows[ui.row][k] : ui.ux_last_id;
                ui.redraw |= UX_GRAPH | UX_CELLS; /* also at a parameter's limit */
            }
            cell_edit(PAGES[ui.page].rows[ui.row][k], e * (e < 0 ? -e : e));
        }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = UX_ALL;
    ui.keys = keys;
    ui.btn = btn;
    lit = (btn | 1u << PAGES[ui.page].button) & ~((1u << B_ARP) | (1u << B_SEQ));
    if (cur.values[S1P_ARP_ON])
        lit |= 1u << B_ARP;
    if (PART[cur_part]->hold)
        lit |= 1u << B_SEQ;
    {
        const uint8_t status = (uint8_t)((PART[cur_part]->hold ? 2u : 0u) | (cur.values[S1P_ARP_ON] ? 1u : 0u));
        if (ui.status != status) {
            ui.status = status;
            ui.redraw = UX_ALL;
        }
    }
    if (cur_dirty)
        lit |= 1u << B_SAVE;
    if (BI_ON)                           /* bitimbral: the part the panel is on. OCT- and green PLAY: L; OCT+ and red PLAY: R */
        lit |= 1u << (cur_part ? B_OCTUP : B_OCTDN) | 1u << B_PLAY;
    plat_play_red(BI_ON && cur_part);
    plat_glow(1);
    plat_leds(lit, keys);
}

void app_input_only(void) { input(); }

static uint32_t peak_cpu, peak_voices, sheds;   /* since the last readout (app_log) */

/* the development readout (tools/fm1_remote.py log): the render's load now and at its highest since the last
 * readout, with the voices that were sounding, and how often an overload dropped a voice */
void app_log(void (*line)(const char *t))
{
    char t[41], n[12];
    t[0] = 0;
    text_cat(t, "sy1 cpu ");
    int_text((int32_t)plat_cpu_pct(), n);
    text_cat(t, n);
    text_cat(t, " peak ");
    int_text((int32_t)peak_cpu, n);
    text_cat(t, n);
    text_cat(t, " voices ");
    int_text((int32_t)peak_voices, n);
    text_cat(t, n);
    text_cat(t, "/");
    int_text(set.voices, n);
    text_cat(t, n);
    text_cat(t, " shed ");
    int_text((int32_t)sheds, n);
    text_cat(t, n);
    line(t);
    t[0] = 0;                            /* the second core and part R */
    text_cat(t, "sy1 c2 ");
    int_text(plat_cpu2_on(), n); text_cat(t, n);
    text_cat(t, " gu ");
    int_text(plat_cpu2_gave_up(), n); text_cat(t, n);
    text_cat(t, " bad ");
    int_text((int32_t)plat_cpu2_bad(), n); text_cat(t, n);
    text_cat(t, " stk ");
    int_text((int32_t)plat_cpu2_stack(), n); text_cat(t, n);
    text_cat(t, " vb ");
    int_text(s1_active_voices(&synth_b), n); text_cat(t, n);
    line(t);
    t[0] = 0;
    text_cat(t, "sy1 jobs ");
    int_text((int32_t)dual_jobs, n); text_cat(t, n);
    text_cat(t, " self ");
    int_text((int32_t)dual_self, n); text_cat(t, n);
    text_cat(t, " bi ");
    int_text((int32_t)set.bi, n); text_cat(t, n);
    line(t);
    dual_jobs = dual_self = 0;
    peak_cpu = peak_voices = sheds = 0;
}

#include "ui/nord.h"

/* -------------------------------------------------------------------- SysEx --- */
static void sysex_reply(uint32_t cmd, const uint8_t *d, uint32_t n)   /* d: n data bytes, 7 bits each */
{
    uint8_t f[16];
    uint32_t k = 0, i;
    f[k++] = 0xF0; f[k++] = 0x7D; f[k++] = 0x53; f[k++] = 0x59; f[k++] = 0x31;
    f[k++] = (uint8_t)cmd;
    for (i = 0; i < n && i < 8u; i++)
        f[k++] = d[i] & 0x7Fu;
    f[k++] = 0xF7;
    plat_sysex_send(f, k);
}

int app_sysex(const uint8_t *p, uint32_t n)   /* main loop: the bytes between F0 and F7 (bank.h) */
{
    uint8_t d[5];
    if (n < 5u || p[0] != 0x7Du || p[1] != 0x53u || p[2] != 0x59u || p[3] != 0x31u)
        return 0;
    if (p[4] == 0x01u && n >= 7u) {          /* write a slot */
        const uint32_t slot = p[5] | (uint32_t)p[6] << 7;
        uint8_t rec[BANK_REC];
        uint32_t i, got = 0, status = 1;
        for (i = 0; i < BANK_REC; i++)
            rec[i] = 0xFF;
        for (i = 7u; i + 8u <= n && got < BANK_PAYLOAD; i += 8u) {   /* 8 bytes carry 7 */
            uint32_t j;
            for (j = 0; j < 7u && got < BANK_PAYLOAD; j++)
                rec[got++] = (uint8_t)(p[i + 1u + j] | ((p[i] >> j & 1u) << 7));
        }
        if (got == BANK_PAYLOAD && slot < BANK_SLOTS && rec[0] == BANK_USED && rec[1] == 1u) {
            ev_push(EV_PANIC, 0, 0);
            status = bank_write(slot, rec) ? 2u : 0u;
        }
        d[0] = p[5];
        d[1] = p[6];
        d[2] = (uint8_t)status;
        sysex_reply(0x41, d, 3);
        if (!status) {
            char t[8];
            num3(slot + 1u, t);
            app_say("RECEIVED", t);
            if (other.index == NFACT + slot && !other.dirty)
                other_select(other.index);
            if (cur_index == NFACT + slot) {
                if (!cur_dirty) patch_select(cur_index);
                else {  /* keep the player's edits, compare them to what is now actually stored */
                    s1_patch stored;
                    uint32_t j;
                    bank_load(slot, &stored);
                    for (j = 0; j < S1_NPARAMS; j++) saved_values[j] = stored.values[j];
                    for (j = 0; j < S1_NAME_LEN; j++) saved_name[j] = stored.name[j];
                    saved_category = patch_category(cur_index);
                    ui.ref_serial++;
                    refresh_dirty();
                }
            }
        }
    } else if (p[4] == 0x02u) {              /* erase all */
        ev_push(EV_PANIC, 0, 0);
        d[0] = bank_erase_all() ? 2u : 0u;
        sysex_reply(0x42, d, 1);
        if (user_index(other.index))
            other_select(0);
        if (user_index(cur_index))
            patch_select(0);
        app_say("BANK", "ERASED");
    } else if (p[4] == 0x04u && n >= 7u) {   /* a slot's name */
        const uint32_t slot = p[5] | (uint32_t)p[6] << 7;
        const uint8_t *r = bank_rec(slot);
        uint8_t f[40];
        uint32_t k = 0, i;
        f[k++] = 0xF0; f[k++] = 0x7D; f[k++] = 0x53; f[k++] = 0x59; f[k++] = 0x31; f[k++] = 0x44;
        f[k++] = p[5] & 0x7Fu;
        f[k++] = p[6] & 0x7Fu;
        f[k++] = (uint8_t)bank_used(slot);
        for (i = 0; bank_used(slot) && i < 23u && r[4u + i]; i++)
            f[k++] = r[4u + i] >= 32u && r[4u + i] < 127u ? r[4u + i] : '?';
        f[k++] = 0xF7;
        plat_sysex_send(f, k);
        return 1;                            /* (nothing changed: no redraw) */
    } else if (p[4] == 0x05u && n >= 7u) {   /* empty a slot */
        const uint32_t slot = p[5] | (uint32_t)p[6] << 7;
        uint32_t status = 1;
        if (slot < BANK_SLOTS) {
            ev_push(EV_PANIC, 0, 0);
            status = bank_write(slot, 0) ? 2u : 0u;
        }
        d[0] = p[5];
        d[1] = p[6];
        d[2] = (uint8_t)status;
        sysex_reply(0x45, d, 3);
        if (!status && other.index == NFACT + slot)
            other_select(0);
        if (!status && cur_index == NFACT + slot)
            patch_select(0);                 /* the patch playing is gone */
    } else if (p[4] == 0x08u && n >= 7u) {   /* the mode (0 standard, 1 bitimbral) and the output (LAY_*); another
                                              * value leaves it as it is, so 7F 7F only asks */
        if (p[5] <= 1u)
            mode_set((int)p[5]);
        if (p[6] < LAY_COUNT)
            layout_set((int32_t)p[6]);
        d[0] = (uint8_t)BI_ON;
        d[1] = (uint8_t)BI_LAYOUT;
        d[2] = cur_part;
        sysex_reply(0x48, d, 3);
        return 1;
    } else if (p[4] == 0x06u && n >= 8u) {   /* complete credits/readmes, read-only and byte-preserving */
        const uint32_t offset = p[5] | (uint32_t)p[6] << 7 | (uint32_t)p[7] << 14;
        uint8_t frame[204];
        uint32_t at = 0, i;
        frame[at++]=0xF0;frame[at++]=0x7D;frame[at++]=0x53;frame[at++]=0x59;frame[at++]=0x31;frame[at++]=0x46;
        frame[at++]=p[5]&127u;frame[at++]=p[6]&127u;frame[at++]=p[7]&127u;
        for(i=0;i<96u && offset+i<sizeof FACTORY500_CREDITS;i++) {
            const uint8_t byte=FACTORY500_CREDITS[offset+i];
            frame[at++]=byte>>4;frame[at++]=byte&15u;
        }
        frame[at++]=0xF7;plat_sysex_send(frame,at);return 1;
    } else if (p[4] == 0x07u && n >= 8u) {   /* a slot's whole record, in two parts: read only */
        const uint32_t slot = p[5] | (uint32_t)p[6] << 7;
        const uint32_t from = p[7] ? BANK_PART : 0u, to = p[7] ? BANK_PAYLOAD : BANK_PART;
        const uint8_t *r = bank_rec(slot);
        const int used = bank_used(slot);
        uint8_t f[10u + (BANK_PART + 6u) / 7u * 8u + 1u];
        uint32_t k = 0, i, j;
        f[k++] = 0xF0; f[k++] = 0x7D; f[k++] = 0x53; f[k++] = 0x59; f[k++] = 0x31; f[k++] = 0x47;
        f[k++] = p[5] & 0x7Fu;
        f[k++] = p[6] & 0x7Fu;
        f[k++] = p[7] ? 1u : 0u;
        f[k++] = (uint8_t)used;
        for (i = from; used && i < to; i += 7u) {   /* 7 bytes as 8, as they came in; the last group padded */
            uint8_t top = 0;
            for (j = 0; j < 7u && i + j < to; j++)
                top |= (uint8_t)((r[i + j] >> 7) << j);
            f[k++] = top;
            for (j = 0; j < 7u; j++)
                f[k++] = i + j < to ? r[i + j] & 0x7Fu : 0u;
        }
        f[k++] = 0xF7;
        plat_sysex_send(f, k);
        return 1;                            /* (nothing changed: no redraw) */
    } else if (p[4] == 0x03u) {              /* user slots only; built-ins consume none of these */
        d[0] = (uint8_t)(bank_count & 0x7Fu);
        d[1] = (uint8_t)(bank_count >> 7);
        d[2] = (uint8_t)(BANK_SLOTS & 0x7Fu);
        d[3] = (uint8_t)(BANK_SLOTS >> 7);
        d[4] = 1u;                           /* what this SY1 takes besides: bit 0, command 07 (a record read) */
        sysex_reply(0x43, d, 5);
    } else
        return 0;
    ui.redraw = UX_ALL;
    return 1;
}

/* ------------------------------------------------------------------- start --- */
void app_init(void)
{
    s1_config cfg;
    int loaded;
    ux_live = (ux_live_t){0};
    ux_audio = (ux_live_t){0};
    ux_audio_frames = ux_track_age = 0;
    ux_render_generation = ux_wanted_generation = 0;
    ux_track = -1;
    set = (settings_t){0};
    loaded = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if ((loaded != (int)sizeof set && loaded != (int)SET_V3_BYTES && loaded != (int)SET_V2_BYTES && loaded != SET_LEGACY_BYTES) || set.magic != SET_MAGIC ||
        set.octave < -3 || set.octave > 3 || set.voices < 1u || set.voices > S1_MAX_VOICES || set.tempo < 30u ||
        set.tempo > 300u)
        settings_defaults();
    if (set.category >= CAT_COUNT)
        set.category = CAT_ALL;
    if (set.drone_replace > 1)
        set.drone_replace = 0;
    if (set.display >= DISPLAY_COUNT)
        set.display = DISPLAY_NORD;
    if (loaded == SET_LEGACY_BYTES || BI_LAYOUT >= LAY_COUNT || (set.bi & 0xF0u)) {
        set.bi = 0;
        set.patch_b = 0;
    }
    if (!BI_ON)
        set.bi &= (uint8_t)~8u;
    if (set.ch_l > 16u || set.ch_r > 16u)
        set.ch_l = set.ch_r = 0;
    if (set.category_b >= CAT_COUNT)
        set.category_b = CAT_ALL;
    if (loaded != (int)sizeof set || set.octave_b < -3 || set.octave_b > 3 || set.voices_b < 1u ||
        set.voices_b > S1_MAX_VOICES || set.drone_b > 1u) {   /* settings from before the parts had their own */
        set.octave_b = set.octave;
        set.voices_b = set.voices;
        set.drone_b = set.drone_replace;
    }
    set.spare[0] = set.spare[1] = 0;
    cfg.sample_rate = SR;
    cfg.max_voices = set.voices;
    cfg.delay_buf = delay_buf;
    cfg.delay_len = DELAY_LEN;
    cfg.chorus_buf = chorus_buf;
    cfg.chorus_len = CHORUS_LEN;
    s1_init(&synth, &cfg);
    cfg.delay_buf = delay_buf_b;
    cfg.delay_len = DELAY_LEN_B;
    cfg.chorus_buf = chorus_buf_b;
    s1_init(&synth_b, &cfg);
    (void)plat_cpu2_set(1);              /* -1: one core, part B renders on the first */
    {                                    /* as saved, the plain fields are the part's the panel was on */
        const uint32_t on_r = set.bi & 8u;
        synth.cfg.max_voices = on_r ? set.voices_b : set.voices;
        synth_b.cfg.max_voices = on_r ? set.voices : set.voices_b;
        synth.drone_replace = on_r ? set.drone_b : set.drone_replace;
        synth_b.drone_replace = on_r ? set.drone_replace : set.drone_b;
    }
    w_bi = a_bi = (uint8_t)BI_ON;
    w_layout = a_layout = (uint8_t)BI_LAYOUT;
    w_ch[0] = (uint8_t)(CH_L - 1u);
    w_ch[1] = (uint8_t)(CH_R - 1u);
    macro_seen = 0;
    w_focus = cur_part = 0;
    ux_refocus = 0;
    clk_n = clk_t0 = clk_last = ext_tempo = 0;
    clk_live = 0;
    want_program[0] = want_program[1] = -1;
    ev_overflow = 0;
    s1_set_tempo(&synth, (float)set.tempo);
    s1_set_tempo(&synth_b, (float)set.tempo);
    bank_scan();
    {
        /* as saved, category is the part's the panel was on and category_b the other's */
        const uint32_t on_r = set.bi & 8u;
        const uint8_t cat_l = on_r ? set.category_b : set.category, cat_r = on_r ? set.category : set.category_b;
        uint32_t i;
        cur_part = 1;                    /* part R first, then L */
        set.category = cat_r;
        patch_select(set.patch_b);
        set.category_b = cat_l;
        if (!on_r) {                     /* (the swaps below exchange the parts' settings: R's in the plain fields first) */
            const uint8_t v = set.voices, dr = set.drone_replace;
            const int8_t o = set.octave;
            set.voices = set.voices_b; set.voices_b = v;
            set.drone_replace = set.drone_b; set.drone_b = dr;
            set.octave = set.octave_b; set.octave_b = o;
        }
        ui.page = ui.row = other.page = other.row = 0;
        for (i = 0; i < 8u; i++)
            ui.ux_rows[i] = other.ux_rows[i] = 0;
        part_swap();                     /* R's patch and settings into the other part's place: the panel on L */
        patch_select(set.patch);
        if (on_r)
            part_swap();
    }
    set_dirty = 0;
}

void app_ui_init(void)
{
    ui.ux_focus = -1;
    ui.ux_last_id = X_NONE;
    ui.ux_page = (uint8_t)ui.page;
    ui.ux_row = (uint8_t)ui.row;
    ux_cache_valid = 0;
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = UX_ALL;
}

void app_frame(void)
{
    static uint32_t last_save, last_stat, midi_seen, midi_at, displayed_tempo, displayed_clock;
    const uint32_t now = plat_ms();
    input();
    speak();
    const int was_moving = ux_motion();
    if (ux_read_live()) {
        if (was_moving || ux_motion()) ui.redraw |= UX_GRAPH;
        if (shown_voices != ux_live.voices) {
            shown_voices = ux_live.voices;
            ui.redraw |= UX_FOOT;
        }
    }
    if (midi_notes != midi_seen) { midi_seen = midi_notes; midi_at = now; }
    if (shown_midi != (uint32_t)(midi_at && now - midi_at < 150u)) {
        shown_midi = midi_at && now - midi_at < 150u;
        ui.redraw |= UX_FOOT;
    }
    const uint32_t tempo = ext_tempo ? ext_tempo : set.tempo;
    if (tempo != displayed_tempo || (ext_tempo != 0) != displayed_clock) {
        displayed_tempo = tempo;
        displayed_clock = ext_tempo != 0;
        ui.redraw |= UX_GRAPH | UX_CELLS | UX_FOOT;
    }
    {                                        /* a Circuit knob moved: its part and label, what it does, the value */
        static uint32_t macro_shown;
        const uint32_t seen = macro_seen;
        if (seen != macro_shown) {
            const uint32_t p = seen >> 3 & 1u, m = seen & 7u, id = MACRO[m][0].id;
            const int32_t *const base = p == cur_part ? cur.values : other.cur.values;
            char a[24], b[32];
            const char *unit;
            macro_shown = seen;
            a[0] = 0; text_cat(a, p ? "R " : "L "); text_cat(a, MACRO_NAME[m]);
            b[0] = 0; text_cat(b, ux_label(id)); text_cat(b, " ");
            {
                char *end = b;
                while (*end) end++;
                ux_value(id, macro_param(base, m, 0, (float)mac[p].target[m]), end, &unit);
            }
            if (unit[0]) { text_cat(b, " "); text_cat(b, unit); }
            app_say(a, b);
        }
    }
    {                                        /* a program change: the list's patch of that number, if there is one */
        uint32_t p;
        for (p = 0; p < 2u; p++)
            if (want_program[p] >= 0) {
                const uint32_t i = (uint32_t)want_program[p];
                want_program[p] = -1;
                if (i >= list_count() || !list_has(i))
                    continue;
                if (p == cur_part) {
                    patch_select(i);
                    say_patch();
                } else
                    other_select(i);
            }
    }
    if (shed) {                              /* the render ran out of time: one voice fewer */
        shed = 0;
        sheds++;
        cpu_sheds++;
        ui.redraw |= UX_FOOT | UX_GRAPH | UX_CELLS;
        /* the part with the most voices sounding gives one up */
        const uint32_t p = BI_ON && s1_active_voices(PART[cur_part ^ 1u]) > s1_active_voices(PART[cur_part]) ?
            cur_part ^ 1u : cur_part;
        uint8_t *const limit = p == cur_part ? &set.voices : &set.voices_b;
        if (*limit > 2u) {
            (*limit)--;
            set_dirty = 1;
            ev_push_to(p, EV_VOICES, 0, *limit);
            app_say("CPU FULL", "FEWER VOICES");
        }
    }
    if (now - last_stat >= 250u) {           /* the footer's numbers, four times a second */
        const uint32_t v = (uint32_t)(s1_active_voices(&synth) + s1_active_voices(&synth_b)), cpu = plat_cpu_pct();
        last_stat = now;
        if (cpu > peak_cpu)
            peak_cpu = cpu;
        if (cpu > cpu_high) cpu_high = cpu;
        if (v > peak_voices)
            peak_voices = v;
        if (v != shown_voices || cpu != shown_cpu) {
            shown_voices = v;
            shown_cpu = cpu;
            ui.redraw = UX_ALL;
        }
    }
    if (set_dirty && !ui.keys && !s1_active_voices(&synth) && !s1_active_voices(&synth_b) && now - last_save >= 5000u) {
        last_save = now;
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = UX_ALL;
    }
    if (ui.redraw) {
        const uint32_t areas = ui.redraw;
        ui.redraw = 0;
        ux_draw(areas);
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "patch"))
        *got = (int)cur_index;
    else if (!strcmp(what, "count"))
        *got = (int)bank_count;
    else if (!strcmp(what, "factory")) *got = FACTORY500_COUNT;
    else if (!strcmp(what, "voices"))
        *got = s1_active_voices(&synth);
    else if (!strcmp(what, "voicesb")) *got = s1_active_voices(&synth_b);
    else if (!strcmp(what, "mode")) *got = (int)BI_ON;
    else if (!strcmp(what, "layout")) *got = (int)BI_LAYOUT;
    else if (!strcmp(what, "part")) *got = cur_part;
    else if (!strcmp(what, "patchb")) *got = (int)(cur_part ? set.patch : set.patch_b);
    else if (!strcmp(what, "arpstep")) *got = PART[cur_part]->arp_step;
    else if (!strcmp(what, "clkpos")) *got = (int)PART[cur_part]->clk_pos;
    else if (!strcmp(what, "arpext")) *got = PART[cur_part]->arp_ext;
    else if (!strcmp(what, "chl")) *got = (int)CH_L;
    else if (!strcmp(what, "chr")) *got = (int)CH_R;
    else if (!strcmp(what, "maxvb")) *got = set.voices_b;
    else if (!strcmp(what, "maxv"))
        *got = set.voices;
    else if (!strcmp(what, "page"))
        *got = (int)ui.page;
    else if (!strcmp(what, "row"))
        *got = (int)ui.row;
    else if (!strcmp(what, "ux_focus"))
        *got = ui.ux_focus;
    else if (!strcmp(what, "display")) *got = set.display;
    else if (!strcmp(what, "ux_note")) *got = ux_live.active ? ux_live.note : -1;
    else if (!strcmp(what, "ux_stage")) *got = ux_live.stage[0];
    else if (!strcmp(what, "ux_live")) *got = (int)ux_live.sequence;
    else if (!strcmp(what, "dirty"))
        *got = cur_dirty;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "tempo"))
        *got = (int)(ext_tempo ? ext_tempo : set.tempo);
    else if (!strcmp(what, "hold"))
        *got = PART[cur_part]->hold;
    else if (!strcmp(what, "category"))
        *got = set.category;
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9')   /* pN: the playing patch's parameter N */
        *got = (int)cur.values[atoi(what + 1) % S1_NPARAMS];
    else if (what[0] == 'e' && what[1] >= '0' && what[1] <= '9')   /* eN: the same as the engine has it */
        *got = (int)s1_get_param(PART[cur_part], atoi(what + 1));
    else
        return 0;
    return 1;
}
#endif
