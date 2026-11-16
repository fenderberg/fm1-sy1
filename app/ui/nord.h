/* Shared Nord-style UI. Five independent strips fit the existing RGB565 canvas.
 * Diagrams show settings; moving points come only from the consistent audio snapshot. */
#pragma once

#include "display.h"

/* Fit with the actual proportional font widths, reserving room for the ellipsis. */
static void ux_fit(char *out, uint32_t cap, const char *text, const felucca_font_t *font, int32_t width)
{
    uint32_t n = 0;
    while (text[n] && n + 1u < cap) {
        out[n] = text[n];
        n++;
    }
    out[n] = 0;
    if (ux_display_width(font, out) <= width)
        return;
    while (n && ux_display_width(font, out) + ux_display_width(font, "...") > width) out[--n] = 0;
    if (n + 3u < cap) text_cat(out, "...");
}

static void ux_center(int32_t x, int32_t y, int32_t width, const felucca_font_t *font,
                      const char *text, uint16_t color)
{
    char fitted[40];
    ux_fit(fitted, sizeof fitted, text, font, width);
    ux_display_text(x + (width - ux_display_width(font, fitted)) / 2, y, font, fitted, color);
}

static int32_t ux_lo(uint32_t id)
{
    return id < S1_NPARAMS ? s1_param_min[id] : id == X_TEMPO ? 30 : id == X_CATEGORY ? CAT_BASS : id == X_DRONE || id == X_DISPLAY ? 0 : 1;
}
static int32_t ux_hi(uint32_t id)
{
    return id < S1_NPARAMS ? s1_param_max[id] : id == X_TEMPO ? 300 : id == X_CATEGORY ? CAT_OTHER : id == X_DISPLAY ? DISPLAY_COUNT-1 : id == X_DRONE ? 1 : S1_MAX_VOICES;
}
static int32_t ux_norm_value(uint32_t id, int32_t value)
{
    const int32_t lo = ux_lo(id), hi = ux_hi(id);
    return clamp((value - lo) * 127 / (hi > lo ? hi - lo : 1), 0, 127);
}
static int32_t ux_norm(uint32_t id) { return ux_norm_value(id, cell_value(id)); }

static void ux_indicator(uint32_t id, int32_t x, uint16_t color)
{
    int32_t i;
    const int32_t norm = ux_norm(id), segment = (norm * 7 + 63) / 127;
    if (id >= X_CPU && id <= X_RAW) return;
    if (id < S1_NPARAMS && s1_param_max[id] - s1_param_min[id] == 1) {
        cv_rect(x, 29, 20, 5, cell_value(id) ? UX_RULE : color);
        cv_rect(x+28, 29, 20, 5, cell_value(id) ? color : UX_RULE);
    } else {
        for (i = 0; i < 8; i++) {
            const int lit = ux_centered(id) && cell_value(id)==64 ? 0 : ux_discrete(id) ? i == segment : ux_centered(id) ?
                (segment >= 4 ? i >= 4 && i <= segment : i < 4 && i >= segment) : i * 127 < norm * 8;
            cv_rect(x + i * 6, 29, 4, 5, lit ? color : UX_RULE);
        }
        if (ux_centered(id)) cv_rect(x + 23, 26, 1, 11, UX_MUTED);
    }
    if (id < S1_NPARAMS || id == X_CATEGORY) {
        const int32_t saved = ux_norm_value(id, id == X_CATEGORY ? saved_category : saved_values[id]);
        cv_rect(x + saved * 46 / 127, 35, 2, 4, UX_MUTED);
    }
}

