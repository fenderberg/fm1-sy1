/* SY1: the app. The engine (dsp/) plays in the audio interrupt; the main loop reads the panel, draws the screen,
 * keeps the patches and hands the engine its changes through a small queue. Part of the unity build
 * (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "s1_synth.h"
#include "s1_patch.h"
#include "bank.h"

#define SR 44100.0f
#define MAX_VOICES_DEFAULT 8             /* until measured on the FM-1; GLOBAL > VOICES changes it, an overload lowers it */
#define OUT_GAIN 2.0f                    /* one voice peaks near 0.2 of full scale, as Synth1's does */
#define DELAY_LEN 32768u                 /* 0.74 s a side, 16-bit: 128 KiB of the pool (longer echoes play an octave shorter) */
#define CHORUS_LEN 4096u

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x53593131u            /* "SY11" */
typedef struct {
    uint32_t magic;
    uint16_t patch;                      /* the list's index: the built-in ones, then the user's slots */
    int8_t octave;                       /* -3 .. +3 */
    uint8_t voices;                      /* 1 .. S1_MAX_VOICES */
    uint16_t tempo;                      /* 30 .. 300 */
    uint8_t rsv[2];
} settings_t;
static settings_t set;

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.patch = 0;
    set.octave = 0;
    set.voices = MAX_VOICES_DEFAULT;
    set.tempo = 120;
}

/* ----------------------------------------------------------------- engine --- */
static s1_synth synth;
static int16_t delay_buf[2u * DELAY_LEN] PLAT_POOL;
static float chorus_buf[2u * CHORUS_LEN] PLAT_POOL;
static float buf_l[APP_BLOCK], buf_r[APP_BLOCK];
static float gain_now;

/* main loop -> audio interrupt: a ring of small events, and a whole patch at a time */
enum { EV_NOTE_ON, EV_NOTE_OFF, EV_PARAM, EV_PANIC, EV_TEMPO, EV_VOICES };
typedef struct { uint8_t type, a; int16_t b; } ev_t;
static ev_t evq[64];
static volatile uint32_t ev_w, ev_r;
static int32_t patch_next[S1_NPARAMS];
static volatile uint8_t patch_ready;
static volatile int32_t want_program = -1;   /* a program change from MIDI, for the main loop */
static volatile uint32_t ext_tempo;          /* MIDI clock's tempo, 0 none yet */
static volatile uint8_t shed;                /* the render ran out of time: the main loop lowers the voices */
static volatile uint32_t midi_notes;         /* notes in from MIDI (the screen's activity mark) */

static void ev_push(uint32_t type, uint32_t a, int32_t b)
{
    const uint32_t w = ev_w;
    if (w - ev_r >= 64u)
        return;
    evq[w & 63u].type = (uint8_t)type;
    evq[w & 63u].a = (uint8_t)a;
    evq[w & 63u].b = (int16_t)b;
    ev_w = w + 1u;
}

static void events(void)                 /* audio interrupt */
{
    if (patch_ready) {
        s1_set_patch(&synth, patch_next);
        patch_ready = 0;
    }
    while (ev_r != ev_w) {
        const ev_t e = evq[ev_r & 63u];
        ev_r++;
        switch (e.type) {
        case EV_NOTE_ON: s1_note_on(&synth, e.a, e.b); break;
        case EV_NOTE_OFF: s1_note_off(&synth, e.a); break;
        case EV_PARAM: s1_set_param(&synth, e.a, e.b); break;
        case EV_PANIC: s1_all_notes_off(&synth, 1); break;
        case EV_TEMPO: s1_set_tempo(&synth, (float)e.b); break;
        default:
            s1_all_notes_off(&synth, 1);
            synth.cfg.max_voices = clamp(e.b, 1, S1_MAX_VOICES);
            break;
        }
    }
}

