/* The patches: a few built in (our own, so the FM-1 sounds before anything is loaded), and the user's, which
 * are Synth1 patches converted on the computer (tools: build/sy1pack) and sent over USB SysEx into the flash's
 * data area. Part of the unity build (included by app.c).
 *
 * A user patch is a 256-byte record at PLAT_DATA_LO + slot * 256:
 *   [0] 0xA5 used (0xFF: empty)   [1] format 1   [2] category [3] 0xC1 if explicit
 *   [4..27] name   [28..225] 99 values, int16 LE. Missing category: infer from name.
 *
 * SysEx, both ways, F0 7D 53 59 31 <cmd> ... F7 (7D: the non-commercial ID; "SY1"):
 *   01 <slot lo7> <slot hi7> <7-bit packed bytes 0..225 of the record>   write a slot  -> 41 <lo7> <hi7> <0 ok>
 *   02                                                                   erase all     -> 42 <0 ok>
 *   03                                                                   how many?     -> 43 <count lo7> <hi7> <slots lo7> <hi7>
 *   04 <slot lo7> <slot hi7>                                             a slot's name -> 44 <lo7> <hi7> <1 used> <name, ASCII>
 *   05 <slot lo7> <slot hi7>                                             empty a slot  -> 45 <lo7> <hi7> <0 ok>
 *   06 <offset lo7> <mid7> <hi7>                            bundled credits (read only) -> 46 <offset, 3 bytes> <up to 96 bytes as high/low nibbles>
 * 7-bit packing: each 7 bytes as 8, first the 7 top bits (bit 0 = the first byte's), then the 7 low parts. */
#pragma once
#include "s1_patch.h"
#include "category.h"

#define BANK_SLOTS 1024u
#define BANK_REC 256u
#define BANK_BASE PLAT_DATA_LO
#define BANK_USED 0xA5u
#define BANK_PAYLOAD 226u                /* the record's bytes that travel */
_Static_assert(BANK_BASE + BANK_SLOTS * BANK_REC <= 0xDC000u, "the bank must stay under the store's objects");

/* ---------------------------------------------------------------- built in --- */
#define E 255                            /* a list's end */
/* every built-in patch starts from this plain sound: one saw, an open 12 dB filter, no modulation, no effects */
static const uint8_t FACT_BASE[] = {
    S1P_OSC1_SHAPE, 1, S1P_OSC2_SHAPE, 1, S1P_OSC2_PITCH, 64, S1P_OSC2_FINE, 64, S1P_OSC_MIX, 0,
    S1P_OSC_PULSE_WIDTH, 127, S1P_FILTER_TYPE, 0, S1P_FILTER_FREQ, 110, S1P_FILTER_RES, 10, S1P_FILTER_AMOUNT, 64,
    S1P_FILTER_KBD_TRACK, 64, S1P_FILTER_ATTACK, 0, S1P_FILTER_DECAY, 64, S1P_FILTER_SUSTAIN, 64,
    S1P_FILTER_RELEASE, 64, S1P_AMP_ATTACK, 0, S1P_AMP_DECAY, 64, S1P_AMP_SUSTAIN, 115, S1P_AMP_RELEASE, 45,
    S1P_AMP_GAIN, 64, S1P_AMP_VEL_SENS, 40, S1P_LFO1_ON, 0, S1P_LFO2_ON, 0, S1P_LFO1_DEPTH, 0, S1P_LFO2_DEPTH, 0,
    S1P_DELAY_SPREAD, 64, S1P_DELAY_FEEDBACK, 45, S1P_DELAY_DRY_WET, 28, S1P_CHORUS_LEVEL, 70, E};
static const uint8_t FACT_BASS[] = {
    S1P_OSC2_SHAPE, 2, S1P_OSC_MIX, 40, S1P_FILTER_TYPE, 1, S1P_FILTER_FREQ, 46, S1P_FILTER_RES, 38,
    S1P_FILTER_AMOUNT, 92, S1P_FILTER_DECAY, 52, S1P_FILTER_SUSTAIN, 18, S1P_AMP_SUSTAIN, 100, S1P_AMP_RELEASE, 30,
    S1P_PLAY_MODE, 1, S1P_PORTA_TIME, 18, S1P_PORTA_AUTO, 1, S1P_SUB_GAIN, 70, S1P_SUB_SHAPE, 3, S1P_SUB_OCTAVE, 1,
    S1P_FILTER_SAT, 30, E};