/* A schematic setting diagram, deliberately not a measured filter response. */
static void ux_filter(void)
{
    const int32_t cutoff = 32 + cur.values[S1P_FILTER_FREQ] * 164 / 127;
    const int32_t peak = 30 - cur.values[S1P_FILTER_RES] * 20 / 127;
    const int32_t type = cur.values[S1P_FILTER_TYPE];
    const int filter_focus = ui.ux_focus >= 0 &&
        PAGES[ui.page].rows[ui.row][ui.ux_focus] != S1P_AMP_GAIN;
    const uint16_t line = filter_focus ? UX_AMBER : UX_TEXT;
    cv_line(12, 61, 227, 61, UX_RULE);
    cv_line(cutoff, 14, cutoff, 61, UX_RULE);
    if (cur.values[S1P_FILTER_FREQ] != saved_values[S1P_FILTER_FREQ]) {
        const int32_t old = 32 + saved_values[S1P_FILTER_FREQ] * 164 / 127;
        int32_t y;
        for (y = 15; y < 60; y += 5) cv_rect(old, y, 1, 2, UX_MUTED);
    }
    if (type == 2) {
        cv_line(12, 59, cutoff - 15, 35, line);
        cv_line(cutoff - 15, 35, cutoff, peak, line);
        cv_line(cutoff, peak, cutoff + 12, 30, line);
        cv_line(cutoff + 12, 30, 227, 30, line);
    } else if (type == 3) {
        cv_line(12, 59, cutoff - 16, 42, line);
        cv_line(cutoff - 16, 42, cutoff, peak, line);
        cv_line(cutoff, peak, cutoff + 16, 42, line);
        cv_line(cutoff + 16, 42, 227, 59, line);
    } else {
        cv_line(12, 30, cutoff - 17, 30, line);
        cv_line(cutoff - 17, 30, cutoff - 6, peak + 3, line);
        cv_line(cutoff - 6, peak + 3, cutoff, peak, line);
        cv_line(cutoff, peak, cutoff + 9, 39, line);
        cv_line(cutoff + 9, 39, cutoff + (type == 0 ? 28 : 16), 59, line);
    }
    cv_rect(cutoff - 2, peak - 2, 5, 5, line);
    ux_display_text(12, 64, &FONT_XS, "LOW", UX_MUTED);
    ux_display_text(227 - ux_display_width(&FONT_XS, "HIGH"), 64, &FONT_XS, "HIGH", UX_MUTED);
}

static void ux_envelope(int which)
{
    const uint32_t attack = which == 0 ? S1P_AMP_ATTACK : which == 1 ? S1P_FILTER_ATTACK : S1P_MOD_ENV_ATTACK;
    const uint32_t decay = which == 0 ? S1P_AMP_DECAY : which == 1 ? S1P_FILTER_DECAY : S1P_MOD_ENV_DECAY;
    const uint32_t sustain = which == 0 ? S1P_AMP_SUSTAIN : S1P_FILTER_SUSTAIN;
    const uint32_t release = which == 0 ? S1P_AMP_RELEASE : S1P_FILTER_RELEASE;
    const int32_t a = 17 + cur.values[attack] * 39 / 127;
    const int32_t d = a + 20 + cur.values[decay] * 32 / 127;
    const int32_t r = 227 - (which == 2 ? 19 : 19 + cur.values[release] * 44 / 127);
    const float sustain_level = which == 0 ? s1_t_amp_sustain[cur.values[sustain]] : which == 1 ?
        (float)cur.values[sustain] / 127.0f : 0.0f;
    const int32_t sy = 60 - (int32_t)(sustain_level * 48.0f);
    const int32_t xs[5] = {12, a, d, r, 227};
    const int32_t ys[5] = {60, 12, sy, sy, 60};
    const char *const labels[] = {"A", "D", "S", "R"};
    int32_t i;
    cv_line(12, 60, 227, 60, UX_RULE);
    for (i = 1; i < 4; i++) {
        int32_t y;
        for (y = ys[i] + 5; y < 60; y += 5) cv_rect(xs[i], y, 1, 2, UX_RULE);
    }
    for (i = 0; i < 4; i++) {
        const uint32_t parameter = i == 0 ? attack : i == 1 ? decay : i == 2 ? sustain : release;
        const int selected = ui.ux_focus >= 0 && PAGES[ui.page].rows[ui.row][ui.ux_focus] == parameter;
        const uint16_t color = selected ? UX_AMBER : UX_TEXT;
        cv_line(xs[i], ys[i], xs[i + 1], ys[i + 1], color);
        cv_rect(xs[i + 1] - 2, ys[i + 1] - 2, 4, 4, color);
        ux_center(i * 60, 64, 60, &FONT_XS, labels[i], selected ? UX_AMBER : UX_MUTED);
    }
    if (ux_live.active && ux_live.stage[which]) {
        const int32_t stage = ux_live.stage[which];
        const int32_t level = clamp(ux_live.env[which], 0, 1024);
        const int32_t y = 60 - level * 48 / 1024;
        int32_t x;
        if (stage == 1) x = 12 + (a - 12) * level / 1024;
        else if (stage == 2) x = level <= (int32_t)(sustain_level * 1024.0f) + 8 ? (d + r) / 2 :
            a + (d - a) * (1024 - level) / (1024 - (int32_t)(sustain_level * 1024.0f) + 1);
        else x = r + (227 - r) * (1024 - level) / 1024;
        cv_rect(clamp(x,12,227)-2, y-2, 5, 5, UX_AMBER);
    }

}

