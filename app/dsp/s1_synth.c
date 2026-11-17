/* The engine: keys, voices, oscillators, filters, envelopes, LFOs, arpeggiator.
 * Effects are in s1_fx.c. Laws marked (m) are oxo_synth2's measurements of Synth1. */
#include "s1_synth.h"
#include "s1_fx.h"
#include "s1_math.h"

#define P(name) (s->val[S1P_##name])
#define ENV_AIM (1.0f / 2048.0f)
enum { ST_IDLE, ST_ATTACK, ST_DECAY, ST_RELEASE };

static float lim_tab[258];        /* the filters' output limiter, gain per follower level (x 32) */
static float ladder_log2[128];    /* log2 of the ladder's cutoff per knob */
static uint8_t tabs_ready;

static void tables_init(void)
{
    int i;
    if (tabs_ready)
        return;
    for (i = 0; i < 258; i++)    /* (m) (1 + (e / T)^k)^(-p / k), T 1.038, p 0.856, k 11.54 */
        lim_tab[i] = i ? s1_powf(1.0f + s1_powf((float)i / 32.0f / 1.038f, 11.54f), -0.856f / 11.54f) : 1.0f;
    for (i = 0; i < 128; i++)
        ladder_log2[i] = s1_log2f(s1_t_ladder_hz[i]);
    tabs_ready = 1;
}

static uint32_t rng_next(uint32_t *st)
{
    uint32_t x = *st;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *st = x;
}
static float rng_bipolar(uint32_t *st) { return (float)(int32_t)rng_next(st) * (1.0f / 2147483648.0f); }

/* ------------------------------------------------------------------ cook --- */
static void env_cook(s1_envc *e, float sr, int a, int d, int su, int r, int amp_law)
{
    e->att = (float)S1_CHUNK / s1_maxf(1.0f, s1_t_attack_s[a] * sr);
    e->dec = 1.0f - s1_expf(-(float)S1_CHUNK / (s1_t_env_tau_s[d] * sr));
    e->rel = 1.0f - s1_expf(-(float)S1_CHUNK / (s1_t_env_tau_s[r] * sr));
    e->sus = amp_law ? s1_t_amp_sustain[su] : (float)su / 127.0f;
}

static float lfo_hz(const s1_synth *s, int speed, int sync)
{
    static const float beats[30] = {64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3.5f, 3, 2, 1.75f, 1.5f,
                                    4.0f / 3, 1, 0.875f, 0.75f, 2.0f / 3, 0.5f, 0.375f, 1.0f / 3, 0.25f,
                                    1.0f / 6, 0.125f, 1.0f / 12, 1.0f / 24};
    if (sync)   /* (m) 30 steps over the knob, a cycle in beats */
        return s->tempo / 60.0f / beats[s1_clampi(speed, 0, 127) * 30 / 128];
    return s1_exp2f((float)speed / 12.0367f) / 12.0f;   /* (m) speed 0 = a 12 s cycle */
}

static void cook(s1_synth *s)
{
    static const float det_off[S1_DETUNE_OSCS] = {-7, -5, -3, -1, 1, 3, 5, 7};
    const float sr = s->cfg.sample_rate;
    float mix2, sg, norm, r, q;
    int i;
    env_cook(&s->ck.amp, sr, P(AMP_ATTACK), P(AMP_DECAY), P(AMP_SUSTAIN), P(AMP_RELEASE), 1);
    env_cook(&s->ck.fil, sr, P(FILTER_ATTACK), P(FILTER_DECAY), P(FILTER_SUSTAIN), P(FILTER_RELEASE), 0);
    env_cook(&s->ck.mod, sr, P(MOD_ENV_ATTACK), P(MOD_ENV_DECAY), 0, 0, 0);
    s->ck.amp_gain = 0.2f * s1_t_amp_gain[P(AMP_GAIN)];   /* (m) an oscillator peaks at 0.2 */
    /* (m) velocity: -15 dB x sens / 64 at velocity 1 */
    s->ck.vel_k = -15.0f * ((float)P(AMP_VEL_SENS) / 64.0f) / 126.0f * 0.166096405f;
    mix2 = (float)P(OSC_MIX) / 127.0f;
    sg = (float)P(SUB_GAIN);
    s->ck.sub_w = sg / (sg + 32.0f);                      /* (m) the sub's auto level */
    s->ck.main_w = 1.0f - s->ck.sub_w;
    norm = 1.0f / (1.0f - mix2 * s->ck.sub_w);
    s->ck.mix_a = (1.0f - mix2) * norm;
    s->ck.mix_b = mix2 * s->ck.main_w * norm;
    s->ck.width = (float)P(OSC_PULSE_WIDTH) / 254.0f;     /* (m) duty of the low part, 127 = square */
    s->ck.fm_beta = P(OSC1_FM) > 0 ? s1_t_fm_beta[P(OSC1_FM)] : 0.0f;
    s->ck.fm_scale = P(OSC2_SHAPE) == 4 ? 0.65f / 0.65039f : 1.0f;
    s->ck.ring_gain = P(OSC1_FM) > 0 ? (P(OSC2_SHAPE) == 2 ? 2.0f : P(OSC2_SHAPE) == 4 ? 1.0f / 0.65039f : 1.0f) : 1.0f;
    s->ck.sat_a = s1_t_sat_a[P(FILTER_SAT)];
    for (i = 0; i < S1_DETUNE_OSCS; i++)                  /* (m) 8 copies at +-1, 3, 5, 7 x d cents, d up to 20 */
        s->ck.det_ratio[i] = s1_exp2f(det_off[i] * (20.0f * (float)P(OSC1_DETUNE) / 127.0f) / 1200.0f);
    s->ck.det_gain = P(OSC1_SHAPE) == 0 ? 0.288f : 0.297f;
    r = (float)P(FILTER_RES) / 127.0f;
    s->ck.svf_d = s1_maxf(1.0f - r, 0.0f);
    s->ck.svf_g = 1.0f - 0.7f * r;
    s->ck.lad_k = s1_maxf(0.0f, 0.0297f * (float)P(FILTER_RES) - 0.076f);
    s->ck.kbd = s1_t_kbd_track[P(FILTER_KBD_TRACK)] * (12.624f / 12.0f);
    s->ck.env_depth = (float)(P(FILTER_AMOUNT) - 64) * 1.984f;
    s->ck.mod_amt = (float)(P(MOD_ENV_AMOUNT) - 64) / (P(MOD_ENV_AMOUNT) > 64 ? 63.0f : 64.0f);
    s->ck.tune = (float)s1_t_fine_cents[P(OSC_FINE_TUNE)] / 100.0f;
    s->ck.base_off = (float)P(OSC_KEY_SHIFT) + s->ck.tune;
    s->ck.osc2_off = (float)s1_t_osc2_semis[P(OSC2_PITCH)] + (float)s1_t_fine_cents[P(OSC2_FINE)] / 100.0f;
    s->ck.glide_a = P(PORTA_TIME) > 0 ? s1_expf(-(float)S1_CHUNK / (s1_t_porta_tau[P(PORTA_TIME)] * sr)) : 0.0f;
    q = (float)(P(PAN) - 64) / (P(PAN) < 64 ? 64.0f : 63.0f);
    s->ck.pan_l = s1_minf(1.0f, 1.0f - q);
    s->ck.pan_r = s1_minf(1.0f, 1.0f + q);
    s->ck.lfo_inc[0] = lfo_hz(s, P(LFO1_SPEED), P(LFO1_TEMPO_SYNC)) * (float)S1_CHUNK * s->inv_sr;
    s->ck.lfo_inc[1] = lfo_hz(s, P(LFO2_SPEED), P(LFO2_TEMPO_SYNC)) * (float)S1_CHUNK * s->inv_sr;
    s1_fx_cook(s);
    s->dirty = 0;
}