static void midi_in(void)                /* audio interrupt: USB, the TRS jack, Bluetooth */
{
    static uint32_t clk_n, clk_t0;
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        const uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2) {
                s1_note_on(&synth, (int)d1, (int)d2);
                midi_notes++;
                break;
            }
            /* fall through */
        case 0x80u: s1_note_off(&synth, (int)d1); break;
        case 0xB0u: s1_control_change(&synth, (int)d1, (int)d2); break;
        case 0xC0u: want_program = (int32_t)d1; break;
        case 0xD0u: s1_pressure(&synth, (int)d1); break;
        case 0xE0u: s1_pitch_bend(&synth, (int)(d1 | d2 << 7)); break;
        default:
            if (st == 0xF8u) {           /* MIDI clock: the tempo over each quarter note (24 ticks) */
                if (clk_n == 0u)
                    clk_t0 = synth.now;
                else if (clk_n == 24u) {
                    const uint32_t span = synth.now - clk_t0;
                    if (span > 4000u && span < 100000u) {
                        const uint32_t bpm = (uint32_t)(60.0f * SR / (float)span + 0.5f);
                        if (bpm != ext_tempo) {
                            ext_tempo = bpm;
                            s1_set_tempo(&synth, (float)bpm);
                        }
                    }
                    clk_t0 = synth.now;
                    clk_n = 0u;
                }
                clk_n++;
            } else if (st == 0xFAu || st == 0xFCu)
                clk_n = 0u;
            break;
        }
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
    for (pos = 0; pos < n; pos += APP_BLOCK) {
        const uint32_t m = n - pos < APP_BLOCK ? n - pos : APP_BLOCK;
        const float step = (gain - gain_now) / (float)m;   /* MASTER glides over the block: no click */
        s1_render(&synth, buf_l, buf_r, (int)m);
        for (i = 0; i < m; i++) {
            gain_now += step;
            lr[2u * (pos + i)] = (int32_t)(soft(buf_l[i] * OUT_GAIN) * gain_now);
            lr[2u * (pos + i) + 1u] = (int32_t)(soft(buf_r[i] * OUT_GAIN) * gain_now);
        }
        gain_now = gain;
    }
}

void app_panic(void)
{
    s1_all_notes_off(&synth, 1);
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
    uint8_t redraw;
} ui;

static s1_patch cur;                     /* the patch playing, with the player's edits */
static uint32_t cur_index;
static uint8_t cur_dirty, set_dirty;

static uint32_t list_count(void) { return NFACT + bank_count; }
static int list_has(uint32_t i) { return i < NFACT || bank_used(i - NFACT); }

static void to_engine(void)
{
    uint32_t i;
    patch_ready = 0;                     /* the interrupt leaves a half-written patch alone */
    for (i = 0; i < S1_NPARAMS; i++)
        patch_next[i] = cur.values[i];
    patch_ready = 1;
}

static void patch_select(uint32_t i)
{
    if (i >= list_count() || !list_has(i))
        i = 0;
    if (i < NFACT)
        fact_load(i, &cur);
    else
        bank_load(i - NFACT, &cur);
    cur_index = i;
    cur_dirty = 0;
    if (set.patch != i) {
        set.patch = (uint16_t)i;
        set_dirty = 1;
    }
    to_engine();
    ui.redraw = 1;
}

static void patch_step(int32_t d)        /* to the next patch that exists, d of them on, round the ends */
{
    const uint32_t n = list_count();
    uint32_t i = cur_index;
    int32_t left = d < 0 ? -d : d;
    while (left > 0) {
        do
            i = d > 0 ? (i + 1u) % n : (i + n - 1u) % n;
        while (!list_has(i));
        left--;
    }
    patch_select(i);
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
    if (i < NFACT) {
        b[0] = 'P';
        b[1] = (char)('0' + (i + 1u) / 10u % 10u);
        b[2] = (char)('0' + (i + 1u) % 10u);
        b[3] = 0;
    } else
        num3(i - NFACT + 1u, b);
}

/* ------------------------------------------------------------------ pages --- */
enum { X_TEMPO = 100, X_VOICES, X_NONE = 255 };   /* a cell that is not one of the patch's parameters */

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
    {S1P_ARP_GATE, X_TEMPO, X_NONE, X_NONE}};
static const uint8_t R_VOICE[][4] = {
    {S1P_PLAY_MODE, S1P_PORTA_TIME, S1P_PORTA_AUTO, S1P_BEND_RANGE},
    {S1P_UNISON_ON, S1P_UNISON_VOICES, S1P_UNISON_DETUNE, S1P_UNISON_PAN},
    {S1P_UNISON_PITCH, S1P_UNISON_PHASE, S1P_POLYPHONY, S1P_PAN},
    {X_VOICES, X_TEMPO, X_NONE, X_NONE}};