static void ux_controls(void)
{
    uint32_t c;
    cv_begin(240, 80, UX_PANEL);
    for (c = 0; c < 4; c++) {
        const uint32_t id = PAGES[ui.page].rows[ui.row][c];
        const int32_t x = (int32_t)c * 60;
        const int active = ui.ux_focus == (int8_t)c;
        const uint16_t color = active ? UX_AMBER : UX_TEXT;
        const char *unit;
        char text[40];
        if (active) {
            cv_rect(x, 0, 60, 80, UX_FOCUS);
            cv_rect(x + 2, 0, 56, 2, UX_AMBER);
        }
        if (c) cv_rect(x, 5, 1, 70, UX_RULE);
        if (id == X_NONE) { ux_center(x, 42, 60, &FONT_S, "--", UX_MUTED); continue; }
        ux_center(x + 3, 7, 54, &FONT_XS, ux_label(id), active ? UX_AMBER : UX_MUTED);
        ux_indicator(id, x + 7, color);
        ux_value(id, cell_value(id), text, &unit);
        const felucca_font_t *font = ux_display_width(&FONT_M,text) <= 52 ? &FONT_M :
            ux_display_width(&FONT_S,text) <= 52 ? &FONT_S : &FONT_XS;
        ux_center(x + 3, 40, 54, font, text, color);
        ux_center(x + 3, 66, 54, &FONT_T, id >= X_CPU && id <= X_RAW ? "READ ONLY" : unit, UX_MUTED);
    }
    cv_blit(0, 136);
}

static const char *ux_play_state(void)
{
    return (ui.status & 2u) ? (cur.values[S1P_ARP_ON] ? "ARP ON / HOLD ON" :
        (set.drone_replace ? "DRONE / REPLACE" : "DRONE / STACK")) :
        cur.values[S1P_ARP_ON] ? "ARP ON / HOLD OFF" : "ARP OFF / HOLD OFF";
}

static void ux_status(void)
{
    char text[40], n[12];
    cv_begin(240, 24, UX_BG);
    cv_rect(0, 0, 240, 1, UX_RULE);
    int_text((int32_t)shown_voices, text); text_cat(text, "/");
    int_text(set.voices, n); text_cat(text, n);
    ux_display_text(6, 1, &FONT_T, text, UX_TEXT);
    text[0] = 0; text_cat(text, "OCT ");
    text[4] = set.octave < 0 ? '-' : '+';
    text[5] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave)); text[6] = 0;
    ux_display_text(40, 1, &FONT_T, text, UX_MUTED);
    int_text((int32_t)(ext_tempo ? ext_tempo : set.tempo), text);
    text_cat(text, ext_tempo ? " EXT" : " INT");
    ux_display_text(94, 1, &FONT_T, text, UX_TEXT);
    ux_display_text(202, 1, &FONT_T, "MIDI", shown_midi ? UX_AMBER : UX_MUTED);
    cv_rect(230, 5, 4, 4, shown_midi ? UX_AMBER : UX_RULE);
    const char *state = ux_play_state();
    ux_display_text(6, 12, &FONT_T, state, (ui.status & 2u) || cur.values[S1P_ARP_ON] ? UX_AMBER : UX_MUTED);
    if (cpu_sheds) ux_display_text(199, 12, &FONT_T, "LIMIT", UX_AMBER);
    cv_blit(0, 216);
}