/* -------------------------------------------------------------- envelopes --- */
/* one chunk on. (m) linear attack, exponential decay and release; they aim `aim` under their target and stop
 * within `stop` of it, so they land in finite time */
static float env_step(s1_env *e, const s1_envc *c, float aim, float stop)
{
    switch (e->stage) {
    case ST_ATTACK:
        e->level += c->att;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = ST_DECAY;
        }
        break;
    case ST_DECAY:
        e->level += (c->sus - aim - e->level) * c->dec;
        if (e->level <= c->sus + stop)
            e->level = c->sus;
        break;
    case ST_RELEASE:
        e->level += (-aim - e->level) * c->rel;
        if (e->level <= stop) {
            e->level = 0.0f;
            e->stage = ST_IDLE;
        }
        break;
    default:
        break;
    }
    return e->level;
}

/* ----------------------------------------------------------------- voices --- */
static uint32_t cycles_u32(float c)
{
    c -= s1_floorf(c);
    return (uint32_t)(c * 65536.0f) << 16;
}

static void start_phases(s1_synth *s, s1_voice *v, int relock)
{
    /* (m) osc phase 0 = free running: the phases carry on from the voice's last note. Above 0, osc1 starts
     * at 0 and osc2 up to half a cycle later; the unison phase shifts each voice of a stack by its own amount */
    static const float copy_deg[S1_DETUNE_OSCS] = {321, 172, 291, 202, 0, 69, 210, 126};
    int i;
    if (P(OSC_PHASE) > 0) {
        const float k = (float)(P(OSC_PHASE) - 1) / 126.0f;
        const float sh = (float)P(UNISON_PHASE) / 127.0f;
        const float u = (float)v->uni_k;
        v->ph1 = cycles_u32(sh * u * 0.381966f);
        v->ph2 = cycles_u32(0.5f * k + sh * u * 0.618034f);
        for (i = 0; i < S1_DETUNE_OSCS; i++)
            v->phc[i] = cycles_u32(copy_deg[i] / 360.0f * k + sh * u * (0.137f + 0.1f * (float)i));
        relock = 1;
    }
    if (relock)   /* (m) the sub runs locked to osc1 */
        v->phsub = P(SUB_OCTAVE) ? v->ph1 >> 1 : v->ph1;
}

static void set_note(s1_voice *v, int note, int glide)
{
    v->note = (uint8_t)note;
    if (!glide)
        v->glide = (float)note;
    v->glide_aim = 0.0135f * s1_absf(v->glide - (float)note);   /* (m) a glide aims 1.35 % past its target */
}

static void voice_start(s1_synth *s, s1_voice *v, int key, int vel, int glide, float cents, float pan,
                        float semis, int k)
{
    const int stolen = v->active;
    v->uni_semis = cents / 100.0f + semis;
    v->pan_l = s1_minf(1.0f, 1.0f - pan);
    v->pan_r = s1_minf(1.0f, 1.0f + pan);
    set_note(v, key, glide);
    v->vel = (uint8_t)vel;
    v->vel_gain = s1_exp2f(s->ck.vel_k * (float)(127 - vel));
    v->age = s->now;
    v->gate = 1;
    v->active = 1;
    v->uni_k = (uint8_t)k;
    v->aenv.stage = v->fenv.stage = v->menv.stage = ST_ATTACK;
    start_phases(s, v, !stolen);
    if (!stolen) {   /* a stolen voice still sounding keeps its filter and gain: no click */
        v->aenv.level = v->fenv.level = v->menv.level = 0.0f;
        v->low = v->band = v->fb = v->agc_env = v->lim_env = 0.0f;
        v->st[0] = v->st[1] = v->st[2] = v->st[3] = 0.0f;
        v->lad_gain = 1.0f;
        v->gain = 0.0f;
        v->fresh = 1;
    }
}

static void voice_change(s1_voice *v, int note, int vel, int retrigger, int glide)
{
    set_note(v, note, glide);
    v->gate = 1;
    if (retrigger) {
        if (vel >= 0)
            v->vel = (uint8_t)vel;
        v->aenv.stage = v->fenv.stage = v->menv.stage = ST_ATTACK;
    }
}

static void voice_release(s1_voice *v)
{
    v->gate = 0;
    if (v->aenv.stage != ST_IDLE)
        v->aenv.stage = ST_RELEASE;
    if (v->fenv.stage != ST_IDLE)
        v->fenv.stage = ST_RELEASE;
}

static int voice_limit(const s1_synth *s) { return s1_clampi(P(POLYPHONY), 1, s->cfg.max_voices); }

/* (m) round robin; at the limit a released voice goes first (the oldest), then the oldest held one */
static s1_voice *voice_alloc(s1_synth *s)
{
    const int limit = voice_limit(s);
    s1_voice *oldest = 0, *released = 0, *pick = 0;
    int k;
    for (k = 1; k <= limit && !pick; k++) {
        s1_voice *v = &s->voice[(s->last_voice + k) % limit];
        if (!v->active)
            pick = v;
        else if (v->gate) {
            if (!oldest || v->age < oldest->age)
                oldest = v;
        } else if (!released || v->age < released->age)
            released = v;
    }
    if (!pick)
        pick = released ? released : oldest;
    if (pick)
        s->last_voice = (int)(pick - s->voice);
    return pick;
}

static void held_remove(s1_synth *s, int key)
{
    int i, n = 0;
    for (i = 0; i < s->held_n; i++)
        if (s->held[i] != key)
            s->held[n++] = s->held[i];
    s->held_n = n;
}

