#include "s1_patch.h"

void s1_patch_init(s1_patch *p)
{
    int i;
    for (i = 0; i < S1_NAME_LEN; i++)
        p->name[i] = 0;
    p->name[0] = 'I'; p->name[1] = 'N'; p->name[2] = 'I'; p->name[3] = 'T';
    p->version = 112;
    for (i = 0; i < S1_NPARAMS; i++)
        p->values[i] = s1_param_def[i];
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* an integer with spaces around it, the whole of [s, e) */
static int parse_int(const char *s, const char *e, int32_t *out)
{
    int neg = 0, digits = 0;
    int32_t v = 0;
    while (s < e && (*s == ' ' || *s == '\t'))
        s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        e--;
    if (s < e && (*s == '-' || *s == '+'))
        neg = *s++ == '-';
    for (; s < e; s++, digits++) {
        if (*s < '0' || *s > '9')
            return 0;
        if (v < 100000000)
            v = v * 10 + (*s - '0');
    }
    *out = neg ? -v : v;
    return digits > 0;
}

/* Synth1's conversion of an old-version patch (oxo_synth2's measurements) */
static void convert_legacy(int32_t *v, int version)
{
    int group, i, mono;
    int32_t old[S1_NPARAMS];
    switch (version) {
    case 0: case 100: group = 1; break;
    case 105: case 106: group = 2; break;
    case 107: case 108: group = 4; break;
    default: return;
    }
    for (i = 0; i < S1_NPARAMS; i++)
        old[i] = v[i];
    for (i = 0; i < S1_LEGACY_NRULES; i++) {
        const s1_legacy_rule *r = &s1_legacy_rules[i];
        if (r->groups & group)
            v[r->param] = r->table < 0 ? r->value : s1_legacy_tab[r->table][clampi(old[r->param], 0, 127)];
    }
    /* the wheel's sens 1 / 2 pulled lfo1's depth / speed toward 127; now they add */
    v[S1P_WHEEL_SPEED_SENS] = 64 + old[S1P_WHEEL_SPEED_SENS] * (127 - old[S1P_LFO1_DEPTH]) * 63 / (127 * 127);
    v[S1P_WHEEL_AMOUNT_SENS] = 64 + old[S1P_WHEEL_AMOUNT_SENS] * (127 - old[S1P_LFO1_SPEED]) * 63 / (127 * 127);
    /* unison had no knobs: 2 voices poly, 4 mono */
    mono = old[S1P_PLAY_MODE] != 0;
    v[S1P_UNISON_VOICES] = mono ? 4 : 2;
    v[S1P_UNISON_DETUNE] = group == 4 ? s1_legacy_tab[mono ? 12 : 11][clampi(old[S1P_UNISON_DETUNE], 0, 127)]
                                      : mono ? 70 : 48;
}

int s1_patch_parse(s1_patch *p, const char *text, uint32_t len)
{
    const char *s = text, *end = text + len;
    int first = 1, count = 0, i;
    s1_patch_init(p);
    p->version = 0;
    while (s < end) {
        const char *e = s, *sep;
        while (e < end && *e != '\n' && *e != '\r')
            e++;
        if (first) {
            for (i = 0; i < S1_NAME_LEN - 1 && s + i < e; i++)
                p->name[i] = s[i];
            for (; i < S1_NAME_LEN; i++)
                p->name[i] = 0;
            first = 0;
        } else {
            for (sep = s; sep < e && *sep != '=' && *sep != ','; sep++) {}
            if (sep < e && *sep == '=') {
                int32_t v;
                if (sep - s == 3 && s[0] == 'v' && s[1] == 'e' && s[2] == 'r' && parse_int(sep + 1, e, &v))
                    p->version = (int)v;
            } else if (sep < e) {
                int32_t id, v;
                if (parse_int(s, sep, &id) && parse_int(sep + 1, e, &v) && id >= 0 && id < S1_NPARAMS) {
                    p->values[id] = v < s1_param_min[id] ? s1_param_min[id] : v > s1_param_max[id] ? s1_param_max[id] : v;
                    count++;
                }
            }
        }
        s = e < end ? e + 1 : e;
    }
    convert_legacy(p->values, p->version);
    for (i = 0; i < S1_NPARAMS; i++)   /* a remap never leaves the range, but the rules' values are trusted less */
        p->values[i] = p->values[i] < s1_param_min[i] ? s1_param_min[i]
                     : p->values[i] > s1_param_max[i] ? s1_param_max[i] : p->values[i];
    return count;
}