static const char *ux_row_title(void)
{
    static const char *const titles[8][7] = {
        {"PLAY / FILTER", "PLAY / PERFORM"},
        {"OSC / WAVES", "OSC / PITCH", "OSC / MOD", "OSC / SUB", "OSC / MOD ENV", "OSC / ENV TIME"},
        {"FILTER / MAIN", "FILTER / TRACK"},
        {"AMP ENV", "FILTER ENV", "ENV / LEVEL"},
        {"LFO 1", "LFO 1 / MOD", "LFO 2", "LFO 2 / MOD"},
        {"FX / EFFECT", "FX / MIX", "FX / EQ", "FX / DELAY", "FX / DELAY MIX", "FX / CHORUS", "FX / CHORUS MOD"},
        {"ARP", "ARP / GATE"},
        {"VOICE", "VOICE / UNISON", "VOICE / SPREAD", "VOICE / SETUP", "DIAGNOSTIC", "DISPLAY"}
    };
    return titles[ui.page][ui.row];
}

static void ux_caption(const char *left, const char *right)
{
    char fitted[40];
    ux_fit(fitted, sizeof fitted, left, &FONT_T, 148);
    ux_display_text(8, 65, &FONT_T, fitted, UX_MUTED);
    ux_fit(fitted, sizeof fitted, right, &FONT_T, 72);
    ux_display_text(232 - ux_display_width(&FONT_T,fitted), 65, &FONT_T, fitted, UX_MUTED);
}

static void ux_big(const char *title, const char *value, const char *detail)
{
    ux_center(8, 2, 224, &FONT_XS, title, UX_MUTED);
    ux_center(8, 22, 224, &FONT_M, value, UX_TEXT);
    ux_center(8, 59, 224, &FONT_T, detail, UX_MUTED);
}

static int32_t ux_wave_y(int shape, int32_t phase, int32_t amplitude, int32_t width)
{
    phase &= 127;
    if (shape == 0) return (int32_t)(s1_sin2pi((float)phase / 128.0f) * (float)amplitude);
    if (shape == 1) return amplitude - phase * amplitude / 64;
    if (shape == 2) return phase < 128 - width ? amplitude : -amplitude;
    if (shape == 3) return phase < 32 ? phase * amplitude / 32 : phase < 96 ?
        amplitude - (phase - 32) * amplitude / 32 : -amplitude + (phase - 96) * amplitude / 32;
    if (shape == 5) return phase < 64 ? -amplitude + phase * amplitude / 32 : 3 * amplitude - phase * amplitude / 32;
    return (int32_t)(((uint32_t)(phase / 5 + 1) * 1103515245u >> 16) % 31u) * amplitude / 15 - amplitude;
}

static void ux_wave_cycles(int shape, int32_t left, int32_t width, int32_t middle, int32_t amplitude,
                    int32_t duty, uint16_t color, int32_t cycles)
{
    int32_t x, old = middle - ux_wave_y(shape, 0, amplitude, duty);
    cv_line(left, middle, left + width, middle, UX_RULE);
    for (x = 4; x <= width; x += 4) {
        const int32_t y = middle - ux_wave_y(shape, x * cycles * 128 / width, amplitude, duty);
        cv_line(left + x - 4, old, left + x, y, color);
        old = y;
    }
}

static void ux_wave(int shape, int32_t left, int32_t width, int32_t middle, int32_t amplitude,
                    int32_t duty, uint16_t color)
{
    ux_wave_cycles(shape,left,width,middle,amplitude,duty,color,2);
}