static void key_sync(s1_synth *s)
{
    int k;
    for (k = 0; k < 2; k++)
        if (k ? P(LFO2_KEY_SYNC) : P(LFO1_KEY_SYNC)) {
            s1_lfo *l = &s->lfo[k];
            l->phase = 0.0f;
            l->prev = l->cur;
            l->cur = rng_bipolar(&l->rng);
        }
}

static void retrigger_phases(s1_synth *s)
{
    int i;
    for (i = 0; i < s->cfg.max_voices; i++)
        if (s->voice[i].active && s->voice[i].gate)
            start_phases(s, &s->voice[i], 1);
}

static void voice_note_on(s1_synth *s, int key, int vel, int sync)
{
    const int mode = P(PLAY_MODE);   /* 0 poly, 1 mono, 2 legato */
    const int porta = P(PORTA_TIME) > 0;
    int any = 0, i, glide;
    for (i = 0; i < s->cfg.max_voices; i++)
        any |= s->voice[i].active && s->voice[i].gate;
    if (sync && !any)   /* (m) key sync restarts the LFOs on the first key of a phrase */
        key_sync(s);
    held_remove(s, key);
    s->held[s->held_n < 32 ? s->held_n++ : 31] = (uint8_t)key;
    if (mode != 0) {
        if (any) {   /* (m) mono retriggers, legato glides; one voice (or one stack) keeps sounding */
            glide = porta;
            for (i = 0; i < s->cfg.max_voices; i++)
                if (s->voice[i].active)
                    voice_change(&s->voice[i], key, vel, mode == 1, glide);
            if (mode == 1)
                retrigger_phases(s);
            return;
        }
    } else {
        for (i = 0; i < s->cfg.max_voices; i++)   /* (m) a held key pressed again: a fresh voice */
            if (s->voice[i].active && s->voice[i].gate && s->voice[i].note == key)
                voice_release(&s->voice[i]);
    }
    glide = porta && !P(PORTA_AUTO);
    if (!P(UNISON_ON)) {
        s1_voice *v = mode != 0 ? &s->voice[s->last_voice = 0] : voice_alloc(s);
        if (v)
            voice_start(s, v, key, vel, glide, 0.0f, 0.0f, 0.0f, 0);
        return;
    }
    {   /* (m) unison: N voices evenly over +-D cents, spread over the pan, odd ones shifted by the pitch knob */
        const int n = s1_clampi(P(UNISON_VOICES), 2, 8);
        const float d = s1_t_unison_cents[P(UNISON_DETUNE)];
        const float spread = (float)(P(UNISON_PAN) - 64) / 64.0f;
        const int sounding = n < voice_limit(s) ? n : voice_limit(s);
        int k;
        for (k = 0; k < sounding; k++) {
            s1_voice *v = mode != 0 ? &s->voice[s->last_voice = k] : voice_alloc(s);
            float t;
            if (!v)
                break;
            i = s->last_voice % n;
            t = 2.0f * (float)i / (float)(n - 1) - 1.0f;
            voice_start(s, v, key, vel, glide, t * d, t * spread, (i & 1) ? (float)(P(UNISON_PITCH) - 24) : 0.0f, k);
        }
    }
}

static void voice_note_off(s1_synth *s, int key)
{
    int i;
    held_remove(s, key);
    if (P(PLAY_MODE) != 0) {
        if (s->held_n > 0) {   /* (m) back to the key still held: mono retriggers, legato does not */
            const int back = s->held[s->held_n - 1];
            int changed = 0;
            for (i = 0; i < s->cfg.max_voices; i++)
                if (s->voice[i].active && s->voice[i].note == key) {
                    voice_change(&s->voice[i], back, -1, P(PLAY_MODE) == 1, P(PORTA_TIME) > 0);
                    changed = 1;
                }
            if (changed && P(PLAY_MODE) == 1)
                retrigger_phases(s);
            return;
        }
        for (i = 0; i < s->cfg.max_voices; i++)
            if (s->voice[i].active && s->voice[i].gate)
                voice_release(&s->voice[i]);
        return;
    }
    for (i = 0; i < s->cfg.max_voices; i++)
        if (s->voice[i].active && s->voice[i].gate && s->voice[i].note == key)
            voice_release(&s->voice[i]);
}

/* ------------------------------------------------------------ arpeggiator --- */
static int arp_has(const s1_synth *s, int key)
{
    int i;
    for (i = 0; i < s->arp_n; i++)
        if (s->arp_keys[i] == key)
            return 1;
    return 0;
}

static void arp_note_on(s1_synth *s, int key, int vel)
{
    int i;
    if (s->arp_n == 0) {
        s->arp_step = -1;
        s->arp_count = 0;
        s->arp_dir = 1;
        key_sync(s);
    }
    if (arp_has(s, key) || s->arp_n >= 32)
        return;
    for (i = s->arp_n++; i > 0 && s->arp_keys[i - 1] > key; i--)   /* kept sorted */
        s->arp_keys[i] = s->arp_keys[i - 1];
    s->arp_keys[i] = (uint8_t)key;
    s->arp_vel = vel;
}

static void arp_note_off(s1_synth *s, int key)
{
    int i, n = 0;
    for (i = 0; i < s->arp_n; i++)
        if (s->arp_keys[i] != key)
            s->arp_keys[n++] = s->arp_keys[i];
    s->arp_n = n;
    if (n == 0 && s->arp_playing >= 0) {
        voice_note_off(s, s->arp_playing);
        s->arp_playing = -1;
    }
}

static int arp_next(s1_synth *s)
{
    const int total = s->arp_n * (P(ARP_OCT_RANGE) + 1);
    const int type = P(ARP_TYPE);   /* 1 up-down, 2 up, 3 down, 4 random */
    int key;
    if (type == 4)
        s->arp_step = (int)(rng_next(&s->rng) % (uint32_t)total);
    else if (type == 3)
        s->arp_step = s->arp_step <= 0 ? total - 1 : s->arp_step - 1;
    else if (type == 2)
        s->arp_step = (s->arp_step + 1) % total;
    else if (total == 1)
        s->arp_step = 0;
    else {
        if (s->arp_step + s->arp_dir >= total || s->arp_step + s->arp_dir < 0)
            s->arp_dir = -s->arp_dir;
        s->arp_step = s1_clampi(s->arp_step + s->arp_dir, 0, total - 1);
    }
    key = s->arp_keys[s->arp_step % s->arp_n] + 12 * (s->arp_step / s->arp_n);
    return key > 127 ? 127 : key;
}