static const uint8_t FACT_PAD[] = {
    S1P_OSC2_FINE, 71, S1P_OSC_MIX, 64, S1P_OSC1_DETUNE, 38, S1P_FILTER_FREQ, 72, S1P_FILTER_RES, 18,
    S1P_FILTER_AMOUNT, 78, S1P_FILTER_ATTACK, 82, S1P_FILTER_DECAY, 90, S1P_FILTER_SUSTAIN, 90,
    S1P_FILTER_RELEASE, 95, S1P_AMP_ATTACK, 84, S1P_AMP_DECAY, 90, S1P_AMP_SUSTAIN, 112, S1P_AMP_RELEASE, 96,
    S1P_CHORUS_ON, 1, S1P_CHORUS_TYPE, 2, S1P_LFO1_ON, 1, S1P_LFO1_DEST, 5, S1P_LFO1_TYPE, 1, S1P_LFO1_SPEED, 40,
    S1P_LFO1_DEPTH, 30, E};
static const uint8_t FACT_LEAD[] = {
    S1P_OSC1_SHAPE, 2, S1P_OSC2_FINE, 69, S1P_OSC_MIX, 48, S1P_OSC_PULSE_WIDTH, 100, S1P_FILTER_TYPE, 1,
    S1P_FILTER_FREQ, 88, S1P_FILTER_RES, 30, S1P_AMP_SUSTAIN, 127, S1P_AMP_RELEASE, 42, S1P_PLAY_MODE, 2,
    S1P_PORTA_TIME, 36, S1P_LFO1_ON, 1, S1P_LFO1_DEST, 2, S1P_LFO1_TYPE, 5, S1P_LFO1_SPEED, 84, S1P_LFO1_DEPTH, 5,
    S1P_DELAY_ON, 1, S1P_DELAY_TIME, 10, E};
static const uint8_t FACT_PLUCK[] = {
    S1P_OSC2_SHAPE, 2, S1P_OSC_MIX, 50, S1P_OSC_PULSE_WIDTH, 80, S1P_FILTER_FREQ, 38, S1P_FILTER_RES, 46,
    S1P_FILTER_AMOUNT, 112, S1P_FILTER_DECAY, 46, S1P_FILTER_SUSTAIN, 0, S1P_FILTER_RELEASE, 50, S1P_AMP_DECAY, 62,
    S1P_AMP_SUSTAIN, 0, S1P_AMP_RELEASE, 58, S1P_DELAY_ON, 1, S1P_DELAY_TYPE, 2, S1P_DELAY_TIME, 10, E};
static const uint8_t FACT_SYNC[] = {
    S1P_OSC_MIX, 127, S1P_OSC2_SYNC, 1, S1P_OSC2_PITCH, 84, S1P_MOD_ENV_ON, 1, S1P_MOD_ENV_DEST, 0,
    S1P_MOD_ENV_AMOUNT, 92, S1P_MOD_ENV_ATTACK, 0, S1P_MOD_ENV_DECAY, 86, S1P_FILTER_TYPE, 1, S1P_FILTER_FREQ, 104,
    S1P_AMP_SUSTAIN, 120, S1P_PLAY_MODE, 1, S1P_DELAY_ON, 1, S1P_DELAY_TIME, 8, E};
static const uint8_t FACT_BELL[] = {
    S1P_OSC1_SHAPE, 0, S1P_OSC2_SHAPE, 3, S1P_OSC2_PITCH, 95, S1P_OSC1_FM, 58, S1P_FILTER_FREQ, 120,
    S1P_AMP_DECAY, 92, S1P_AMP_SUSTAIN, 0, S1P_AMP_RELEASE, 92, S1P_AMP_VEL_SENS, 70, S1P_CHORUS_ON, 1,
    S1P_CHORUS_TYPE, 2, S1P_CHORUS_LEVEL, 50, S1P_AMP_GAIN, 50, E};
