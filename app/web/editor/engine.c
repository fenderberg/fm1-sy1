/* One SY1 instance in the editor's audio worklet. Linear memory is fixed. */
#include "s1_synth.h"

static s1_synth synth;
static int16_t delay[2 * 131072];
static float chorus[2 * 16384];
static int32_t patch[S1_NPARAMS];
static float channels[2][128];

void editor_init(float rate)
{
    s1_config cfg = {rate, S1_MAX_VOICES, delay, 131072, chorus, 16384};
    s1_init(&synth, &cfg);
}
int32_t *editor_patch(void) { return patch; }
void editor_load(void) { s1_set_patch(&synth, patch); }
void editor_param(int id, int value) { s1_set_param(&synth, id, value); }
void editor_key(int note, int down)
{
    if (down) s1_note_on(&synth, note, 100);
    else s1_note_off(&synth, note);
}
void editor_stop(void) { s1_all_notes_off(&synth, 1); }
float *editor_output(void) { return channels[0]; }
void editor_render(int frames) { s1_render(&synth, channels[0], channels[1], frames); }