static void arp_play_step(s1_synth *s, int step)   /* the next note, for a step of that many frames */
{
    if (s->arp_playing >= 0)
        voice_note_off(s, s->arp_playing);
    s->arp_playing = arp_next(s);
    if (P(ARP_GATE) > 0)   /* (m) gate 0 = silent */
        voice_note_on(s, s->arp_playing, s->arp_vel, 0);
    else
        s->arp_playing = -1;
    s->arp_gate = step * P(ARP_GATE) / 127;
    if (s->arp_gate < 1)
        s->arp_gate = 1;
}

void s1_clock(s1_synth *s, int what)
{
    /* the beats of arp_advance in clocks: every one is a whole number of them */
    static const uint8_t ticks[19] = {96, 84, 72, 48, 42, 36, 32, 24, 21, 18, 16, 12, 9, 8, 6, 4, 3, 2, 1};
    if (what == S1_CLOCK_OFF) {
        s->arp_ext = 0;
        s->arp_count = 0;
        return;
    }
    if (what == S1_CLOCK_START) {        /* the pattern from its top, on the bar (the first clock steps it) */
        s->clk_pos = 0;
        s->arp_step = -1;
        s->arp_dir = 1;
        return;
    }
    s->arp_ext = 1;
    if (P(ARP_ON) && s->arp_n > 0) {
        const int t = ticks[s1_clampi(P(ARP_BEAT), 0, 18)];
        if (s->clk_pos % (uint32_t)t == 0u)
            arp_play_step(s, (int)((float)t * 2.5f / s->tempo * s->cfg.sample_rate));
    }
    s->clk_pos++;
}

static void arp_advance(s1_synth *s, int elapsed)
{
    static const float beats[19] = {4.0f, 3.5f, 3.0f, 2.0f, 1.75f, 1.5f, 4.0f / 3, 1.0f, 0.875f, 0.75f,
                                    2.0f / 3, 0.5f, 0.375f, 1.0f / 3, 0.25f, 1.0f / 6, 0.125f, 1.0f / 12, 1.0f / 24};
    if (!P(ARP_ON) || (s->arp_n == 0 && s->arp_playing < 0))
        return;
    s->arp_count -= elapsed;
    s->arp_gate -= elapsed;
    if (s->arp_playing >= 0 && s->arp_gate <= 0 && P(ARP_GATE) < 127) {
        voice_note_off(s, s->arp_playing);
        s->arp_playing = -1;
    }
    if (s->arp_ext) {                    /* s1_clock steps it */
        s->arp_count = 0;
        return;
    }
    if (s->arp_count <= 0 && s->arp_n > 0) {
        int step;
        s->arp_acc += beats[s1_clampi(P(ARP_BEAT), 0, 18)] * 60.0f / s->tempo * s->cfg.sample_rate;
        step = (int)s->arp_acc;
        if (step < 1)
            step = 1;
        s->arp_acc -= (float)step;
        arp_play_step(s, step);
        s->arp_count += step;
    }
}

static void arp_switched(s1_synth *s)
{
    int i;
    if (P(ARP_ON)) {   /* the held keys become the arpeggiator's */
        for (i = 0; i < s->cfg.max_voices; i++)
            if (s->voice[i].active && s->voice[i].gate)
                voice_release(&s->voice[i]);
        s->held_n = 0;
        for (i = 0; i < 128; i++)
            if ((s->key_down[i] && s->latched[i] != 2) || s->sustained[i] || s->latched[i] == 1)
                arp_note_on(s, i, s->key_vel[i]);
        return;
    }
    if (s->arp_playing >= 0)
        voice_note_off(s, s->arp_playing);
    s->arp_playing = -1;
    for (i = 0; i < s->arp_n; i++)
        voice_note_on(s, s->arp_keys[i], s->key_vel[s->arp_keys[i]], 1);
    s->arp_n = 0;
}

/* ------------------------------------------------------- MIDI controllers --- */
/* (m) a controller adds round(v d 128 / (63 x 127)) knob units to its parameter, d = sens - 64 above 64,
 * sens - 63 below 63; only parameters with a 0..127 range are routed */
static float source_value(const s1_synth *s, int word)
{
    switch ((word >> 8) & 0xF0) {
    case 0xB0: return (float)s->cc[word & 0x7F];
    case 0xE0: return (float)s->bend / 64.0f;
    case 0xD0: return (float)s->pressure;
    default: return 0.0f;
    }
}

static void apply_routing(s1_synth *s)
{
    static const uint8_t src[2] = {S1P_CTRL_SRC1, S1P_CTRL_SRC2}, dst[2] = {S1P_CTRL_ASSIGN1, S1P_CTRL_ASSIGN2},
                         sens[2] = {S1P_WHEEL_SPEED_SENS, S1P_WHEEL_AMOUNT_SENS};
    int r;
    for (r = 0; r < 2; r++)
        if (s->routed[r] >= 0)
            s->val[s->routed[r]] = s->base[s->routed[r]];
    for (r = 0; r < 2; r++) {
        const int t = s->base[dst[r]], se = s->base[sens[r]];
        const int d = se >= 64 ? se - 64 : se <= 62 ? se - 63 : 0;
        float x;
        int off;
        s->routed[r] = -1;
        if (t < 0 || t >= S1_NPARAMS || s1_param_min[t] != 0 || s1_param_max[t] != 127)
            continue;
        x = source_value(s, s->base[src[r]]) * (float)d * 128.0f / (63.0f * 127.0f);
        off = x >= 0.0f ? (int)(x + 0.5f) : -(int)(-x + 0.5f);
        s->routed[r] = t;
        s->val[t] = s1_clampi(s->val[t] + off, 0, 127);
    }
    s->dirty = 1;
}

/* -------------------------------------------------------------------- API --- */
static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void s1_init(s1_synth *s, const s1_config *cfg)
{
    int i, d;
    tables_init();
    zero(s, sizeof *s);
    s->cfg = *cfg;
    s->cfg.max_voices = s1_clampi(cfg->max_voices, 1, S1_MAX_VOICES);
    s->inv_sr = 1.0f / cfg->sample_rate;
    s->tempo = 120.0f;
    s->volume = 1.0f;
    s->rng = 0x9E3779B9u;
    s->arp_playing = -1;
    s->last_voice = -1;
    s->routed[0] = s->routed[1] = -1;
    for (i = 0; i < S1_MAX_VOICES; i++) {   /* free-running phases: each voice its own */
        s1_voice *v = &s->voice[i];
        v->ph1 = rng_next(&s->rng);
        v->ph2 = rng_next(&s->rng);
        v->phsub = v->ph1;
        for (d = 0; d < S1_DETUNE_OSCS; d++)
            v->phc[d] = rng_next(&s->rng);
        v->noise = 0x70A6E;
        v->lad_gain = 1.0f;
    }
    for (i = 0; i < 2; i++) {
        s->lfo[i].rng = 0x2545F491u + (uint32_t)i * 7919u;
        s->lfo[i].prev = rng_bipolar(&s->lfo[i].rng);
        s->lfo[i].cur = rng_bipolar(&s->lfo[i].rng);
    }
    for (i = 0; i < S1_NPARAMS; i++)
        s->base[i] = s->val[i] = s1_param_def[i];
    s->cutoff = s->cutoff_target = (float)s->val[S1P_FILTER_FREQ];
    s1_fx_reset(s);
    apply_routing(s);
}