static const uint8_t FACT_STRINGS[] = {
    S1P_UNISON_ON, 1, S1P_UNISON_VOICES, 2, S1P_UNISON_DETUNE, 44, S1P_UNISON_PAN, 110, S1P_UNISON_PITCH, 24,
    S1P_OSC2_FINE, 68, S1P_OSC_MIX, 60, S1P_FILTER_FREQ, 84, S1P_AMP_ATTACK, 72, S1P_AMP_SUSTAIN, 118,
    S1P_AMP_RELEASE, 88, S1P_CHORUS_ON, 1, S1P_CHORUS_TYPE, 4, S1P_EQ_TONE, 58, S1P_AMP_GAIN, 50, E};
static const uint8_t FACT_ARP[] = {
    S1P_ARP_ON, 1, S1P_ARP_TYPE, 2, S1P_ARP_OCT_RANGE, 1, S1P_ARP_BEAT, 14, S1P_ARP_GATE, 70, S1P_OSC1_SHAPE, 2,
    S1P_OSC_PULSE_WIDTH, 90, S1P_FILTER_TYPE, 1, S1P_FILTER_FREQ, 52, S1P_FILTER_RES, 44, S1P_FILTER_AMOUNT, 100,
    S1P_FILTER_DECAY, 50, S1P_FILTER_SUSTAIN, 10, S1P_AMP_DECAY, 60, S1P_AMP_SUSTAIN, 40, S1P_AMP_RELEASE, 40,
    S1P_DELAY_ON, 1, S1P_DELAY_TIME, 7, S1P_DELAY_TYPE, 1, E};
#undef E
static const struct { const char *name; const uint8_t *set; } FACT[] = {
    {"Saw Bass", FACT_BASS}, {"Warm Pad", FACT_PAD}, {"Pulse Lead", FACT_LEAD}, {"Pluck", FACT_PLUCK},
    {"Sync Lead", FACT_SYNC}, {"FM Bell", FACT_BELL}, {"Strings", FACT_STRINGS}, {"Arp", FACT_ARP},
};
#define NFACT ((uint32_t)(sizeof FACT / sizeof FACT[0]))

/* Community factory sounds live in program flash, after the fixed user-ID range.
 * Keeping these IDs separate preserves old MIDI program numbers and saved selections. */
#include "presets/factory500.h"
#define FACTORY500_BASE (NFACT + BANK_SLOTS)
#define FACTORY500_END (FACTORY500_BASE + FACTORY500_COUNT)
static int factory500_has(uint32_t index) { return index >= FACTORY500_BASE && index < FACTORY500_END; }
static int user_index(uint32_t index) { return index >= NFACT && index < FACTORY500_BASE; }

static void fact_load(uint32_t i, s1_patch *p)
{
    const uint8_t *lists[2], *l;
    uint32_t k;
    lists[0] = FACT_BASE;
    lists[1] = FACT[i].set;
    s1_patch_init(p);
    for (k = 0; k < 2u; k++)
        for (l = lists[k]; *l != 255; l += 2)
            p->values[l[0]] = l[1];
    for (k = 0; k < S1_NAME_LEN - 1u && FACT[i].name[k]; k++)
        p->name[k] = FACT[i].name[k];
    for (; k < S1_NAME_LEN; k++)
        p->name[k] = 0;
}

/* ------------------------------------------------------------------- user --- */
static uint32_t bank_count;              /* slots up to the last used one */
static uint8_t bank_category[BANK_SLOTS]; /* built at scan time, keeping panel browsing cheap */
static uint8_t bank_sector[4096] PLAT_POOL;

static const uint8_t *bank_rec(uint32_t slot) { return slot < BANK_SLOTS ? plat_data(BANK_BASE + slot * BANK_REC) : 0; }
static int bank_used(uint32_t slot)
{
    const uint8_t *r = bank_rec(slot);
    return r && r[0] == BANK_USED && r[1] == 1u;
}

