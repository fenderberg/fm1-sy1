/* The app (app/app.c) run against a small stand-in for the FM-1: its panel, flash, store, MIDI and SysEx as
 * plain memory, enough to play it, send it patches and restart it. Not fm1-base's simulator (./build.sh --host
 * is: it has the real screen, panel and storage code); this one needs only plat.h, so it runs anywhere.
 *   make test-app BASE=path/to/fm1-base */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define PLAT_HOST
#include "plat.h"
#include "app.h"

/* ---- the stand-in ---- */
static uint32_t now_ms, btn_held, keys_held, sysex_n, store_len[OBJ_NOBJ], midi_n, midi_r, leds_btn;
static int32_t enc_turn[NE];
static uint8_t flash[0x100000], store[OBJ_NOBJ][64], sysex_last[64];
static uint32_t midi_q[64];

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return btn_held; }
int32_t plat_enc(int role) { const int32_t e = enc_turn[role]; enc_turn[role] = 0; return e; }
void plat_leds(uint32_t buttons, uint32_t keys) { leds_btn = buttons; (void)keys; }
void plat_glow(int on) { (void)on; }
int plat_midi_in(uint32_t *pkt, int *port)
{
    if (midi_r == midi_n)
        return 0;
    *pkt = midi_q[midi_r++ & 63u];
    if (port)
        *port = PORT_USB;
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to) { (void)pkt; (void)to; }
const uint8_t *plat_data(uint32_t off) { return PLAT_DATA_OK(off, 1u) ? flash + off : 0; }
int plat_data_erase(uint32_t off)
{
    if ((off & 4095u) || !PLAT_DATA_OK(off, 4096u))
        return -1;
    memset(flash + off, 0xFF, 4096);
    return 0;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)   /* as NOR flash: bits only go to 0 */
{
    uint32_t i;
    if (!PLAT_DATA_OK(off, n))
        return -1;
    for (i = 0; i < n; i++)
        flash[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
int plat_sysex_send(const uint8_t *frame, uint32_t n)
{
    sysex_n = n < sizeof sysex_last ? n : sizeof sysex_last;
    memcpy(sysex_last, frame, sysex_n);
    return 0;
}
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || !store_len[obj] || store_len[obj] > max)
        return -1;
    memcpy(dst, store[obj], store_len[obj]);
    return (int)store_len[obj];
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > sizeof store[0])
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = len;
    return 0;
}
uint32_t plat_cpu_pct(void) { return 12; }

typedef struct { int h; } felucca_font_t;
static const felucca_font_t FONT_XS = {12}, FONT_M = {24};
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu
static uint16_t pal[5];
#define C_LINE pal[0]
#define C_DIM pal[1]
#define C_GRAY pal[2]
#define C_AMB pal[3]
#define C_HI pal[4]
static char screen[4096];                /* every text drawn in the last frame, one after another */
static uint32_t draws;
void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg) { (void)w; (void)bg; if (h == 46u) { screen[0] = 0; draws++; } }
static void cv_blit(uint32_t x, uint32_t y) { (void)x; (void)y; }
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    (void)y; (void)f; (void)c;
    if (strlen(screen) + strlen(s) + 2 < sizeof screen) {
        strcat(screen, s);
        strcat(screen, "|");
    }
    return x + (int32_t)strlen(s) * 8;
}
static int32_t text_w(const felucca_font_t *f, const char *s) { (void)f; return (int32_t)strlen(s) * 8; }
#define ANN_NOW 1
static int ann_on(void) { return 0; }
static int ann_where(void) { return 0; }
static void ann_say(const char *t, int a, int how) { (void)t; (void)a; (void)how; }
static void ann_tick(uint32_t ms) { (void)ms; }
void touch_on(int on) { (void)on; }
void touch_poll(void) {}
uint32_t touch_keys(void) { return keys_held; }
uint32_t touch_vel(uint32_t k) { (void)k; return 100; }

#include "app.c"
#include "sy1frame.h"

/* ---- the test ---- */
static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t audio[2 * APP_BLOCK];
static float peak;

static void run(uint32_t ms)             /* the FM-1 for ms: a frame every 16 ms, the audio in blocks */
{
    uint32_t t, i;
    static uint32_t frames_due;
    peak = 0;
    for (t = 0; t < ms; t++) {
        now_ms++;
        if (now_ms % 16u == 0)
            app_frame();
        frames_due += 441;               /* 44.1 frames a millisecond, in tenths */
        while (frames_due >= APP_BLOCK * 10u) {
            frames_due -= APP_BLOCK * 10u;
            app_render(audio, APP_BLOCK, 4096);
            for (i = 0; i < 2 * APP_BLOCK; i++)
                if (fabsf((float)audio[i]) > peak)
                    peak = fabsf((float)audio[i]);
        }
    }
}