static int32_t clamp_param(int id, int32_t v)
{
    return v < s1_param_min[id] ? s1_param_min[id] : v > s1_param_max[id] ? s1_param_max[id] : v;
}

void s1_set_param(s1_synth *s, int id, int32_t value)
{
    int arp_was;
    if (id < 0 || id >= S1_NPARAMS)
        return;
    arp_was = P(ARP_ON) != 0;
    s->base[id] = s->val[id] = clamp_param(id, value);
    apply_routing(s);
    if (id == S1P_ARP_ON && arp_was != (P(ARP_ON) != 0))
        arp_switched(s);
}

int32_t s1_get_param(const s1_synth *s, int id) { return id >= 0 && id < S1_NPARAMS ? s->base[id] : 0; }

void s1_set_patch(s1_synth *s, const int32_t values[S1_NPARAMS])
{
    int i;
    s1_all_notes_off(s, 1);
    for (i = 0; i < S1_NPARAMS; i++)
        s->base[i] = s->val[i] = clamp_param(i, values[i]);
    s->routed[0] = s->routed[1] = -1;
    apply_routing(s);
    s->cutoff = s->cutoff_target = (float)P(FILTER_FREQ);
    s1_fx_reset(s);
}

void s1_set_tempo(s1_synth *s, float bpm)
{
    s->tempo = bpm >= 20.0f ? bpm : 120.0f;
    s->dirty = 1;
}

void s1_set_volume(s1_synth *s, float gain) { s->volume = gain; }

static void release_key(s1_synth *s, int key)
{
    const int arp_key = arp_has(s, key);
    if (arp_key)
        arp_note_off(s, key);
    if (P(ARP_ON) && arp_key)
        return;
    if (!P(ARP_ON) || key != s->arp_playing)
        voice_note_off(s, key);
}

void s1_set_hold(s1_synth *s, int on)
{
    int k;
    s->hold = (uint8_t)(on != 0);
    for (k = 0; k < 128; k++) {
        if (on) {
            if (s->key_down[k] || s->sustained[k])
                s->latched[k] = 1;
        } else if (s->latched[k] == 1) {
            s->latched[k] = 0;
            if (!s->key_down[k] && !s->sustained[k])
                release_key(s, k);
        }
    }
}

void s1_note_on(s1_synth *s, int key, int velocity)
{
    int k, physical = 0;
    if (velocity <= 0) {
        s1_note_off(s, key);
        return;
    }
    key &= 127;
    if (s->dirty)
        cook(s);
    if (s->hold) {
        /* A gesture ends when every physical key is up. HOLD and REPLACE start a new chord;
         * STACK lets each note ring until it is played again. Pedal ownership stays separate. */
        if (P(ARP_ON) || s->drone_replace) {
            for (k = 0; k < 128; k++)
                physical |= s->key_down[k];
            if (!physical)
                for (k = 0; k < 128; k++)
                    if (s->latched[k] == 1) {
                        s->latched[k] = 0;
                        if (!s->sustained[k])
                            release_key(s, k);
                    }
        } else if (s->latched[key] == 1 && !s->key_down[key]) {
            s->latched[key] = 2;
            s->key_down[key] = 1;
            s->sustained[key] = 0;
            release_key(s, key);
            return;
        }
        s->latched[key] = 1;
    }
    s->key_down[key] = 1;
    s->key_vel[key] = (uint8_t)s1_clampi(velocity, 1, 127);
    s->sustained[key] = 0;
    if (P(ARP_ON))
        arp_note_on(s, key, s->key_vel[key]);
    else
        voice_note_on(s, key, s->key_vel[key], 1);
}

void s1_note_off(s1_synth *s, int key)
{
    key &= 127;
    s->key_down[key] = 0;
    if (s->latched[key] == 2) {
        s->latched[key] = 0;
        return;
    }
    if (s->sustain) {
        s->sustained[key] = 1;
        return;
    }
    if (s->latched[key] == 1)
        return;
    release_key(s, key);
}

void s1_all_notes_off(s1_synth *s, int immediate)
{
    int i;
    for (i = 0; i < S1_MAX_VOICES; i++) {
        if (immediate) {
            s->voice[i].active = s->voice[i].gate = 0;
            s->voice[i].aenv.stage = ST_IDLE;
            s->voice[i].aenv.level = 0.0f;
        } else if (s->voice[i].active)
            voice_release(&s->voice[i]);
    }
    zero(s->sustained, sizeof s->sustained);
    zero(s->key_down, sizeof s->key_down);
    zero(s->latched, sizeof s->latched);
    s->hold = s->sustain = 0;
    s->held_n = 0;
    s->arp_n = 0;
    s->arp_playing = -1;
}

void s1_control_change(s1_synth *s, int cc, int value)
{
    int k;
    cc &= 127;
    s->cc[cc] = (uint8_t)s1_clampi(value, 0, 127);
    if (cc == 64) {
        const int down = value >= 64;
        if (s->sustain && !down)
            for (k = 0; k < 128; k++)
                if (s->sustained[k]) {
                    s->sustained[k] = 0;
                    if (!s->key_down[k] && s->latched[k] != 1)
                        release_key(s, k);
                }
        s->sustain = (uint8_t)down;
    } else if (cc == 120 || cc == 123)
        s1_all_notes_off(s, cc == 120);
    apply_routing(s);
}

void s1_pitch_bend(s1_synth *s, int value14)
{
    s->bend = value14 - 8192;
    apply_routing(s);
}

void s1_pressure(s1_synth *s, int value)
{
    s->pressure = s1_clampi(value, 0, 127);
    apply_routing(s);
}

int s1_active_voices(const s1_synth *s)
{
    int i, n = 0;
    for (i = 0; i < s->cfg.max_voices; i++)
        n += s->voice[i].active;
    return n;
}

/* ------------------------------------------------------------ oscillators --- */
#define PHF(ph) ((float)(int32_t)((ph) >> 8) * (1.0f / 16777216.0f))