static void ux_osc(void)
{
    char text[40], n[16];
    const uint16_t selected = ui.ux_focus >= 0 ? UX_AMBER : UX_TEXT;
    if (ui.row == 0) {
        ux_display_text(8, 6, &FONT_T, "OSC1", UX_MUTED);
        ux_display_text(8, 39, &FONT_T, "OSC2", UX_MUTED);
        ux_wave(cur.values[S1P_OSC1_SHAPE], 49, 180, 22, 3+(127-cur.values[S1P_OSC_MIX])*11/127,
                cur.values[S1P_OSC_PULSE_WIDTH] / 2,ui.ux_focus == 0 || ui.ux_focus >= 2 ? UX_AMBER : UX_TEXT);
        ux_wave(cur.values[S1P_OSC2_SHAPE] == 4 ? 4 : cur.values[S1P_OSC2_SHAPE], 49, 180, 53,
                3+cur.values[S1P_OSC_MIX]*11/127,cur.values[S1P_OSC_PULSE_WIDTH] / 2,
                ui.ux_focus == 1 || ui.ux_focus >= 2 ? UX_AMBER : UX_TEXT);
        ux_caption("WAVEFORMS / SETTINGS", "OSC MIX");
    } else if (ui.row == 1) {
        text[0] = 0; value_text(S1P_OSC2_SHAPE, cur.values[S1P_OSC2_SHAPE], text);
        ux_display_text(8, 3, &FONT_XS, "OSC 2", UX_MUTED);
        ux_display_text(232-ux_display_width(&FONT_XS,text),3,&FONT_XS,text,UX_TEXT);
        int_text(s1_t_osc2_semis[cur.values[S1P_OSC2_PITCH]],text); text_cat(text," st  ");
        int_text(s1_t_fine_cents[cur.values[S1P_OSC2_FINE]],n); text_cat(text,n); text_cat(text," cent");
        ux_center(8,25,224,&FONT_M,text,selected);
        ux_caption(cur.values[S1P_OSC2_KBD_TRACK] ? "KEY TRACK ON" : "KEY TRACK OFF",
                   cur.values[S1P_OSC2_SYNC] ? "SYNC ON" : "SYNC OFF");
    } else if (ui.row == 2) {
        ux_big("OSCILLATOR INTERACTION", cur.values[S1P_OSC2_RING] ? "RING" :
            cur.values[S1P_OSC1_FM] ? "FM" : "DIRECT", "OSC 1  >  OSC 2");
    } else if (ui.row == 3) {
        static const uint8_t sub_as_osc[] = {0,3,1,2};
        ux_wave(sub_as_osc[cur.values[S1P_SUB_SHAPE]],12,216,32,20,64,
                cur.values[S1P_SUB_GAIN] ? selected : UX_MUTED);
        ux_caption("SUB OSCILLATOR",cur.values[S1P_SUB_GAIN] ? "ON" : "OFF");
    } else {
        ux_envelope(2);
        if (!cur.values[S1P_MOD_ENV_ON]) ux_display_text(180,0,&FONT_T,"ENV OFF",UX_MUTED);
    }
}

static void ux_lfo(void)
{
    const uint32_t which = ui.row / 2, wave = cur.values[which ? S1P_LFO2_TYPE : S1P_LFO1_TYPE];
    const int on = cur.values[which ? S1P_LFO2_ON : S1P_LFO1_ON];
    const int sync = cur.values[which ? S1P_LFO2_TEMPO_SYNC : S1P_LFO1_TEMPO_SYNC];
    const int32_t amp = 5 + cur.values[which ? S1P_LFO2_DEPTH : S1P_LFO1_DEPTH] * 19 / 127;
    const int shape = wave == 0 ? 1 : wave == 1 ? 5 : wave == 2 ? 2 : wave == 5 ? 0 : 4;
    const int random = wave == 3 || wave == 4;
    if (random) {
        int32_t i, old_y = 33 - ux_lfo_history[which][ux_lfo_position] * amp / 1024;
        cv_line(12,33,228,33,UX_RULE);
        for (i=1;i<32;i++) {
            const int32_t y=33-ux_lfo_history[which][(ux_lfo_position+i)&31]*amp/1024;
            cv_line(12+(i-1)*216/31,old_y,12+i*216/31,y,on?UX_TEXT:UX_MUTED); old_y=y;
        }
    } else ux_wave_cycles(shape,12,216,33,amp,64,on ? UX_TEXT : UX_MUTED,1);
    if (on && ux_live.sequence) {
        const int32_t x = random ? 228 : 12 + ux_live.phase[which] * 216 / 255;
        const int32_t y = 33 - ux_live.lfo[which] * amp / 1024;
        cv_line(x,8,x,58,UX_RULE);
        cv_rect(x-2,y-2,5,5,UX_AMBER);
    }
    char dest[16]; dest[0]=0;
    value_text(which ? S1P_LFO2_DEST : S1P_LFO1_DEST,cur.values[which ? S1P_LFO2_DEST : S1P_LFO1_DEST],dest);
    ux_caption(on ? dest : "LFO OFF / SETTINGS",random ? "RECENT VALUES" : sync ? "TEMPO SYNC" : "FREE RATE");
}

