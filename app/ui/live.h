/* Audio -> UI: a small seqlock snapshot, published at <= 15 Hz. The UI never walks live voices.
 * Single audio core. Compiler barriers order the RAM accesses; readers retry only twice. */
#pragma once
typedef struct {
    uint32_t sequence;
    uint32_t generation;
    uint16_t env[3];
    uint8_t stage[3], phase[2];
    int16_t lfo[2], note, arp_note;
    uint8_t active, voices, arp_step, arp_total;
} ux_live_t;
static volatile ux_live_t ux_audio;
static ux_live_t ux_live;
static int16_t ux_lfo_history[2][32];
static uint8_t ux_lfo_position;
static uint32_t ux_audio_frames, ux_track_age;
static int32_t ux_track = -1;
static volatile uint32_t ux_wanted_generation;
static uint32_t ux_render_generation;

static void ux_publish(uint32_t frames)
{
    uint32_t i;
    int32_t newest = -1;
    ux_audio_frames += frames;
    if (ux_audio_frames < 2940u) return;    /* 44100 / 15; no floating-point timing */
    ux_audio_frames %= 2940u;
    for (i = 0; i < (uint32_t)synth.cfg.max_voices; i++)
        if (synth.voice[i].active && (newest < 0 || (int32_t)(synth.voice[i].age - synth.voice[newest].age) > 0))
            newest = (int32_t)i;
    if (newest >= 0 && (ux_track < 0 || (int32_t)(synth.voice[newest].age - ux_track_age) > 0)) {
        ux_track = newest;
        ux_track_age = synth.voice[newest].age;
    }
    ux_audio.sequence++;
    __asm__ volatile("" ::: "memory");
    ux_audio.generation = ux_render_generation;
    ux_audio.active = ux_track >= 0 && synth.voice[ux_track].active;
    ux_audio.note = ux_audio.active ? synth.voice[ux_track].note : -1;
    for (i = 0; i < 3; i++) {
        const s1_env *env = ux_audio.active ? (i == 0 ? &synth.voice[ux_track].aenv :
            i == 1 ? &synth.voice[ux_track].fenv : &synth.voice[ux_track].menv) : 0;
        ux_audio.env[i] = env ? (uint16_t)(s1_clampf(env->level, 0.0f, 1.0f) * 1024.0f) : 0;
        ux_audio.stage[i] = env ? env->stage : 0;
    }
    for (i = 0; i < 2; i++) {
        float phase = synth.lfo[i].phase - synth.ck.lfo_inc[i]; /* value belongs to the pre-increment phase */
        if (phase < 0.0f) phase += 1.0f;
        ux_audio.phase[i] = (uint8_t)(s1_clampf(phase, 0.0f, 0.999f) * 255.0f);
        ux_audio.lfo[i] = (int16_t)(s1_clampf(synth.lfo[i].value, -1.0f, 1.0f) * 1024.0f);
    }
    ux_audio.voices = (uint8_t)s1_active_voices(&synth);
    ux_audio.arp_note = (int16_t)synth.arp_playing;
    ux_audio.arp_step = (uint8_t)(synth.arp_step < 0 ? 0 : synth.arp_step);
    ux_audio.arp_total = (uint8_t)(synth.arp_n * (synth.val[S1P_ARP_OCT_RANGE] + 1));
    __asm__ volatile("" ::: "memory");
    ux_audio.sequence++;
}

static int ux_read_live(void)
{
    uint32_t attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        const uint32_t before = ux_audio.sequence;
        ux_live_t next;
        if ((before & 1u) || before == ux_live.sequence) return 0;
        __asm__ volatile("" ::: "memory");
        next = ux_audio;
        __asm__ volatile("" ::: "memory");
        if (before == ux_audio.sequence && next.generation == ux_wanted_generation) {
            ux_live = next;
            ux_lfo_history[0][ux_lfo_position] = next.lfo[0];
            ux_lfo_history[1][ux_lfo_position] = next.lfo[1];
            ux_lfo_position = (uint8_t)((ux_lfo_position + 1u) & 31u);
            return 1;
        }
    }
    return 0;
}