/* polyBLEP: the correction for a step of +2 at phase 0 (t the phase, dt the step per sample) */
static inline float blep(float t, float dt, float inv)
{
    if (t < dt) {
        const float x = t * inv;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        const float x = (t - 1.0f) * inv;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

/* shapes as osc1 numbers them: 0 sine, 1 saw (falling, +1 at phase 0), 2 pulse (low part w), 3 triangle */
static inline float osc_read(int shape, float p, float w, float dt, float inv)
{
    float q;
    switch (shape) {
    case 0:
        return s1_sin2pi(p);
    case 1:
        return 1.0f - 2.0f * p + blep(p, dt, inv);
    case 2:
        q = p + w;
        if (q >= 1.0f)
            q -= 1.0f;
        return (p < 1.0f - w ? w : w - 1.0f) + 0.5f * (blep(p, dt, inv) - blep(q, dt, inv));
    default:
        q = p + 0.25f;
        if (q >= 1.0f)
            q -= 1.0f;
        return 1.0f - 4.0f * s1_absf(q - 0.5f);
    }
}

/* a detune copy: the plain shape from the integer phase. (m) Synth1's copies are not band-limited either */
static inline float copy_read(int shape, uint32_t ph, float w, uint32_t thr)
{
    switch (shape) {
    case 0:
        return s1_sin2pi(PHF(ph));
    case 1:
        return -(float)(int32_t)(ph + 0x80000000u) * (1.0f / 2147483648.0f);
    case 2:
        return ph < thr ? w : w - 1.0f;
    default:
        return 1.0f - s1_absf((float)(int32_t)(ph + 0xC0000000u)) * (1.0f / 1073741824.0f);
    }
}

static float note_inc(const s1_synth *s, float note)
{
    const float inc = 8.17579892f * s1_exp2f(note * (1.0f / 12.0f)) * s->inv_sr;
    return inc > 0.49f ? 0.49f : inc;
}

/* the filter's coefficient for a cutoff in knob units */
static float filter_coef(const s1_synth *s, int ladder, float c, float *comp)
{
    if (!ladder) {   /* (m) fc = 16.7 Hz x 2^(c / 12.624), c up to 125; f = 2 sin(pi fc / (2 fs)) */
        const float hz = 16.7f * s1_exp2f(s1_minf(c, 125.0f) * (1.0f / 12.624f));
        return 2.0f * s1_sin2pi(s1_minf(hz * s->inv_sr, 1.0f) * 0.25f);
    }
    {   /* (m) the ladder's own cutoff table; one-pole a = g / (1 + g) */
        const float k = s1_clampf(c, 0.0f, 127.0f);
        int i = (int)k;
        float t, hz, g;
        if (i > 126)
            i = 126;
        t = k - (float)i;
        hz = s1_exp2f(ladder_log2[i] + (ladder_log2[i + 1] - ladder_log2[i]) * t);
        g = s1_tanf(S1_PI * s1_minf(hz * s->inv_sr, 0.49f));
        *comp = s1_t_ladder_comp[i] + (s1_t_ladder_comp[i + 1] - s1_t_ladder_comp[i]) * t;
        return g / (1.0f + g);
    }
}

static inline float lim_gain(float e)
{
    const float x = (e < 8.0f ? e : 8.0f) * 32.0f;
    const int i = (int)x;
    return lim_tab[i] + (lim_tab[i + 1] - lim_tab[i]) * (x - (float)i);
}

/* ------------------------------------------------------------ voice chunk --- */
static void voice_chunk(s1_synth *s, s1_voice *v, float *L, float *R, int n)
{
    const int32_t *p = s->val;
    const int shape1 = p[S1P_OSC1_SHAPE], shape2 = p[S1P_OSC2_SHAPE];
    static const uint8_t sub_as1[4] = {0, 3, 1, 2};   /* the sub's sine, triangle, saw, square as osc1 shapes */
    const int sub_shape = sub_as1[p[S1P_SUB_SHAPE] & 3], sub_on = p[S1P_SUB_GAIN] > 0, sub_oct = p[S1P_SUB_OCTAVE] != 0;
    const int mod_on = p[S1P_MOD_ENV_ON] != 0, mod_dest = p[S1P_MOD_ENV_DEST];
    const int ring = p[S1P_OSC2_RING] != 0, sync = p[S1P_OSC2_SYNC] != 0, detune = p[S1P_OSC1_DETUNE] > 0;
    const int ftype = p[S1P_FILTER_TYPE], ladder = ftype == 1 || ftype == 4;
    const float inv_n = 1.0f / (float)n;
    const float fnote = (float)v->note;
    float fe, me, ae, base, n2, inc1, inc2, w, beta, cutoff, low, target, comp = 1.0f, fco, dfco, lad_k;
    float gain, dgain, g1, inv1, inv2, invs, incs, fmk;
    uint32_t inc1u, inc2u, incc[S1_DETUNE_OSCS], thr, ph1 = v->ph1, ph2 = v->ph2, phsub = v->phsub, noise = v->noise;
    float flow = v->low, fband = v->band, st0 = v->st[0], st1 = v->st[1], st2 = v->st[2], st3 = v->st[3];
    float fb = v->fb, agc = v->agc_env, lgain = v->lad_gain, lim = v->lim_env;
    const float svf_d = s->ck.svf_d, svf_g = s->ck.svf_g, sat = s->ck.sat_a;
    const float mix_a = s->ck.mix_a, mix_b = s->ck.mix_b, main_w = s->ck.main_w, sub_w = s->ck.sub_w;
    const float det_gain = s->ck.det_gain, ring_gain = s->ck.ring_gain;
    const float pan_l = v->pan_l, pan_r = v->pan_r;
    /* followers' rates per sample: the ladder's AGC 1.2 ms / 202 ms, the limiter 3.7 ms / 6.9 ms (m) */
    const float agc_a = s1_minf(1.0f, 833.3f * s->inv_sr), agc_r = 4.958f * s->inv_sr;
    const float lim_a = s1_minf(1.0f, 269.3f * s->inv_sr), lim_r = s1_minf(1.0f, 145.1f * s->inv_sr);
    int i, d, fm_on;

    /* (m) a glide moves every 16 samples: exponential, aimed a little past the target so it lands */
    if (p[S1P_PORTA_TIME] <= 0)
        v->glide = fnote;
    else if (v->glide != fnote) {
        const float r = v->glide - fnote, a = s->ck.glide_a;
        const float rest = s1_maxf(s1_absf(r) * a - v->glide_aim * (1.0f - a), 0.0f);
        v->glide = fnote + (r < 0.0f ? -rest : rest);
    }

    fe = env_step(&v->fenv, &s->ck.fil, ENV_AIM, 0.0f);
    me = mod_on ? env_step(&v->menv, &s->ck.mod, ENV_AIM, 0.0f) : 0.0f;
    ae = env_step(&v->aenv, &s->ck.amp, 0.0f, ENV_AIM);

    /* ---- pitch ---- */
    base = v->glide + s->ck.base_off + v->uni_semis + s->bend_semis + s->lfo_pitch_all;
    n2 = (p[S1P_OSC2_KBD_TRACK] ? base : 57.0f + s->ck.tune + s->bend_semis + s->lfo_pitch_all) + s->ck.osc2_off +
         s->lfo_pitch2;
    if (mod_on && mod_dest == 0)   /* (m) mod env on osc2's pitch: +-60 semitones */
        n2 += me * s->ck.mod_amt * 60.0f;
    inc1 = note_inc(s, base);
    inc2 = note_inc(s, n2);
    inc1u = (uint32_t)(inc1 * 4294967296.0f);
    inc2u = (uint32_t)(inc2 * 4294967296.0f);
    inv1 = 1.0f / inc1;
    inv2 = 1.0f / inc2;
    incs = sub_oct ? inc1 * 0.5f : inc1;
    invs = 1.0f / incs;
    if (detune)
        for (d = 0; d < S1_DETUNE_OSCS; d++)
            incc[d] = (uint32_t)(inc1 * s->ck.det_ratio[d] * 4294967296.0f);

    /* ---- pulse width: (m) modulation moves it by a share of the room left on that side ---- */
    w = s->ck.width;
    if (s->has_pw)
        w += s->lfo_pw * (s->lfo_pw > 0.0f ? 1.0f - w : w);
    if (mod_on && mod_dest == 2) {
        const float m = s1_clampf(me * s->ck.mod_amt, -1.0f, 1.0f);
        w += m * (m > 0.0f ? 1.0f - w : w);
    }
    w = s1_clampf(w, 1.25f / 254.0f, 1.0f - 1.375f / 254.0f);
    thr = (uint32_t)((1.0f - w) * 4294967040.0f);

    /* ---- FM: (m) a fixed phase step per sample, beta x middle C at 44.1 kHz ---- */
    beta = s->ck.fm_beta;
    if (s->has_fm || (mod_on && mod_dest == 1)) {
        float knob = (float)p[S1P_OSC1_FM];
        int j;
        if (s->has_fm)
            knob += (127.0f - knob) * s->lfo_fm;
        if (mod_on && mod_dest == 1) {   /* (m) the decay reaches FM in dB: 0 dB = 1, -80 dB = 0 */
            const float g = v->menv.stage == ST_ATTACK ? me : me > 1e-4f ? 1.0f + s1_log2f(me) * (1.0f / 13.2877f) : 0.0f;
            const float m = s1_clampf(s->ck.mod_amt * g, -1.0f, 1.0f);
            knob += m * (m > 0.0f ? 127.0f - knob : knob);
        }
        knob = s1_clampf(knob, 0.0f, 127.0f);
        j = (int)knob;
        if (j > 126)
            j = 126;
        beta = (j ? s1_t_fm_beta[j] : 0.0f) + (s1_t_fm_beta[j + 1] - (j ? s1_t_fm_beta[j] : 0.0f)) * (knob - (float)j);
    }
    fm_on = beta > 0.0f && !ring;
    fmk = beta * s->ck.fm_scale * (261.6255653f / 44100.0f) * 4294967296.0f;

    /* ---- cutoff, in knob units: (m) kbd track pivots on key 48, the env sweeps from 2.1 under the base to
     * its target, an LFO pulls toward the top ---- */
    cutoff = s->cutoff + s->ck.kbd * (fnote - 48.0f);
    low = cutoff - 2.1f;
    target = s1_clampf(cutoff + s->ck.env_depth * (p[S1P_FILTER_VEL_SWITCH] ? s1_t_filter_vel[v->vel] : 1.0f), -2.0f, 125.0f);
    cutoff = low + (target - low) * fe;
    if (s->has_cut)
        cutoff += s1_maxf(0.0f, 125.0f - cutoff) * s1_minf(1.0f, s->lfo_cut);
    target = filter_coef(s, ladder, cutoff, &comp);
    if (v->fresh) {
        v->fco = target;
        v->fresh = 0;
    }
    fco = v->fco;
    dfco = (target - fco) * inv_n;
    lad_k = s->ck.lad_k * comp;

    g1 = ae * s->ck.amp_gain * v->vel_gain * s->lfo_amp;
    gain = v->gain;
    dgain = (g1 - gain) * inv_n;

    for (i = 0; i < n; i++) {
        float o1, o2, sub = 0.0f, mixed, y;
        int32_t dev = 0;
        uint32_t nph;
        int negative, forward, backward;

        /* osc2: 1 saw, 2 pulse, 3 triangle, 4 noise */
        if (shape2 == 4) {   /* (m) a 20-bit shift register, +-0.65 */
            const uint32_t bit = (noise >> 12 ^ noise >> 17 ^ noise >> 18 ^ noise >> 19) & 1u;
            noise = (noise << 1 | bit) & 0xFFFFFu;
            o2 = bit ? 0.65039f : -0.65039f;
        } else
            o2 = osc_read(shape2, PHF(ph2), w, inc2, inv2);

        if (fm_on)
            dev = (int32_t)s1_clampf(fmk * o2, -1.0e9f, 1.0e9f);

        /* osc1, its detune copies, the sub */
        o1 = osc_read(shape1, PHF(ph1), w, inc1, inv1);
        if (sub_on)
            sub = osc_read(sub_shape, PHF(phsub), 0.5f, incs, invs);
        if (detune) {
            float acc = o1;
            for (d = 0; d < S1_DETUNE_OSCS; d++) {
                acc += copy_read(shape1, v->phc[d], w, thr);
                v->phc[d] += incc[d] + (uint32_t)dev;
            }
            o1 = acc * det_gain;
        }
        if (ring)
            o2 *= o1 * ring_gain;
        mixed = mix_a * (sub_on ? main_w * o1 + sub_w * sub : o1) + mix_b * o2 + 1e-18f;

        /* phases. (m) sync restarts osc2 whenever osc1's phase drops: a forward wrap, or FM driving it back */
        negative = dev < 0 && (uint32_t)(-dev) > inc1u;
        nph = ph1 + inc1u + (uint32_t)dev;
        forward = !negative && nph < ph1;
        backward = negative && nph > ph1;
        ph1 = nph;
        phsub += sub_oct ? (inc1u >> 1) + (uint32_t)(dev >> 1) : inc1u + (uint32_t)dev;
        if (sync && (forward || (negative && !backward))) {
            ph2 = forward ? 300648u : 0u;
            noise = 0x70A6E;
        } else
            ph2 += inc2u;

        /* filter -> limiter -> saturation -> amp (m) */
        fco += dfco;
        if (ladder) {   /* 4 one-poles, feedback through one sample, an AGC riding the feedback */
            float u = mixed - lad_k * lgain * fb, t, a;
            t = (u - st0) * fco; u = t + st0; st0 = u + t;
            t = (u - st1) * fco; u = t + st1; st1 = u + t;
            t = (u - st2) * fco; u = t + st2; st2 = u + t;
            t = (u - st3) * fco; u = t + st3; st3 = u + t;
            fb = u;
            a = s1_absf(u);
            agc += (a > agc ? agc_a : agc_r) * (a - agc);
            lgain = agc > 5.5f ? s1_powf(5.5f / agc, 0.883f) : 1.0f;
            y = u;
        } else {        /* Chamberlin state variable, run twice per sample */
            const float in = svf_g * mixed;
            float high;
            flow += fco * fband;
            high = in - flow - svf_d * fband;
            fband += fco * high;
            flow += fco * fband;
            high = in - flow - svf_d * fband;
            fband += fco * high;
            y = ftype == 0 ? flow : ftype == 2 ? high : fband;
        }
        {
            const float a = s1_absf(y);
            lim += (a > lim ? lim_a : lim_r) * (a - lim);
            if (lim > 0.39f)
                y *= lim_gain(lim);
        }
        if (sat > 0.0f)
            y = (1.0f + sat) * y / (1.0f + sat * s1_absf(y));
        gain += dgain;
        y *= gain;
        L[i] += y * pan_l;
        R[i] += y * pan_r;
    }

    v->ph1 = ph1; v->ph2 = ph2; v->phsub = phsub; v->noise = noise;
    v->low = flow; v->band = fband;
    v->st[0] = st0; v->st[1] = st1; v->st[2] = st2; v->st[3] = st3;
    v->fb = fb; v->agc_env = agc; v->lad_gain = lgain; v->lim_env = lim;
    v->fco = target;
    v->gain = g1;
    if (v->aenv.stage == ST_IDLE) {
        v->active = 0;
        v->gate = 0;
    }
}

/* ------------------------------------------------------------------- LFOs --- */
/* (m) 0 saw (falling), 1 triangle (-1 at phase 0), 2 square, 3 sample and hold, 4 smooth random, 5 sine */
static void lfo_tick(s1_lfo *l, int type, float inc)
{
    const float ph = l->phase;
    switch (type) {
    case 0: l->value = 1.0f - 2.0f * ph; break;
    case 1: l->value = ph < 0.5f ? -1.0f + 4.0f * ph : 3.0f - 4.0f * ph; break;
    case 2: l->value = ph < 0.5f ? 1.0f : -1.0f; break;
    case 3: l->value = l->cur; break;
    case 4: l->value = l->prev + (l->cur - l->prev) * ph; break;
    default: l->value = s1_sin2pi(ph); break;
    }
    l->phase += inc;
    if (l->phase >= 1.0f) {
        l->phase -= s1_floorf(l->phase);
        l->prev = l->cur;
        l->cur = rng_bipolar(&l->rng);
    }
}

static void lfos_chunk(s1_synth *s)
{
    int k;
    s->lfo_pitch_all = s->lfo_pitch2 = s->lfo_cut = s->lfo_pw = s->lfo_fm = s->lfo_pan = 0.0f;
    s->lfo_amp = 1.0f;
    s->has_cut = s->has_pw = s->has_fm = s->has_pan = 0;
    for (k = 0; k < 2; k++) {
        const int depth = k ? P(LFO2_DEPTH) : P(LFO1_DEPTH);
        const float sc = (float)depth / 127.0f;
        float v;
        lfo_tick(&s->lfo[k], k ? P(LFO2_TYPE) : P(LFO1_TYPE), s->ck.lfo_inc[k]);   /* (m) they run when off too */
        if (!(k ? P(LFO2_ON) : P(LFO1_ON)) || depth <= 0)
            continue;
        v = s->lfo[k].value;
        switch (s1_clampi(k ? P(LFO2_DEST) : P(LFO1_DEST), 1, 7)) {
        case 1: s->lfo_pitch2 += s1_t_lfo_cents[depth] * v * 0.01f; break;        /* osc2 */
        case 2: s->lfo_pitch_all += s1_t_lfo_cents[depth] * v * 0.01f; break;     /* osc1 and 2 */
        case 3: s->lfo_cut += sc * 0.5f * (v + 1.0f); s->has_cut = 1; break;      /* filter, unipolar (m) */
        case 4: s->lfo_amp *= 1.0f - sc * 0.5f * (1.0f - v); break;               /* amp */
        case 5: s->lfo_pw += sc * v; s->has_pw = 1; break;                        /* pulse width */
        case 6: s->lfo_fm += sc * 0.5f * (v + 1.0f); s->has_fm = 1; break;        /* FM */
        default: s->lfo_pan += sc * v; s->has_pan = 1; break;                     /* pan */
        }
    }
}

/* ----------------------------------------------------------------- render --- */
void s1_render(s1_synth *s, float *left, float *right, int n)
{
    int pos, m, i, any;
    if (s->dirty)
        cook(s);
    for (pos = 0; pos < n; pos += m) {
        float *L = left + pos, *R = right + pos;
        m = n - pos < S1_CHUNK ? n - pos : S1_CHUNK;
        for (i = 0; i < m; i++)
            L[i] = R[i] = 0.0f;
        arp_advance(s, m);
        lfos_chunk(s);
        /* (m) only the bend's top 7 bits count, +-63 = the full range */
        s->bend_semis = (float)s1_clampi(s->bend / 128, -63, 63) / 63.0f * (float)P(BEND_RANGE);
        /* (m) the cutoff knob glides to a new value over 50 ms */
        any = 0;
        for (i = 0; i < s->cfg.max_voices; i++)
            any |= s->voice[i].active;
        if ((float)P(FILTER_FREQ) != s->cutoff_target) {
            s->cutoff_target = (float)P(FILTER_FREQ);
            s->cutoff_rate = s1_absf(s->cutoff_target - s->cutoff) / (0.05f * s->cfg.sample_rate) * (float)S1_CHUNK;
        }
        if (!any || s1_absf(s->cutoff_target - s->cutoff) <= s->cutoff_rate)
            s->cutoff = s->cutoff_target;
        else
            s->cutoff += s->cutoff_target > s->cutoff ? s->cutoff_rate : -s->cutoff_rate;
        for (i = 0; i < s->cfg.max_voices; i++)
            if (s->voice[i].active)
                voice_chunk(s, &s->voice[i], L, R, m);
        s1_fx_process(s, L, R, m);
        s->now += (uint32_t)m;
    }
}