static void tap(uint32_t b) { btn_held |= 1u << b; run(40); btn_held &= ~(1u << b); run(40); }
static void turn(int e, int32_t d) { enc_turn[e] += d; run(40); }
static void midi(uint32_t st, uint32_t d1, uint32_t d2) { midi_q[midi_n++ & 63u] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24; }
static void power_on(void)
{
    memset(&ui, 0, sizeof ui);
    ev_w = ev_r = 0;
    app_init();
    app_ui_init();
    run(100);
}
static void send_sysex(const uint8_t *f, uint32_t n) { sysex_n = 0; CHECK(app_sysex(f + 1, n - 2) == 1, "SysEx frame not taken"); run(50); }

static void test_live_controls(void)
{
    uint8_t rec[BANK_REC];
    uint32_t k;
    patch_select(1);
    tap(B_HOME);
    tap(B_ARP);
    CHECK(cur.values[S1P_ARP_ON] && ui.page == 0 && (leds_btn & 1u << B_ARP), "ARP tap did not toggle on from HOME");
    tap(B_SEQ);
    keys_held = 1u << 7 | 1u << 11 | 1u << 14;
    run(100);
    keys_held = 0;
    run(300);
    CHECK(synth.hold && synth.arp_n == 3 && peak > 1000.0f && (leds_btn & 1u << B_SEQ),
          "panel chord was not held by SEQ");
    keys_held = 1u << 9 | 1u << 12;
    run(100);
    keys_held = 0;
    run(100);
    CHECK(synth.arp_n == 2, "panel HOLD appended instead of replacing a chord");
    tap(B_ARP);
    CHECK(!cur.values[S1P_ARP_ON] && synth.hold && synth.held_n == 2, "ARP off did not become a drone");
    tap(B_SEQ);
    CHECK(!synth.hold && synth.held_n == 0, "SEQ off left the drone held");
    btn_held = 1u << B_ARP;
    run(600);
    btn_held = 0;
    run(60);
    CHECK(ui.page == 6 && !cur.values[S1P_ARP_ON], "holding ARP toggled it instead of opening settings");
    tap(B_REC);
    CHECK(!cur_dirty && !synth.hold, "REC did not restore the saved sound");

    tap(B_GLO);
    turn(EN_SELECT, 3);
    turn(EN_K4, 1);                    /* drone REPLACE */
    tap(B_SEQ);
    midi(0x90, 60, 100);
    midi(0x90, 64, 100);
    midi(0x80, 60, 0);
    midi(0x90, 64, 0);                 /* velocity-zero note-off */
    run(100);
    CHECK(synth.latched[60] && synth.latched[64] && !synth.key_down[64], "MIDI drone was not latched");
    midi(0x90, 67, 100);
    midi(0x80, 67, 0);
    run(100);
    CHECK(synth.latched[67] && !synth.latched[60] && !synth.latched[64], "MIDI REPLACE kept the old chord");
    midi(0xFC, 0, 0);
    run(100);
    CHECK(!synth.hold && !s1_active_voices(&synth) && !(leds_btn & 1u << B_SEQ), "MIDI Stop left a drone or LED");
    tap(B_SEQ);
    midi(0x90, 60, 100);
    midi(0x80, 60, 0);
    run(100);
    tap(B_PLAY);
    CHECK(!synth.hold && !s1_active_voices(&synth), "PLAY left a drone");
    tap(B_SEQ);
    for (k = 0; k < 70; k++)
        ev_push(EV_NOTE_ON, 60, 100);
    run(100);
    CHECK(!synth.hold && !s1_active_voices(&synth), "event queue overflow left a held note");

    /* External clock disconnect returns to the musician's internal tempo. */
    for (k = 0; k < 50; k++) {
        midi(0xF8, 0, 0);
        run(18);
    }
    CHECK(ext_tempo > 130, "external clock was not acquired");
    run(2200);
    CHECK(!ext_tempo && synth.tempo == (float)set.tempo, "lost clock did not return to internal tempo");

    set.category = CAT_ALL;
    patch_select(0);
    turn(EN_ALGO, 1);
    CHECK(set.category == CAT_BASS && cur_index == 0, "ALGORITHM did not choose BASS");
    turn(EN_ALGO, 1);
    CHECK(set.category == CAT_PAD && cur_index == 1, "ALGORITHM did not choose first PAD");
    turn(EN_PRESET, 1);
    CHECK(cur_index == 6, "PAD browsing did not select Strings");
    turn(EN_PRESET, 1);
    CHECK(cur_index == 1, "PAD browsing did not wrap within category");
    turn(EN_PRESET, -1);
    CHECK(cur_index == 6, "PAD browsing backwards left category");
    turn(EN_ALGO, 4);                   /* LEAD, KEYS, PLUCK, ARP */
    CHECK(set.category == CAT_ARP && cur_index == 7, "category traversal gave %u patch %u", set.category, cur_index);
    turn(EN_ALGO, 1);
    CHECK(set.category == CAT_ALL, "empty FX/OTHER/USER categories were not skipped");
    CHECK(category_guess("deep bass") == CAT_BASS && category_guess("STRINGS 1") == CAT_PAD &&
          category_guess("unknown") == CAT_OTHER, "legacy names not classified");

    bank_pack(&cur, rec);
    memcpy(rec + 4, "Unknown", 8);
    CHECK(!bank_write(20, rec), "could not create uncategorized legacy patch");
    set.category = CAT_OTHER;
    patch_select(NFACT + 20);
    run(80);
    CHECK(cur_category == CAT_OTHER, "legacy unknown patch was not OTHER");
    tap(B_GLO);
    turn(EN_SELECT, 3);
    turn(EN_K3, -3);                   /* accelerate 9 steps: OTHER -> BASS */
    CHECK(cur_category == CAT_BASS && cur_dirty, "manual category could not be edited");
    tap(B_SAVE);
    CHECK(bank_category[20] == CAT_BASS && bank_rec(20)[3] == CAT_MARK, "saved category missing");
    power_on();
    CHECK(cur_index == NFACT + 20 && cur_category == CAT_BASS && set.drone_replace == 1 && !synth.hold,
          "restart lost category/mode or resumed a drone");
    turn(EN_ALGO, -1);                 /* USER includes every stored patch */
    CHECK(set.category == CAT_USER && cur_index == NFACT + 20, "USER filter did not keep its patch");
    midi(0xC0, 2, 0);
    run(100);
    CHECK(cur_index == 2 && set.category == CAT_ALL, "MIDI program change was trapped by a category");
    set.category = CAT_FX;             /* the last sound in a filter was removed by a manager */
    turn(EN_PRESET, 1);
    CHECK(set.category == CAT_ALL, "empty category did not recover");
}