#define ROWS(r) (uint8_t)(sizeof r / sizeof r[0]), r
static const page_t PAGES[] = {
    {"PLAY", B_HOME, ROWS(R_HOME)},     {"OSC", B_EDIT, ROWS(R_OSC)},   {"FILTER", B_SEL, ROWS(R_FILTER)},
    {"ENV", B_ENV, ROWS(R_ENV)},        {"LFO", B_LFO, ROWS(R_LFO)},    {"EFFECTS", B_FX, ROWS(R_FX)},
    {"ARP", B_ARP, ROWS(R_ARP)},        {"VOICE", B_GLO, ROWS(R_VOICE)},
};
#define NPAGES ((uint32_t)(sizeof PAGES / sizeof PAGES[0]))

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
    return id == X_TEMPO ? "TEMPO" : id == X_VOICES ? "VOICES" : id < S1_NPARAMS ? PNAME[id] : "";
}
static int32_t cell_value(uint32_t id)
{
    return id == X_TEMPO ? (int32_t)set.tempo : id == X_VOICES ? (int32_t)set.voices : cur.values[id];
}

/* --------------------------------------------------------------------- UI --- */
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_patch(void)
{
    char t[8];
    index_text(cur_index, t);
    app_say(t, cur.name);
}

static void speak(void)                  /* a screen reader that has just started listening hears the screen */
{
    char t[96], n[8];
    if (ann_on() && ann_where()) {
        index_text(cur_index, n);
        t[0] = 0;
        text_cat(t, APP_NAME ". PATCH ");
        text_cat(t, n);
        text_cat(t, " ");
        text_cat(t, cur.name);
        text_cat(t, ". PAGE ");
        text_cat(t, PAGES[ui.page].title);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void cell_edit(uint32_t id, int32_t d)
{
    char t[12];
    int32_t v;
    if (id == X_NONE || d == 0)
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
    } else {
        v = clamp(cur.values[id] + d, s1_param_min[id], s1_param_max[id]);
        if (v == cur.values[id])
            return;
        cur.values[id] = v;
        cur_dirty = 1;
        ev_push(EV_PARAM, id, v);
    }
    value_text(id, v, t);
    if (ann_on())
        app_say(cell_name(id), t);
    ui.redraw = 1;
}

static void save_patch(void)
{
    uint8_t rec[BANK_REC];
    int slot = cur_index >= NFACT ? (int)(cur_index - NFACT) : bank_free_slot();   /* a built-in one: a new slot */
    if (slot < 0) {
        app_say("SAVE", "BANK FULL");
        return;
    }
    bank_pack(&cur, rec);
    ev_push(EV_PANIC, 0, 0);             /* the flash write stops the sound a moment */
    if (bank_write((uint32_t)slot, rec)) {
        app_say("SAVE", "FAILED");
        return;
    }
    cur_index = NFACT + (uint32_t)slot;
    cur_dirty = 0;
    set.patch = (uint16_t)cur_index;
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
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        const uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            const uint32_t note = (uint32_t)clamp(53 + (int32_t)k + 12 * set.octave, 0, 127), v = touch_vel(k);
            ui.held_note[k] = (uint8_t)note;
            ev_push(EV_NOTE_ON, note, (int32_t)(v ? v : 1u));
            plat_midi_send(0x09u | 0x90u << 8 | note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ev_push(EV_NOTE_OFF, ui.held_note[k], 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.held_note[k] << 16, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {
        const uint32_t bit = 1u << k;
        uint32_t pg;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        for (pg = 0; pg < NPAGES && PAGES[pg].button != k; pg++) {}
        if (pg < NPAGES) {                   /* a page; again: its next row */
            if (ui.page == pg)
                ui.row = (ui.row + 1u) % PAGES[pg].nrows;
            else {
                ui.page = pg;
                ui.row = 0;
            }
            if (ann_on())
                app_say("PAGE", PAGES[pg].title);
            ui.redraw = 1;
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_SAVE)
            save_patch();
        else if (k == B_PLAY) {              /* silence */
            ev_push(EV_PANIC, 0, 0);
            app_say("ALL NOTES", "OFF");
        } else if (k == B_SEQ) {             /* the patch as it was saved */
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
        patch_step(e * 10);
        say_patch();
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        ui.row = (uint32_t)clamp((int32_t)ui.row + e, 0, (int32_t)PAGES[ui.page].nrows - 1);
        ui.redraw = 1;
    }
    for (k = 0; k < 4u; k++)                 /* K1..K4: the row's four values; a fast turn goes further */
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            cell_edit(PAGES[ui.page].rows[ui.row][k], e * (e < 0 ? -e : e));
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    lit = btn | 1u << PAGES[ui.page].button;
    if (cur_dirty)
        lit |= 1u << B_SAVE;
    plat_glow(1);
    plat_leds(lit, keys);
}

void app_input_only(void) { input(); }

static uint32_t shown_voices, shown_cpu, shown_midi;

static void draw(void)
{
    const page_t *pg = &PAGES[ui.page];
    const uint32_t now = plat_ms();
    uint32_t r, c, first;
    char t[40], n[12];
    /* the header: the patch, or a message for a moment; the page */
    cv_begin(240, 46, C_BLACK);
    if (now < ui.msg_until) {
        cv_text(8, 2, &FONT_XS, ui.msg[0], C_GRAY);
        cv_text(8, 18, &FONT_M, ui.msg[1], C_WHITE);
    } else {
        index_text(cur_index, n);
        cv_text(8, 2, &FONT_XS, n, C_HI);
        if (cur_dirty)
            cv_text(40, 2, &FONT_XS, "EDITED", C_AMB);
        cv_text(232 - text_w(&FONT_XS, pg->title), 2, &FONT_XS, pg->title, C_GRAY);
        cv_text(8, 18, &FONT_M, cur.name, C_WHITE);
    }
    cv_rect(0, 44, 240, 1, C_LINE);
    cv_blit(0, 0);
    /* the page's rows, four at a time around the chosen one: each cell a name and its value; K1..K4 turn the
     * chosen row's */
    cv_begin(240, 152, C_BLACK);
    first = ui.row >= 4u ? ui.row - 3u : 0u;
    for (r = first; r < pg->nrows && r < first + 4u; r++) {
        const int32_t y = (int32_t)(r - first) * 38;
        const int sel = r == ui.row;
        if (sel)
            cv_rect(0, y, 240, 37, C_LINE);
        for (c = 0; c < 4u; c++) {
            const uint32_t id = pg->rows[r][c];
            const int32_t x = 4 + (int32_t)c * 59;
            if (id == X_NONE)
                continue;
            cv_text(x, y + 2, &FONT_XS, cell_name(id), sel ? C_GRAY : C_DIM);
            value_text(id, cell_value(id), t);
            cv_text(x, y + 15, &FONT_XS, t, sel ? C_HI : C_GRAY);
        }
    }
    if (pg->nrows > 4u) {                    /* more rows than fit: where we are */
        const int32_t h = 152 * 4 / (int32_t)pg->nrows;
        cv_rect(237, (int32_t)first * 152 / (int32_t)pg->nrows, 3, h, C_DIM);
    }
    cv_blit(0, 46);
    /* the footer: voices sounding, the render's load, the octave, the tempo */
    cv_begin(240, 42, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    cv_text(8, 6, &FONT_XS, "VOICES", C_DIM);
    int_text((int32_t)shown_voices, t);
    text_cat(t, "/");
    int_text(set.voices, n);
    text_cat(t, n);
    cv_text(8, 20, &FONT_XS, t, C_HI);
    cv_text(68, 6, &FONT_XS, "CPU", C_DIM);
    int_text((int32_t)shown_cpu, t);
    text_cat(t, "%");
    cv_text(68, 20, &FONT_XS, t, shown_cpu > 85u ? C_WHITE : C_HI);
    cv_text(118, 6, &FONT_XS, "OCT", C_DIM);
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
    cv_text(118, 20, &FONT_XS, t, C_HI);
    cv_text(160, 6, &FONT_XS, ext_tempo ? "CLOCK" : "TEMPO", C_DIM);
    int_text((int32_t)(ext_tempo ? ext_tempo : set.tempo), t);
    cv_text(160, 20, &FONT_XS, t, C_HI);
    cv_text(208, 6, &FONT_XS, "MIDI", shown_midi ? C_HI : C_DIM);
    cv_blit(0, 198);
}

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
    uint8_t d[4];
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
            if (cur_index == NFACT + slot && !cur_dirty)
                patch_select(cur_index);     /* the patch playing was replaced */
        }
    } else if (p[4] == 0x02u) {              /* erase all */
        ev_push(EV_PANIC, 0, 0);
        d[0] = bank_erase_all() ? 2u : 0u;
        sysex_reply(0x42, d, 1);
        if (cur_index >= NFACT)
            patch_select(0);
        app_say("BANK", "ERASED");
    } else if (p[4] == 0x03u) {              /* how many: the slots in use, and all of them */
        d[0] = (uint8_t)(bank_count & 0x7Fu);
        d[1] = (uint8_t)(bank_count >> 7);
        d[2] = (uint8_t)(BANK_SLOTS & 0x7Fu);
        d[3] = (uint8_t)(BANK_SLOTS >> 7);
        sysex_reply(0x43, d, 4);
    } else
        return 0;
    ui.redraw = 1;
    return 1;
}

/* ------------------------------------------------------------------- start --- */
void app_init(void)
{
    s1_config cfg;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.octave < -3 || set.octave > 3 || set.voices < 1u || set.voices > S1_MAX_VOICES || set.tempo < 30u ||
        set.tempo > 300u)
        settings_defaults();
    cfg.sample_rate = SR;
    cfg.max_voices = set.voices;
    cfg.delay_buf = delay_buf;
    cfg.delay_len = DELAY_LEN;
    cfg.chorus_buf = chorus_buf;
    cfg.chorus_len = CHORUS_LEN;
    s1_init(&synth, &cfg);
    s1_set_tempo(&synth, (float)set.tempo);
    bank_scan();
    patch_select(set.patch);
    set_dirty = 0;
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_save, last_stat, midi_seen, midi_at;
    const uint32_t now = plat_ms();
    input();
    speak();
    if (want_program >= 0) {                 /* a program change: the list's patch of that number, if there is one */
        const uint32_t i = (uint32_t)want_program;
        want_program = -1;
        if (i < list_count() && list_has(i)) {
            patch_select(i);
            say_patch();
        }
    }
    if (shed) {                              /* the render ran out of time: one voice fewer */
        shed = 0;
        if (set.voices > 2u) {
            set.voices--;
            set_dirty = 1;
            ev_push(EV_VOICES, 0, set.voices);
            app_say("CPU FULL", "FEWER VOICES");
        }
    }
    if (now - last_stat >= 250u) {           /* the footer's numbers, four times a second */
        const uint32_t v = (uint32_t)s1_active_voices(&synth), cpu = plat_cpu_pct();
        last_stat = now;
        if (midi_notes != midi_seen) {
            midi_seen = midi_notes;
            midi_at = now;
        }
        if (v != shown_voices || cpu != shown_cpu || (midi_at && now - midi_at < 600u) != shown_midi) {
            shown_voices = v;
            shown_cpu = cpu;
            shown_midi = midi_at && now - midi_at < 600u;
            ui.redraw = 1;
        }
    }
    if (set_dirty && !ui.keys && now - last_save >= 5000u) {   /* the settings keep themselves, between notes */
        last_save = now;
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "patch"))
        *got = (int)cur_index;
    else if (!strcmp(what, "count"))
        *got = (int)bank_count;
    else if (!strcmp(what, "voices"))
        *got = s1_active_voices(&synth);
    else if (!strcmp(what, "maxv"))
        *got = set.voices;
    else if (!strcmp(what, "page"))
        *got = (int)ui.page;
    else if (!strcmp(what, "row"))
        *got = (int)ui.row;
    else if (!strcmp(what, "dirty"))
        *got = cur_dirty;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "tempo"))
        *got = (int)(ext_tempo ? ext_tempo : set.tempo);
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9')   /* pN: the playing patch's parameter N */
        *got = (int)cur.values[atoi(what + 1) % S1_NPARAMS];
    else if (what[0] == 'e' && what[1] >= '0' && what[1] <= '9')   /* eN: the same as the engine has it */
        *got = (int)s1_get_param(&synth, atoi(what + 1));
    else
        return 0;
    return 1;
}
#endif