static void ux_fx(void)
{
    char text[40];
    int32_t i;
    if (ui.row < 2) {
        value_text(S1P_EFFECT_TYPE,cur.values[S1P_EFFECT_TYPE],text);
        ux_big("INSERT EFFECT",text,cur.values[S1P_EFFECT_ON] ? "IN  >  EFFECT  >  OUT" : "BYPASS / SETTINGS EDITABLE");
    } else if (ui.row == 2) {
        const int32_t x=25+cur.values[S1P_EQ_FREQ]*180/127, y=32-(cur.values[S1P_EQ_LEVEL]-64)*24/64;
        cv_line(12,32,227,32,UX_RULE);
        cv_line(12,32,x-16,32,UX_TEXT); cv_line(x-16,32,x,y,UX_TEXT);
        cv_line(x,y,x+16,32,UX_TEXT);cv_line(x+16,32,227,32,UX_TEXT);
        cv_rect(x-2,y-2,5,5,UX_AMBER); ux_caption("EQ / SCHEMATIC", "CUT / BOOST");
    } else if (ui.row < 5) {
        const int on=cur.values[S1P_DELAY_ON];
        cv_line(12,56,227,56,UX_RULE);
        for(i=0;i<7;i++) {
            const int32_t h=42-i*(127-cur.values[S1P_DELAY_FEEDBACK])*5/127;
            cv_rect(16+i*30,56-h,4,h,on ? (i ? UX_MUTED : UX_AMBER) : UX_RULE);
        }
        ux_caption(on ? "DELAY / REPEATS" : "DELAY OFF / SETTINGS",cur.values[S1P_DELAY_TYPE]==2 ? "PING PONG" : "STEREO");
    } else {
        const int on=cur.values[S1P_CHORUS_ON];
        ux_wave(0,12,216,26,14,64,on ? UX_TEXT : UX_MUTED);
        ux_wave(0,12,216,43,12,64,on ? UX_AMBER : UX_MUTED);
        ux_caption(on ? "CHORUS / WIDTH" : "CHORUS OFF / SETTINGS", "MODULATION");
    }
}