int main(void)
{
    static const char sy1[] = "Test Patch 7\ncolor=red\nver=112\n0,3\n14,2\n19,77\n9,-12\n38,1\n86,45057\n87,44\n";
    uint8_t frame[SY1_FRAME_MAX], ask[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x03, 0xF7}, wipe[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x02, 0xF7};
    s1_patch p;
    uint32_t n, i;
    int got;

    memset(flash, 0xFF, sizeof flash);
    power_on();
    CHECK(cur_index == 0 && !strcmp(cur.name, "Saw Bass"), "starts on '%s' (%u)", cur.name, (unsigned)cur_index);
    CHECK(strstr(screen, "Saw Bass|") && strstr(screen, "PLAY|") && strstr(screen, "FREQ|"), "screen: %s", screen);

    /* a key plays and lets go; the bass is mono */
    keys_held = 1u << 7;
    run(200);
    CHECK(s1_active_voices(&synth) == 1 && peak > 100000.0f, "key: %d voices, peak %.0f", s1_active_voices(&synth), (double)peak);
    CHECK(peak < 0.75f * 8388607.0f, "one note reaches the output's knee: peak %.0f", (double)peak);
    keys_held = 0;
    run(1500);
    CHECK(s1_active_voices(&synth) == 0, "released key left %d voices", s1_active_voices(&synth));

    /* every built-in patch sounds, none clips on a three-note chord */
    for (i = 0; i < NFACT; i++) {
        patch_select(i);
        run(50);
        keys_held = 1u << 7 | 1u << 11 | 1u << 14;
        run(600);
        CHECK(peak > 60000.0f, "built-in %u (%s) is silent: peak %.0f", (unsigned)i, cur.name, (double)peak);
        CHECK(peak < 8388607.0f, "built-in %u (%s) passes full scale", (unsigned)i, cur.name);
        printf("  %-10s chord peak %.2f of full scale\n", cur.name, (double)(peak / 8388607.0f));
        keys_held = 0;
        run(300);
        midi(0xB0, 120, 0);              /* all sound off */
        run(50);
    }

    /* the patch knob, pages, rows and editing */
    patch_select(0);
    turn(EN_PRESET, 1);
    CHECK(cur_index == 1 && !strcmp(cur.name, "Warm Pad"), "PRESET +1 gives %u", (unsigned)cur_index);
    turn(EN_PRESET, -2);
    CHECK(cur_index == NFACT - 1u, "PRESET wraps to %u", (unsigned)cur_index);
    run(1400);                           /* (the patch's name leaves the header) */
    tap(B_SEL);
    CHECK(ui.page == 2 && strstr(screen, "FILTER|"), "SEL: page %u", (unsigned)ui.page);
    got = cur.values[S1P_FILTER_RES];
    turn(EN_K3, 2);                      /* a fast turn: 2 detents in a frame = 4 steps */
    CHECK(cur.values[S1P_FILTER_RES] == got + 4 && cur_dirty, "K3 on FILTER: %d -> %d", got, (int)cur.values[S1P_FILTER_RES]);
    CHECK(s1_get_param(&synth, S1P_FILTER_RES) == got + 4, "the engine did not get the edit");
    CHECK(strstr(screen, "EDITED|") != 0, "no EDITED mark");
    CHECK(leds_btn >> B_SAVE & 1u, "SAVE not lit for an edited patch");
    turn(EN_K1, 1);
    CHECK(cur.values[S1P_FILTER_TYPE] == 2 && strstr(screen, "HP12|"), "filter type: %d", (int)cur.values[S1P_FILTER_TYPE]);
    tap(B_SEL);
    CHECK(ui.row == 1, "SEL again: row %u", (unsigned)ui.row);
    tap(B_REC);
    CHECK(!cur_dirty && cur.values[S1P_FILTER_RES] == got, "REC did not revert");

    /* MIDI in: notes, a program change, the clock's tempo */
    patch_select(1);
    run(50);
    midi(0x90, 60, 100);
    midi(0x90, 64, 100);
    run(100);
    CHECK(s1_active_voices(&synth) >= 2, "MIDI notes: %d voices", s1_active_voices(&synth));
    midi(0x80, 60, 0);
    midi(0x80, 64, 0);
    midi(0xC0, 3, 0);
    run(100);
    CHECK(cur_index == 3, "program change 3 gives %u", (unsigned)cur_index);
    for (i = 0; i < 60; i++) {           /* 140 bpm: a tick every 17.857 ms */
        midi_q[midi_n++ & 63u] = 0x0F | 0xF8u << 8;
        run(i % 7 == 6 ? 17 : 18);
    }
    CHECK(ext_tempo >= 137 && ext_tempo <= 143, "MIDI clock at 140 read as %u", (unsigned)ext_tempo);

    /* a Synth1 patch over SysEx into slot 5; the count; it plays; out-of-range and legacy handled by the parser */
    CHECK(s1_patch_parse(&p, sy1, sizeof sy1 - 1) == 7, "parse");
    n = sy1_frame(&p, 4, frame);
    CHECK(n <= 640u + 2u, "frame of %u bytes is too long for the platform's SysEx", (unsigned)n);
    send_sysex(frame, n);
    CHECK(sysex_n == 10 && sysex_last[5] == 0x41 && sysex_last[6] == 4 && sysex_last[8] == 0, "write not acknowledged");
    CHECK(bank_count == 5, "bank count %u after slot 5", (unsigned)bank_count);
    send_sysex(ask, sizeof ask);
    CHECK(sysex_last[5] == 0x43 && sysex_last[6] == 5 && (sysex_last[8] | sysex_last[9] << 7) == 1024, "count answer");
    turn(EN_PRESET, -1);                 /* from built-in 4 down ... */
    patch_select(NFACT + 4);
    run(1400);                           /* (the name of the patch before leaves the header) */
    CHECK(!strcmp(cur.name, "Test Patch 7"), "slot 5 is '%s'", cur.name);
    CHECK(cur.values[0] == 3 && cur.values[14] == 2 && cur.values[19] == 77 && cur.values[9] == -12 &&
          cur.values[86] == 45057 && cur.values[87] == 44, "slot 5's values did not survive");
    CHECK(strstr(screen, "005|"), "slot 5 on the screen: %s", screen);
    turn(EN_PRESET, 1);                  /* the list skips the empty slots and wraps to the first */
    CHECK(cur_index == 0, "after the last patch: %u", (unsigned)cur_index);
    turn(EN_PRESET, -1);
    CHECK(cur_index == NFACT + 4, "before the first patch: %u", (unsigned)cur_index);
    keys_held = 1u << 12;
    run(300);
    CHECK(peak > 60000.0f, "the received patch is silent");
    keys_held = 0;

    /* a second patch into a used sector (the sector is rewritten, slot 5 kept); overwrite slot 5 */
    strcpy(p.name, "Second");
    send_sysex(frame, sy1_frame(&p, 6, frame));
    CHECK(bank_count == 7 && bank_used(4) && bank_used(6) && !bank_used(5), "after slot 7: count %u", (unsigned)bank_count);
    strcpy(p.name, "Replaced");
    p.values[19] = 99;
    send_sysex(frame, sy1_frame(&p, 4, frame));
    CHECK(!strcmp(cur.name, "Replaced") && cur.values[19] == 99, "the playing patch was not replaced: '%s'", cur.name);
    bank_load(6, &p);
    CHECK(!strcmp(p.name, "Second"), "slot 7 lost in the sector rewrite: '%s'", p.name);
    frame[8] ^= 0x20;                    /* a damaged frame is refused, nothing written */
    send_sysex(frame, sy1_frame(&p, 9, frame) - 9);
    CHECK(sysex_last[5] == 0x41 && sysex_last[8] != 0 && !bank_used(9), "a short frame was stored");

    /* a slot's name; a slot emptied (the others in its sector kept) */
    {
        uint8_t name4[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x04, 4, 0, 0xF7}, name5[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x04, 5, 0, 0xF7};
        uint8_t del6[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x05, 6, 0, 0xF7};
        send_sysex(name4, sizeof name4);
        CHECK(sysex_last[5] == 0x44 && sysex_last[6] == 4 && sysex_last[8] == 1 && !memcmp(sysex_last + 9, "Replaced", 8) &&
              sysex_last[17] == 0xF7, "slot 5's name");
        send_sysex(name5, sizeof name5);
        CHECK(sysex_last[5] == 0x44 && sysex_last[8] == 0 && sysex_last[9] == 0xF7, "an empty slot's name");
        send_sysex(del6, sizeof del6);
        CHECK(sysex_last[5] == 0x45 && sysex_last[8] == 0 && !bank_used(6) && bank_used(4) && bank_count == 5, "slot 7 emptied");
        strcpy(p.name, "Second");
        send_sysex(frame, sy1_frame(&p, 6, frame));
        CHECK(bank_count == 7, "slot 7 filled again");
    }

    /* save: an edited built-in patch goes to the first free slot; it is all there after a restart */
    patch_select(2);
    tap(B_ENV);
    turn(EN_K4, 3);
    CHECK(cur_dirty, "edit before save");
    got = cur.values[S1P_AMP_RELEASE];
    tap(B_OCTUP);
    tap(B_SAVE);
    CHECK(!cur_dirty && cur_index == NFACT + 0 && bank_used(0), "save went to index %u", (unsigned)cur_index);
    memset(&cur, 0, sizeof cur);
    power_on();
    CHECK(cur_index == NFACT && !strcmp(cur.name, "Pulse Lead") && cur.values[S1P_AMP_RELEASE] == got,
          "after the restart: %u '%s'", (unsigned)cur_index, cur.name);
    CHECK(set.octave == 1 && bank_count == 7, "settings after the restart: octave %d, count %u", set.octave, (unsigned)bank_count);

    /* overload sheds a voice; erase all */
    app_overload();
    run(50);
    CHECK(set.voices == MAX_VOICES_DEFAULT - 1 && synth.cfg.max_voices == MAX_VOICES_DEFAULT - 1, "overload: %u voices", set.voices);
    send_sysex(wipe, sizeof wipe);
    CHECK(bank_count == 0 && cur_index == 0, "erase: count %u, patch %u", (unsigned)bank_count, (unsigned)cur_index);
    for (n = 0, i = BANK_BASE; i < BANK_BASE + BANK_SLOTS * BANK_REC; i++)
        n += flash[i] != 0xFF;
    CHECK(n == 0, "erase left %u bytes", (unsigned)n);

    test_live_controls();
    printf("app: %d checks, %d failed (%u screens drawn)\n", checks, fails, (unsigned)draws);
    return fails != 0;
}