static void bank_scan(void)
{
    uint32_t i;
    bank_count = 0;
    for (i = 0; i < BANK_SLOTS; i++)
        if (bank_used(i)) {
            const uint8_t *r = bank_rec(i);
            char name[24];
            uint32_t k;
            for (k = 0; k < 23u; k++)
                name[k] = (char)r[4u + k];
            name[23] = 0;
            bank_category[i] = r[3] == CAT_MARK && r[2] >= CAT_BASS && r[2] <= CAT_OTHER ? r[2] : category_guess(name);
            bank_count = i + 1u;
        }
}

static void bank_decode(const uint8_t *r, s1_patch *p)
{
    uint32_t i;
    for (i = 0; i < 24u; i++)
        p->name[i] = (char)r[4u + i];
    for (; i < S1_NAME_LEN; i++)
        p->name[i] = 0;
    p->name[23] = 0;
    p->version = 112;
    for (i = 0; i < S1_NPARAMS; i++) {
        int32_t v = (int16_t)(uint16_t)(r[28u + 2u * i] | (uint32_t)r[29u + 2u * i] << 8);
        if (i >= S1P_CTRL_SRC1 && i <= S1P_CTRL_ASSIGN2 && v != -1)
            v &= 0xFFFF;                 /* the controllers' words are unsigned (45057: the wheel) */
        p->values[i] = v < s1_param_min[i] ? s1_param_min[i] : v > s1_param_max[i] ? s1_param_max[i] : v;
    }
}

static int bank_load(uint32_t slot, s1_patch *p)
{
    if (!bank_used(slot)) return 0;
    bank_decode(bank_rec(slot), p);
    return 1;
}

static void bank_pack(const s1_patch *p, uint8_t *rec)   /* rec: BANK_REC bytes */
{
    uint32_t i;
    for (i = 0; i < BANK_REC; i++)
        rec[i] = 0xFF;
    rec[0] = BANK_USED;
    rec[1] = 1;
    for (i = 0; i < 24u; i++)
        rec[4u + i] = i < 23u ? (uint8_t)p->name[i] : 0;
    for (i = 0; i < S1_NPARAMS; i++) {
        rec[28u + 2u * i] = (uint8_t)(p->values[i] & 0xFF);
        rec[29u + 2u * i] = (uint8_t)(p->values[i] >> 8 & 0xFF);
    }
}

/* a record into the flash: straight into erased bytes, else the 4 KiB sector rewritten. The sound stops a moment.
 * rec 0: the slot emptied. 0 = done */
static int bank_write(uint32_t slot, const uint8_t *rec)
{
    const uint32_t off = BANK_BASE + slot * BANK_REC, sec = off & ~4095u;
    const uint8_t *now = bank_rec(slot), *s;
    uint32_t i, blank = 1;
    if (!now)
        return -1;
    for (i = 0; i < BANK_REC; i++)
        blank &= now[i] == 0xFFu;
    if (blank && rec) {
        if (plat_data_write(off, rec, BANK_REC))
            return -1;
    } else if (!blank) {
        s = plat_data(sec);
        for (i = 0; i < 4096u; i++)
            bank_sector[i] = s[i];
        for (i = 0; i < BANK_REC; i++)
            bank_sector[off - sec + i] = rec ? rec[i] : 0xFFu;
        if (plat_data_erase(sec) || plat_data_write(sec, bank_sector, 4096u))
            return -1;
    }
    bank_scan();
    return 0;
}

static int bank_erase_all(void)
{
    uint32_t sec, i;
    int r = 0;
    for (sec = BANK_BASE; sec < BANK_BASE + BANK_SLOTS * BANK_REC; sec += 4096u) {
        const uint8_t *s = plat_data(sec);
        uint32_t blank = 1;
        for (i = 0; i < 4096u && blank; i++)
            blank = s[i] == 0xFFu;
        if (!blank)
            r |= plat_data_erase(sec);
    }
    bank_scan();
    return r;
}

static int bank_free_slot(void)
{
    uint32_t i;
    for (i = 0; i < BANK_SLOTS; i++)
        if (!bank_used(i))
            return (int)i;
    return -1;
}