static void ux_arp(void)
{
    int32_t i;
    const int on=cur.values[S1P_ARP_ON], total=ux_live.arp_total;
    if (!on || !total) {
        ux_big(on ? "ARPEGGIATOR" : "ARP OFF",on ? "PLAY A CHORD" :
            (ui.status & 2u) ? "DRONE" : "BYPASS",(ui.status & 2u) ?
            (set.drone_replace ? "REPLACE" : "STACK") : "DIRECTION / RANGE / GATE");
        return;
    }
    const int32_t count=total < 16 ? total : 16;
    const int32_t width=208/count;
    for(i=0;i<count;i++) {
        const int32_t x=16+i*width;
        cv_rect(x,19,width-3,28,i==ux_live.arp_step%count ? UX_AMBER : UX_RULE);
    }
    char text[32], n[8]; text[0]=0;
    if (ux_live.arp_note < 0) text_cat(text,"BETWEEN NOTES");
    else {
        static const char *const notes[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        text_cat(text,"NOTE ");text_cat(text,notes[ux_live.arp_note%12]);
        int_text(ux_live.arp_note/12-1,n);text_cat(text,n);
    }
    ux_display_text(8,2,&FONT_T,text,UX_MUTED);
    text[0]=0;text_cat(text,"STEP ");int_text((int32_t)ux_live.arp_step+1,n);text_cat(text,n);
    text_cat(text,"/");int_text(total,n);text_cat(text,n);
    ux_caption(text,(ui.status&2u) ? "HOLD ON" : "HOLD OFF");
}

static void ux_voice(void)
{
    char text[40];
    if(ui.row==0) {
        value_text(S1P_PLAY_MODE,cur.values[S1P_PLAY_MODE],text);
        ux_big("VOICE MODE",text,cur.values[S1P_PORTA_TIME] ? "PORTAMENTO ON" : "PORTAMENTO OFF");
    } else if(ui.row<3) {
        const int32_t count=clamp(cur.values[S1P_UNISON_VOICES],2,8);
        int32_t i;
        for(i=0;i<count;i++) cv_rect(120-count*12+i*24,22,12,24,cur.values[S1P_UNISON_ON]?UX_TEXT:UX_RULE);
        ux_caption(cur.values[S1P_UNISON_ON] ? "UNISON ON" : "UNISON OFF", "SPREAD / PITCH");
    } else if(ui.row==3) {
        ux_big("PATCH CATEGORY",CATEGORY[cur_category],set.drone_replace ? "DRONE / REPLACE" : "DRONE / STACK");
    } else if(ui.row==5) {
        ux_big("DISPLAY STYLE", UX_LCD ? "GREEN LCD" : UX_GREEN ? "GREEN LED" : "NORD", "KNOB 1 / CHOOSE YOUR DISPLAY");
    } else {
        ux_display_text(8,2,&FONT_XS,"PROCESSOR",UX_MUTED);
        cv_rect(12,25,216,12,UX_RULE);cv_rect(12,25,clamp((int32_t)shown_cpu,0,100)*216/100,12,shown_cpu>85?UX_AMBER:UX_TEXT);
        if(ui.ux_last_id<S1_NPARAMS) {
            char n[8];text[0]=0;text_cat(text,"P");int_text(ui.ux_last_id,n);text_cat(text,n);
            text_cat(text," ");text_cat(text,ux_label(ui.ux_last_id));
        } else {text[0]=0;text_cat(text,"TURN A PARAMETER TO INSPECT RAW");}
        ux_caption(text,cpu_sheds ? "VOICE LIMIT" : "READ ONLY");
    }
}

static void ux_graph(void)
{
    cv_begin(240,80,UX_BG);
    if(ui.page==0 && ui.row==0) ux_filter();
    else if(ui.page==0) {
        char tempo[16];int_text((int32_t)(ext_tempo?ext_tempo:set.tempo),tempo);
        ux_big(ext_tempo?"EXTERNAL CLOCK / BPM":"INTERNAL TEMPO / BPM",tempo,ext_tempo ?
            "TURN TEMPO FOR INTERNAL CLOCK" : cur.values[S1P_PLAY_MODE]==0?"POLYPHONIC":cur.values[S1P_PLAY_MODE]==1?"MONOPHONIC":"LEGATO");
    } else if(ui.page==1) ux_osc();
    else if(ui.page==2 && ui.row==0) ux_filter();
    else if(ui.page==2) ux_big("FILTER RESPONSE",cur.values[S1P_FILTER_VEL_SWITCH]?"VELOCITY ON":"VELOCITY OFF","KEY TRACK / SATURATION");
    else if(ui.page==3 && ui.row<2) ux_envelope((int)ui.row);
    else if(ui.page==3) ux_big("AMPLIFIER","GAIN / VELOCITY","OUTPUT LEVEL / PLAYING DYNAMICS");
    else if(ui.page==4) ux_lfo();
    else if(ui.page==5) ux_fx();
    else if(ui.page==6) ux_arp();
    else ux_voice();
    cv_blit(0,56);
}

static int ux_motion(void)
{
    if(ui.page==3 && ui.row<2) return ux_live.active || ux_live.stage[ui.row];
    if(ui.page==1 && ui.row>=4) return cur.values[S1P_MOD_ENV_ON] && ux_live.active;
    if(ui.page==4) return cur.values[ui.row>=2?S1P_LFO2_ON:S1P_LFO1_ON];
    return ui.page==6 && cur.values[S1P_ARP_ON] && ux_live.arp_total;
}

static void ux_header(void)
{
    char text[40];
    cv_begin(240,40,UX_BG);
    index_text(cur_index,text);ux_display_text(6,0,&FONT_XS,text,UX_MUTED);
    ux_fit(text,sizeof text,CATEGORY[set.category],&FONT_XS,90);ux_display_text(62,0,&FONT_XS,text,UX_MUTED);
    if(cur_dirty) ux_display_text(205,0,&FONT_XS,"EDIT",UX_AMBER);
    ux_fit(text,sizeof text,cur.name,&FONT_M,228);ux_display_text(6,14,&FONT_M,text,UX_TEXT);
    cv_blit(0,0);
}

static void ux_module(void)
{
    char text[52], fitted[40], n[24];
    const int message=plat_ms()<ui.msg_until && ui.msg_priority>0;
    cv_begin(240,16,UX_RED);
    ux_fit(fitted,sizeof fitted,message ? PAGES[ui.page].title : ux_row_title(),&FONT_XS,message?72:190);
    ux_display_text(6,0,&FONT_XS,fitted,UX_TEXT);
    text[0]=0;
    if(message) {
        text_cat(text,ui.msg[0]);text_cat(text," ");
        if(ux_equal(ui.msg[0],"SAVED")) index_text(cur_index,n);
        else if(ux_equal(ui.msg[0],"REVERT")) {n[0]=0;text_cat(n,"OK");}
        else {ux_fit(n,sizeof n,ui.msg[1],&FONT_T,65);}
        text_cat(text,n);
    } else {
        int_text((int32_t)ui.row+1,text);text_cat(text,"/");int_text(PAGES[ui.page].nrows,n);text_cat(text,n);
    }
    const felucca_font_t *font=message?&FONT_T:&FONT_XS;
    ux_fit(fitted,sizeof fitted,text,font,message?146:32);
    ux_display_text(234-ux_display_width(font,fitted),message?2:0,font,fitted,UX_TEXT);
    cv_blit(0,40);
}

/* Only changed zones are transferred. Animation never repaints the knob strip or patch name. */
static uint32_t ux_cache[5];
static uint8_t ux_cache_valid;
static uint32_t ux_hash(uint32_t hash, uint32_t value) { return (hash^value)*16777619u; }

static void ux_draw(uint32_t areas)
{
    uint32_t keys[5]={2166136261u,2166136261u,2166136261u,2166136261u,2166136261u},i;
    for(i=0;i<5;i++) keys[i]=ux_hash(keys[i],set.display);
    keys[0]=ux_hash(ux_hash(ux_hash(keys[0],cur_index),set.category),cur_dirty);
    for(i=0;i<S1_NAME_LEN && cur.name[i];i++) keys[0]=ux_hash(keys[0],(uint8_t)cur.name[i]);
    for(i=1;i<4;i++) keys[i]=ux_hash(ux_hash(keys[i],ui.page),ui.row);
    keys[2]=ux_hash(keys[2],(uint32_t)(ui.ux_focus+1));
    keys[3]=ux_hash(keys[3],(uint32_t)(ui.ux_focus+1));
    const int message=plat_ms()<ui.msg_until && ui.msg_priority>0;
    keys[1]=ux_hash(keys[1],(uint32_t)message);
    if(message) for(i=0;i<24;i++) {
        keys[1]=ux_hash(keys[1],(uint8_t)ui.msg[0][i]);keys[1]=ux_hash(keys[1],(uint8_t)ui.msg[1][i]);
    }
    keys[1]=ux_hash(keys[1],cur_index);
    keys[2]=ux_hash(keys[2],ui.ref_serial);
    for(i=0;i<S1_NPARAMS;i++) keys[2]=ux_hash(keys[2],(uint32_t)cur.values[i]);
    if(ux_motion()) keys[2]=ux_hash(keys[2],ux_live.sequence);
    if(ui.page==7 && ui.row==4)
        keys[2]=ux_hash(ux_hash(ux_hash(ux_hash(keys[2],shown_cpu),cpu_high),cpu_sheds),ui.ux_last_id);
    keys[2]=ux_hash(ux_hash(ux_hash(keys[2],ext_tempo?ext_tempo:set.tempo),cur_category),ui.status);
    keys[2]=ux_hash(keys[2],set.drone_replace);
    keys[3]=ux_hash(ux_hash(keys[3],ui.ref_serial),ui.ux_last_id);
    keys[3]=ux_hash(keys[3],ext_tempo!=0);
    for(i=0;i<4;i++) {
        const uint32_t id=PAGES[ui.page].rows[ui.row][i];
        if(id!=X_NONE) keys[3]=ux_hash(keys[3],(uint32_t)cell_value(id));
    }
    keys[4]=ux_hash(ux_hash(ux_hash(ux_hash(keys[4],shown_voices),shown_midi),set.octave),ui.status);
    keys[4]=ux_hash(ux_hash(ux_hash(keys[4],ext_tempo?ext_tempo:set.tempo),ext_tempo!=0),set.voices);
    keys[4]=ux_hash(ux_hash(keys[4],set.drone_replace),cpu_sheds);
    for(i=0;i<5;i++) if((areas&(1u<<i)) && (!(ux_cache_valid&(1u<<i)) || ux_cache[i]!=keys[i])) {
        if(i==0) ux_header();else if(i==1) ux_module();else if(i==2) ux_graph();else if(i==3) ux_controls();else ux_status();
        ux_cache[i]=keys[i];ux_cache_valid|=(uint8_t)(1u<<i);
    }
}
