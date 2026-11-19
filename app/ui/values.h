/* Display conversions use the engine's measured tables. Stored Synth1 values stay unchanged.
 * Decay/release show time constants explicitly, not a misleading envelope duration. */
#pragma once
static void ux_append(char *out, uint32_t cap, const char *text)
{
    uint32_t n=0;
    while(n+1u<cap && out[n]) n++;
    while(n+1u<cap && *text) out[n++]=*text++;
    out[n]=0;
}

static const char *const UX_LFO_BEATS[30] = {"64","48","36","32","24","18","16","12","9","8","6","4",
    "7/2","3","2","7/4","3/2","4/3","1","7/8","3/4","2/3","1/2","3/8","1/3","1/4","1/6","1/8","1/12","1/24"};
static const char *const UX_ARP_BEATS[19] = {"4","7/2","3","2","7/4","3/2","4/3","1","7/8","3/4","2/3",
    "1/2","3/8","1/3","1/4","1/6","1/8","1/12","1/24"};
static const char *const UX_DELAY_BEATS[20] = {"MIN","1/24","1/12","1/8","1/6","1/4","1/3","3/8","1/2","2/3",
    "3/4","7/8","1","4/3","3/2","7/4","2","3","7/2","4"};

static void ux_decimal(float value, char *text)
{
    if (value < 0.0f) { text[0]='-'; ux_decimal(-value,text+1); return; }
    if (value > 0.0f && value < 1.0f) {
        const int32_t hundredths=(int32_t)(value*100.0f+0.5f);
        if (hundredths>=100) {int_text(1,text);return;}
        text[0]='0';text[1]='.';text[2]=(char)('0'+hundredths/10);
        text[3]=(char)('0'+hundredths%10);text[4]=0;
        if (hundredths%10==0) text[3]=0;
        return;
    }
    const int32_t tenths = (int32_t)(value * 10.0f + 0.5f);
    char digit[4];
    int_text(tenths / 10, text);
    if (tenths < 100 && tenths % 10) {
        text_cat(text, "."); int_text(tenths % 10, digit); text_cat(text, digit);
    }
}

static void ux_value(uint32_t id, int32_t value, char *text, const char **unit)
{
    float seconds;
    text[0] = 0;
    *unit = "";
    switch (id) {
    case S1P_AMP_ATTACK: case S1P_FILTER_ATTACK: case S1P_MOD_ENV_ATTACK:
        seconds = s1_t_attack_s[value]; goto duration;
    case S1P_AMP_DECAY: case S1P_FILTER_DECAY: case S1P_MOD_ENV_DECAY:
    case S1P_AMP_RELEASE: case S1P_FILTER_RELEASE:
        seconds = s1_t_env_tau_s[value];
    duration:
        if (seconds < 1.0f) { ux_decimal(seconds * 1000.0f, text); *unit = "ms"; }
        else { ux_decimal(seconds, text); *unit = "s"; }
        return;
    case S1P_LFO1_SPEED: case S1P_LFO2_SPEED: {
        const int which = id == S1P_LFO2_SPEED;
        if (cur.values[which ? S1P_LFO2_TEMPO_SYNC : S1P_LFO1_TEMPO_SYNC]) {
            text_cat(text,UX_LFO_BEATS[value * 30 / 128]); *unit = "beat";
        } else { ux_decimal(s1_exp2f((float)value / 12.0367f) / 12.0f, text); *unit = "Hz"; }
        return;
    }
    case S1P_OSC2_PITCH: int_text(s1_t_osc2_semis[value], text); *unit = "st"; return;
    case S1P_OSC2_FINE: case S1P_OSC_FINE_TUNE:
        int_text(s1_t_fine_cents[value], text); *unit = "cent"; return;
    case S1P_OSC_KEY_SHIFT: case S1P_BEND_RANGE: case S1P_UNISON_PITCH:
        int_text(value, text); *unit = "st"; return;
    case S1P_OSC_PULSE_WIDTH:
        ux_decimal((float)value * 100.0f / 254.0f, text); *unit = "% low"; return;
    case S1P_AMP_SUSTAIN:
        ux_decimal(s1_t_amp_sustain[value] * 100.0f, text); *unit = "%"; return;
    case S1P_FILTER_SUSTAIN:
        ux_decimal((float)value * 100.0f / 127.0f, text); *unit = "%"; return;
    case S1P_ARP_GATE:
        ux_decimal((float)value * 100.0f / 127.0f, text); *unit = "%"; return;
    case S1P_ARP_OCT_RANGE: int_text(value+1,text); *unit="oct"; return;
    case S1P_ARP_BEAT: text_cat(text,UX_ARP_BEATS[value]); *unit="beat"; return;
    case S1P_DELAY_TIME: text_cat(text,UX_DELAY_BEATS[value]); *unit=value?"beat":""; return;
    case S1P_UNISON_DETUNE:
        ux_decimal(s1_t_unison_cents[value], text); *unit = "cent"; return;
    case S1P_EQ_FREQ: {
        const float hz=50.0f*s1_powf(320.0f,(float)value/127.0f);
        ux_decimal(hz>=1000.0f?hz/1000.0f:hz,text);*unit=hz>=1000.0f?"kHz":"Hz";return;
    }
    case S1P_EQ_LEVEL: ux_decimal((float)(value-64)*50.0f/127.0f,text);*unit="dB";return;
    case S1P_CHORUS_RATE:
        ux_decimal(value<=0?0.01f:2.9409f*s1_exp2f((float)(value-64)/8.89f),text);*unit="Hz";return;
    case X_TEMPO: int_text(value, text); *unit = "BPM"; return;
    case X_CHL: case X_CHR: int_text(value, text); return;
    case X_CPU: int_text((int32_t)shown_cpu, text); *unit = "%"; return;
    case X_PEAK: int_text((int32_t)cpu_high, text); *unit = "%"; return;
    case X_SHED: int_text((int32_t)cpu_sheds, text); return;
    case X_RAW:
        if (ui.ux_last_id < S1_NPARAMS) int_text(cur.values[ui.ux_last_id], text);
        else text_cat(text, "--");
        return;
    default: value_text(id, value, text); return;
    }
}

static const char *ux_label(uint32_t id)
{
    switch (id) {
    case S1P_FILTER_FREQ: return "CUTOFF";
    case S1P_FILTER_AMOUNT: return "ENV AMT";
    case S1P_AMP_DECAY: case S1P_FILTER_DECAY: return "D.TAU";
    case S1P_AMP_RELEASE: case S1P_FILTER_RELEASE: return "R.TAU";
    case S1P_MOD_ENV_DECAY: return "M.TAU";
    case X_CPU: return "CPU";
    case X_PEAK: return "PEAK";
    case X_SHED: return "LIMITS";
    case X_RAW: return "RAW";
    case X_TEMPO: return ext_tempo ? "INT BPM" : "TEMPO";
    default: return cell_name(id);
    }
}

static int ux_discrete(uint32_t id)
{
    if (id >= S1_NPARAMS) return id == X_CATEGORY || id == X_DRONE || id == X_DISPLAY || id == X_BI || id == X_LAYOUT || id == X_QPART || id == X_QOUTPUT;
    if (s1_param_max[id] - s1_param_min[id] == 1) return 1;
    switch (id) {
    case S1P_OSC1_SHAPE: case S1P_OSC2_SHAPE: case S1P_SUB_SHAPE: case S1P_FILTER_TYPE:
    case S1P_LFO1_DEST: case S1P_LFO2_DEST: case S1P_LFO1_TYPE: case S1P_LFO2_TYPE:
    case S1P_PLAY_MODE: case S1P_ARP_TYPE: case S1P_ARP_BEAT: case S1P_ARP_OCT_RANGE:
    case S1P_EFFECT_TYPE: case S1P_DELAY_TYPE: case S1P_DELAY_TIME: case S1P_CHORUS_TYPE:
    case S1P_MOD_ENV_DEST: return 1;
    default: return 0;
    }
}

static int ux_centered(uint32_t id)
{
    switch (id) {
    case S1P_OSC2_PITCH: case S1P_OSC2_FINE: case S1P_OSC_FINE_TUNE:
    case S1P_FILTER_AMOUNT: case S1P_MOD_ENV_AMOUNT: case S1P_PAN:
    case S1P_UNISON_PAN: case S1P_EQ_LEVEL: case S1P_EQ_TONE:
    case S1P_DELAY_SPREAD: case S1P_DELAY_TONE: case S1P_CHORUS_FEEDBACK:
    case S1P_WHEEL_SPEED_SENS: case S1P_WHEEL_AMOUNT_SENS: return 1;
    default: return 0;
    }
}
