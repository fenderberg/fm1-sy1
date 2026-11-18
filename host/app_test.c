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
static uint8_t flash[0x100000], store[OBJ_NOBJ][64], sysex_last[256];
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
/* the second core, as fm1-base's simulator has it: a job runs at once (the measurement build's part B) */
void plat_play_red(int red) { (void)red; }
int plat_cpu2_set(int on) { (void)on; return 0; }
int plat_cpu2_on(void) { return 1; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { fn(arg); return 1; }
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }

typedef struct { int h; } felucca_font_t;
static const felucca_font_t FONT_T = {10}, FONT_XS = {12}, FONT_S = {16}, FONT_M = {24};
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu
static char screen[4096];                /* every text drawn in the last frame, one after another */
static char screen_zones[5][768], drawing_text[768];
static uint32_t draws;
static uint32_t zone_draws[5];
static uint16_t drawing_bg, zone_bg[5];
void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg) { (void)w; (void)h; drawing_bg = bg; drawing_text[0] = 0; }
static void cv_blit(uint32_t x, uint32_t y)
{
    const uint32_t zone = y == 0 ? 0 : y == 40 ? 1 : y == 56 ? 2 : y == 136 ? 3 : 4;
    uint32_t i;
    (void)x;
    strcpy(screen_zones[zone], drawing_text);
    zone_bg[zone] = drawing_bg;
    zone_draws[zone]++;
    screen[0] = 0;
    for (i = 0; i < 5; i++) strcat(screen, screen_zones[i]);
    draws++;
}
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) { (void)x0; (void)y0; (void)x1; (void)y1; (void)c; }
static void record_text(const char *s)
{
    if (strlen(drawing_text) + strlen(s) + 2 < sizeof drawing_text) {
        strcat(drawing_text, s);
        strcat(drawing_text, "|");
    }
}
#define SY1_TEST_PIXEL_TEXT(s) record_text(s)
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    (void)y; (void)f; (void)c;
    record_text(s);
    return x + (int32_t)strlen(s) * 8;
}
static int32_t text_w(const felucca_font_t *f, const char *s) { (void)f; return (int32_t)strlen(s) * 8; }
#define ANN_NOW 1
#define ANN_KEEP 2
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
    turn(EN_SELECT, -10);              /* GLOBAL / PART */
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
    CHECK(factory500_has(cur_index) && cur_category == CAT_PAD, "PAD browsing missed factory pads");
    turn(EN_PRESET, -1);
    CHECK(cur_index == 6, "PAD browsing backwards left category");
    turn(EN_ALGO, 4);                   /* LEAD, KEYS, PLUCK, ARP */
    CHECK(set.category == CAT_ARP && cur_index == 7, "category traversal gave %u patch %u", set.category, cur_index);
    turn(EN_ALGO, 1);
    CHECK(set.category == CAT_FX, "factory FX category was skipped");
    turn(EN_ALGO,1);CHECK(set.category == CAT_OTHER,"factory Other category was skipped");
    turn(EN_ALGO,1);CHECK(set.category == CAT_FACTORY,"empty USER was not skipped for FACTORY");
    turn(EN_ALGO,1);CHECK(set.category == CAT_ALL,"FACTORY did not wrap to ALL");
    CHECK(category_guess("deep bass") == CAT_BASS && category_guess("STRINGS 1") == CAT_PAD &&
          category_guess("unknown") == CAT_OTHER, "legacy names not classified");

    bank_pack(&cur, rec);
    memcpy(rec + 4, "Unknown", 8);
    CHECK(!bank_write(20, rec), "could not create uncategorized legacy patch");
    set.category = CAT_OTHER;
    patch_select(NFACT + 20);
    run(80);
    CHECK(cur_category == CAT_OTHER, "legacy unknown patch was not OTHER");
    tap(B_HOME);
    turn(EN_SELECT, -10);              /* pages now remember their last row */
    turn(EN_SELECT, 4);                /* PLAY / SPREAD: the patch's category on KNOB4 */
    turn(EN_K4, -3);                   /* accelerate 9 steps: OTHER -> BASS */
    CHECK(cur_category == CAT_BASS && cur_dirty, "manual category could not be edited");
    tap(B_SAVE);
    CHECK(bank_category[20] == CAT_BASS && bank_rec(20)[3] == CAT_MARK, "saved category missing");
    power_on();
    CHECK(cur_index == NFACT + 20 && cur_category == CAT_BASS && set.drone_replace == 1 && !synth.hold,
          "restart lost category/mode or resumed a drone");
    turn(EN_ALGO, -2);                 /* from ALL, past FACTORY to USER */
    CHECK(set.category == CAT_USER && cur_index == NFACT + 20, "USER filter did not keep its patch");
    midi(0xC0, 2, 0);
    run(100);
    CHECK(cur_index == 2 && set.category == CAT_ALL, "MIDI program change was trapped by a category");
    bank_write(20,0);
    set.category = CAT_USER;           /* the last user sound was removed by a manager */
    turn(EN_PRESET, 1);
    CHECK(set.category == CAT_ALL, "empty category did not recover");
}

static void test_nord_ux(void)
{
    uint32_t n, i, page, row, mode;
    int32_t base;
    uint8_t frame[SY1_FRAME_MAX];
    s1_patch incoming;
    char text[40];
    const char *unit;
    memset(flash, 0xFF, sizeof flash);
    memset(store_len, 0, sizeof store_len);
    cpu_sheds = cpu_high = 0;
    power_on();
    tap(B_SEL);
    base = cur.values[S1P_FILTER_FREQ];
    turn(EN_K2, 1);
    CHECK(cur_dirty && ui.ux_focus == 1, "cutoff edit has no dirty/focus feedback");
    turn(EN_K2, -1);
    CHECK(!cur_dirty && !(leds_btn & (1u << B_SAVE)), "returning to saved cutoff left EDIT/SAVE lit");
    tap(B_SEL);                        /* remember FILTER row 2 */
    tap(B_ENV);
    tap(B_SEL);
    CHECK(ui.row == 1 && ui.ux_focus == -1, "return to FILTER lost its row or retained encoder focus");
    tap(B_HOME);
    turn(EN_SELECT, -10);
    turn(EN_SELECT, 4);
    turn(EN_K4, 1);
    CHECK(cur_dirty, "category edit was not dirty");
    turn(EN_K4, -1);
    CHECK(!cur_dirty, "returning to saved category left EDIT lit");
    tap(B_GLO);
    turn(EN_SELECT, 10);               /* GLOBAL / DIAGNOSTIC, the last row */
    incoming = cur;
    for (i = 0; i < 4; i++) turn(EN_K1 + (int)i, 1);
    CHECK(ui.row == 3 && !memcmp(&cur, &incoming, sizeof cur) && !cur_dirty,
          "diagnostics changed patch values");

    tap(B_SEL);
    turn(EN_SELECT, -10);
    turn(EN_K2, 1);
    tap(B_SAVE);
    CHECK(cur_index == NFACT && !cur_dirty && saved_values[S1P_FILTER_FREQ] == base + 1,
          "SAVE did not refresh the stored reference");
    CHECK(strstr(screen, "Saw Bass|") && strstr(screen, "SAVED 001|"), "save hid patch identity: %s", screen);
    turn(EN_K2, 1);                    /* unsaved edit, then overwrite this slot from the manager */
    incoming = cur;
    incoming.values[S1P_FILTER_FREQ] = base + 4;
    n = sy1_frame(&incoming, 0, frame);
    send_sysex(frame, n);
    CHECK(cur.values[S1P_FILTER_FREQ] == base + 2 && cur_dirty && saved_values[S1P_FILTER_FREQ] == base + 4,
          "incoming patch discarded edits or kept the old reference");
    turn(EN_K2, 1); turn(EN_K2, 1);
    CHECK(!cur_dirty, "matching the newly imported patch did not clear EDIT");
    incoming.values[S1P_FILTER_FREQ] = 42;
    n = sy1_frame(&incoming, 0, frame);
    send_sysex(frame, n);
    CHECK(cur.values[S1P_FILTER_FREQ] == 42 && saved_values[S1P_FILTER_FREQ] == 42 && !cur_dirty,
          "clean imported patch was not reloaded");

    app_say("CPU FULL", "FEWER VOICES");
    app_say("LFO1", "ON");
    app_say("SAVED", "Saw Bass");
    run(30);
    CHECK(!strcmp(ui.msg[0], "CPU FULL"), "routine feedback erased the overload warning");
    run(1400);
    app_say("SAVED", "Saw Bass");
    CHECK(!strcmp(ui.msg[0], "SAVED"), "expired warning prevented new feedback");

    cur.values[S1P_LFO1_TEMPO_SYNC] = 1;
    ux_value(S1P_LFO1_SPEED, 127, text, &unit);
    CHECK(!strcmp(text,"1/24") && !strcmp(unit,"beat"), "fastest synced LFO displayed %s %s", text, unit);
    ux_value(S1P_ARP_BEAT, 18, text, &unit);
    CHECK(!strcmp(text,"1/24"), "small beat fraction rounded to zero");
    ux_value(S1P_OSC_PULSE_WIDTH, 127, text, &unit);
    CHECK(!strcmp(text,"50"), "pulse width disagrees with engine duty: %s", text);
    ux_value(S1P_CHORUS_RATE, 0, text, &unit);
    CHECK(!strcmp(text,"0.01") && !strcmp(unit,"Hz"), "slow chorus rate rounded to zero: %s",text);
    ux_decimal(0.9999f,text);
    CHECK(!strcmp(text,"1"), "decimal rounding around 1 is invalid: %s",text);

    patch_select(1);
    cell_edit(S1P_AMP_RELEASE, -127);   /* short release: newest voice ends while the old one still sounds */
    tap(B_ENV);
    turn(EN_SELECT,-10);
    keys_held = 1u << 7;
    run(150);
    CHECK(ux_live.active && ux_live.note == 60 && !(ux_live.sequence & 1u), "no consistent envelope snapshot");
    keys_held |= 1u << 11;
    run(150);
    CHECK(ux_live.note == 64, "snapshot did not follow the newest note");
    keys_held = 1u << 7;
    run(700);
    CHECK(!ux_live.active && s1_active_voices(&synth) > 0, "envelope cursor jumped to an older held note");
    tap(B_PLAY);
    keys_held = 0;
    run(150);
    CHECK(!ux_live.active && !ux_motion(), "panic left envelope motion active");

    tap(B_LFO);
    turn(EN_SELECT,-10);
    run(150);
    const uint32_t graph_before = zone_draws[2], head_before = zone_draws[0], cells_before = zone_draws[3];
    run(600);
    CHECK(zone_draws[2] > graph_before && zone_draws[0] == head_before && zone_draws[3] == cells_before,
          "LFO animation did not isolate its redraw to the graph");
    turn(EN_K1,-1);
    run(150);
    n = zone_draws[2];
    run(600);
    CHECK(zone_draws[2] == n, "disabled LFO kept repainting its graph");
    ux_audio.sequence |= 1u;
    CHECK(!ux_read_live(), "reader accepted an incomplete audio snapshot");
    ux_audio.sequence++;

    /* Exercise all 32 subpages with genuine endpoint values, including every enum/table index. */
    for (mode = 0; mode < 3; mode++) {
        for (i = 0; i < S1_NPARAMS; i++) cur.values[i] = mode == 0 ? s1_param_def[i] :
            mode == 1 ? s1_param_min[i] : s1_param_max[i];
        to_engine();
        for (page = 0; page < NPAGES; page++) for (row = 0; row < PAGES[page].nrows; row++) {
            ui.page = page; ui.row = row; ui.redraw = UX_ALL;
            run(32);
            CHECK(screen_zones[1][0] && screen_zones[3][0], "empty UI on page %u row %u endpoint %u", page,row,mode);
        }
    }
    patch_select(0);
}

static void test_display_styles(void)
{
    uint32_t page, row, i, mode, previous[5];
    settings_t old;
    s1_patch before;
    CHECK(sizeof(settings_t)==24 && (uint32_t)((uint8_t *)&set.display-(uint8_t *)&set)==SET_LEGACY_BYTES &&
          (uint32_t)((uint8_t *)&set.ch_l-(uint8_t *)&set)==SET_V2_BYTES,
          "settings no longer append after the legacy prefix");
    patch_select(1);
    set.octave=-2;set.tempo=137;set.voices=6;set.category=CAT_PAD;set.drone_replace=1;
    old=set;
    memcpy(store[OBJ_SETTINGS],&set,SET_LEGACY_BYTES);store_len[OBJ_SETTINGS]=SET_LEGACY_BYTES;
    set.display=DISPLAY_GREEN;
    power_on();
    CHECK(set.display==DISPLAY_NORD && set.patch==old.patch && set.octave==old.octave &&
          set.tempo==old.tempo && set.voices==old.voices && set.category==old.category &&
          set.drone_replace==old.drone_replace,"legacy settings were lost or inherited a stale display");
    CHECK(!strcmp(cur.name,"Warm Pad"),"migration lost selected patch");
    if(bank_used(0)) {
        s1_patch stored_patch;
        bank_load(0,&stored_patch);patch_select(NFACT);
        memcpy(store[OBJ_SETTINGS],&set,SET_LEGACY_BYTES);store_len[OBJ_SETTINGS]=SET_LEGACY_BYTES;
        power_on();
        CHECK(cur_index==NFACT && bank_used(0) && !memcmp(cur.values,stored_patch.values,sizeof cur.values),
              "legacy migration lost the selected imported patch");
        patch_select(1);
    }
    tap(B_GLO);turn(EN_SELECT,-10);turn(EN_SELECT,2);   /* GLOBAL / DISPLAY */
    before=cur;
    for(i=0;i<5;i++) previous[i]=zone_draws[i];
    const uint32_t events_before=ev_w;
    cell_edit(X_DISPLAY,1);
    CHECK(set.display==DISPLAY_GREEN && set_dirty && !cur_dirty && !memcmp(&before,&cur,sizeof cur) && ev_w==events_before,
          "display choice edited the patch or queued an audio event");
    run(32);
    for(i=0;i<5;i++) CHECK(zone_draws[i]>previous[i],"display switch left stale colors in zone %u",i);
    CHECK(strstr(screen,"GREEN LED|") && strstr(screen,"STYLE|"),"display selection lacks feedback: %s",screen);
    CHECK(zone_bg[0]==UX_BG && zone_bg[1]==UX_RED && UX_TEXT!=RGB(244,239,223),"green palette not applied");
    cell_edit(X_DISPLAY,-100);CHECK(set.display==DISPLAY_NORD,"display did not clamp at Nord");
    cell_edit(X_DISPLAY,1);
    CHECK(ux_display_width(&FONT_M,"123")==43 && ux_display_width(&FONT_M,"0.01")==47,
          "LED numeral widths disagree with their cells");
    CHECK(ux_led_number(&FONT_M,"1/24") && !ux_led_number(&FONT_M,"LP24"),"segment selection loses fractions or labels");
    CHECK(ux_pixel_row('a',0)==ux_pixel_row('A',0),"pixel display lost lowercase patch names");
    run(5500);power_on();
    CHECK(set.display==DISPLAY_GREEN && store_len[OBJ_SETTINGS]==sizeof set && set.tempo==137 && set.octave==-2,
          "green selection or existing settings did not survive restart");
    for(mode=0;mode<3;mode++) {
        for(i=0;i<S1_NPARAMS;i++) cur.values[i]=mode==0?s1_param_def[i]:mode==1?s1_param_min[i]:s1_param_max[i];
        to_engine();
        for(page=0;page<NPAGES;page++)for(row=0;row<PAGES[page].nrows;row++) {
            ui.page=page;ui.row=row;ui.redraw=UX_ALL;run(32);
            CHECK(screen_zones[1][0] && screen_zones[3][0],"empty green page %u row %u endpoint %u",page,row,mode);
        }
    }
    ui.page=7;ui.row=2;ui.redraw=UX_ALL;run(32);
    for(i=0;i<5;i++) previous[i]=zone_draws[i];
    before=cur;
    const uint32_t lcd_events=ev_w;
    cell_edit(X_DISPLAY,1);run(32);
    CHECK(set.display==DISPLAY_LCD && ev_w==lcd_events && !memcmp(&before,&cur,sizeof cur),
          "LCD style changed sound or failed to select");
    for(i=0;i<5;i++) CHECK(zone_draws[i]>previous[i],"LCD left stale LED pixels in zone %u",i);
    CHECK(strstr(screen,"GREEN LCD|") && UX_TEXT!=UX_BG && zone_bg[0]==RGB(166,193,89),
          "LCD title or green background missing");
    CHECK(((UX_TEXT>>11)&31)<8 && ((UX_BG>>5)&63)>40 && ((UX_MUTED>>5)&63)<25,
          "LCD text lacks dark-on-light contrast");
    cell_edit(X_DISPLAY,100);CHECK(set.display==DISPLAY_LCD,"display did not clamp at LCD");
    run(5500);power_on();
    CHECK(set.display==DISPLAY_LCD && set.tempo==137 && set.octave==-2,"LCD choice did not survive restart");
    for(mode=0;mode<3;mode++) {
        for(i=0;i<S1_NPARAMS;i++) cur.values[i]=mode==0?s1_param_def[i]:mode==1?s1_param_min[i]:s1_param_max[i];
        to_engine();
        for(page=0;page<NPAGES;page++)for(row=0;row<PAGES[page].nrows;row++) {
            ui.page=page;ui.row=row;ui.redraw=UX_ALL;run(32);
            CHECK(screen_zones[1][0] && screen_zones[3][0],"empty LCD page %u row %u endpoint %u",page,row,mode);
        }
    }
    cell_edit(X_DISPLAY,-1);run(32);
    CHECK(set.display==DISPLAY_GREEN && zone_bg[0]==RGB(1,7,3),"LCD-to-LED switch left light pixels");
    cell_edit(X_DISPLAY,-1);run(32);
    CHECK(set.display==DISPLAY_NORD && zone_bg[0]==RGB(17,19,20),"switch back left green colors");
    set.display=255;plat_store_save(OBJ_SETTINGS,&set,sizeof set);power_on();
    CHECK(set.display==DISPLAY_NORD && set.tempo==137,"invalid display reset unrelated settings");
}

static void test_global_page(void)    /* GLOBAL holds nothing that is stored in the patch */
{
    uint32_t r, c, pg;
    for (pg = 0; pg < NPAGES && PAGES[pg].button != B_GLO; pg++) {}
    CHECK(pg == NPAGES - 1u && !strcmp(PAGES[pg].title, "GLOBAL"), "GLO is not the GLOBAL page");
    for (r = 0; r < PAGES[pg].nrows; r++)
        for (c = 0; c < 4u; c++)
            CHECK(PAGES[pg].rows[r][c] >= S1_NPARAMS && PAGES[pg].rows[r][c] != X_CATEGORY,
                  "GLOBAL row %u knob %u is a patch setting", (unsigned)r + 1u, (unsigned)c + 1u);
    CHECK(PAGES[0].nrows == 5u && PAGES[0].rows[4][3] == X_CATEGORY && PAGES[0].rows[2][0] == S1P_PLAY_MODE,
          "PLAY does not hold the voice settings and the category");
}

static void test_randomizer(void)
{
    memset(flash, 0xFF, sizeof flash);
    memset(store_len, 0, sizeof store_len);
    power_on();
    turn(EN_K1, -2);                     /* an unsaved cutoff edit on HOME */
    const s1_patch start = cur;
    tap(B_GLO);
    turn(EN_K3, 2);
    const s1_patch second = cur;
    CHECK(cell_value(X_RANDOM) == 2 && cur_dirty && memcmp(cur.values, start.values, sizeof cur.values),
          "GLO / PART knob 3 did not make a variation");
    CHECK(!memcmp(synth.base, cur.values, sizeof cur.values) && bank_count == 0,
          "variation did not reach the engine or wrote a user patch");
    turn(EN_K3, 20);
    CHECK(cell_value(X_RANDOM) == VARIATION_COUNT, "randomizer upper bound");
    turn(EN_K3, -6);
    CHECK(!memcmp(cur.values, second.values, sizeof cur.values), "revisiting a variation changed it");
    turn(EN_K3, -20);
    CHECK(!memcmp(cur.values, start.values, sizeof cur.values) && cur_dirty, "BASE lost an unsaved edit");
    turn(EN_K3, 2);
    mode_set(1);
    focus_set(1);
    CHECK(cell_value(X_RANDOM) == 0, "part R inherited L's variation");
    tap(B_GLO);
    turn(EN_K3, 1);
    focus_set(0);
    CHECK(cell_value(X_RANDOM) == 2 && !memcmp(cur.values, second.values, sizeof cur.values), "part R changed L");
    tap(B_SEL);
    turn(EN_K2, -1);
    const s1_patch edited = cur;
    CHECK(cell_value(X_RANDOM) == 0, "editing a variation did not establish a new base");
    tap(B_GLO);
    turn(EN_K3, 1);
    turn(EN_K3, -1);
    CHECK(!memcmp(cur.values, edited.values, sizeof cur.values), "randomizer lost a subsequent edit");
    turn(EN_K3, 1);
    const s1_patch keep = cur;
    tap(B_SAVE);
    CHECK(cell_value(X_RANDOM) == 0 && !cur_dirty && bank_used(0), "SAVE did not commit the variant");
    turn(EN_K3, 2);
    tap(B_REC);
    CHECK(!cur_dirty && cell_value(X_RANDOM) == 0 && !memcmp(cur.values, keep.values, sizeof cur.values),
          "REC did not restore the saved variant");
}

static void test_factory500(void)
{
    uint32_t i,j,counts[CAT_COUNT]={0},wr;
    uint8_t rec[BANK_REC],request[]={0xF0,0x7D,0x53,0x59,0x31,0x06,0,0,0,0xF7};
    s1_patch patch,copy;
    uint32_t audible=0;float factory_peak=0.0f;
    CHECK(FACTORY500_COUNT==256 && FACTORY500_BASE==1032,"factory IDs shifted existing programs");
    for(i=0;i<FACTORY500_COUNT;i++) {
        bank_decode(FACTORY500[i],&patch);
        CHECK(patch.name[0] && FACTORY500_BANK[i]<sizeof FACTORY500_SOURCE/sizeof FACTORY500_SOURCE[0],
              "factory %u has no name or source",i);
        int valid=1;for(j=0;j<S1_NPARAMS;j++)valid&=patch.values[j]>=s1_param_min[j] && patch.values[j]<=s1_param_max[j];
        CHECK(valid,"factory %u has invalid parameter values",i);
        bank_pack(&patch,rec);
        CHECK(!memcmp(rec+28,FACTORY500[i]+28,198),"factory %u changed on C decode/encode",i);
        s1_set_patch(&synth,patch.values);s1_set_tempo(&synth,120.0f);
        s1_note_on(&synth,48,100);s1_note_on(&synth,60,100);s1_note_on(&synth,64,100);s1_note_on(&synth,67,100);
        float left[APP_BLOCK],right[APP_BLOCK],sound_peak=0.0f;int finite=1;
        for(uint32_t block=0;block<345;block++) {
            s1_render(&synth,left,right,APP_BLOCK);
            for(uint32_t sample=0;sample<APP_BLOCK;sample++) {
                finite&=isfinite(left[sample]) && isfinite(right[sample]);
                const float level=fmaxf(fabsf(left[sample]),fabsf(right[sample]));
                if(level>sound_peak)sound_peak=level;
            }
        }
        CHECK(finite,"factory %u generated non-finite audio",i);
        if(sound_peak>0.00001f)audible++;
        if(sound_peak>factory_peak)factory_peak=sound_peak;
        counts[FACTORY500[i][2]]++;
    }
    printf("  factory render: %u / %u audible, all finite; maximum pre-limiter peak %.2f\n",audible,(unsigned)FACTORY500_COUNT,(double)factory_peak);
    CHECK(audible==FACTORY500_COUNT,"factory selection contains %u inaudible sounds",(unsigned)FACTORY500_COUNT-audible);
    CHECK(counts[CAT_BASS]==35 && counts[CAT_PAD]==35 && counts[CAT_LEAD]==34 && counts[CAT_KEYS]==34 &&
          counts[CAT_PLUCK]==34 && counts[CAT_ARP]==34 && counts[CAT_FX]==34 && counts[CAT_OTHER]==16,
          "factory category balance changed");
    for(i=0;i<FACTORY500_COUNT;i++)CHECK(FACTORY500[i][3]==CAT_MARK && FACTORY500[i][2]>=CAT_BASS && FACTORY500[i][2]<=CAT_OTHER,
                                         "factory %u has no explicit category",i);
    memset(flash,0xFF,sizeof flash);memset(store_len,0,sizeof store_len);power_on();
    CHECK(bank_count==0,"factory sounds consumed user storage");
    turn(EN_ALGO,-1);
    CHECK(set.category==CAT_FACTORY && cur_index==FACTORY500_BASE,"FACTORY is not one left from ALL");
    char number[8];index_text(cur_index,number);CHECK(!strcmp(number,"F001"),"bad factory label %s",number);
    turn(EN_PRESET,-1);CHECK(cur_index==FACTORY500_END-1,"factory backwards wrap failed");
    turn(EN_PRESET,1);CHECK(cur_index==FACTORY500_BASE,"factory forward wrap failed");
    set.category=CAT_ALL;patch_select(FACTORY500_END-1);turn(EN_PRESET,1);CHECK(cur_index==0,"ALL did not wrap past the last factory sound");
    set.category=CAT_FACTORY;patch_select(FACTORY500_BASE);copy=cur;
    cell_edit(S1P_FILTER_FREQ,cur.values[S1P_FILTER_FREQ]>60?-1:1);tap(B_SAVE);
    CHECK(cur_index==NFACT && bank_used(0) && set.category==CAT_USER,"factory edit did not save a user copy");
    patch_select(FACTORY500_BASE);CHECK(!memcmp(cur.values,copy.values,sizeof copy.values),"SAVE changed the immutable factory sound");
    set.category=CAT_FACTORY;patch_select(FACTORY500_BASE+17);run(5500);power_on();
    CHECK(cur_index==FACTORY500_BASE+17 && set.category==CAT_FACTORY,"factory selection did not survive restart");
    wr=ev_w;send_sysex(request,sizeof request);
    CHECK(ev_w==wr && sysex_last[5]==0x46 && sysex_n==202,"credits query changed sound or returned wrong size");
    for(i=0;i<96;i++)CHECK((sysex_last[9+2*i]<<4|sysex_last[10+2*i])==FACTORY500_CREDITS[i],"credit byte %u changed",i);
    const uint32_t end=sizeof FACTORY500_CREDITS;
    request[6]=end&127u;request[7]=(end>>7)&127u;request[8]=(end>>14)&127u;
    send_sysex(request,sizeof request);CHECK(sysex_n==10 && sysex_last[5]==0x46,"end of credits response was not empty");
    uint8_t erase[]={0xF0,0x7D,0x53,0x59,0x31,0x02,0xF7};send_sysex(erase,sizeof erase);
    CHECK(bank_count==0 && factory500_has(cur_index),"erasing user slots removed a factory sound");
    for(i=0;i<FACTORY500_COUNT;i++)CHECK(list_has(FACTORY500_BASE+i),"factory %u vanished after erase",i);
    /* A full user bank must not turn a factory SAVE into an out-of-range write. */
    bank_pack(&copy,rec);for(i=0;i<BANK_SLOTS;i++)memcpy(flash+BANK_BASE+i*BANK_REC,rec,BANK_REC);bank_scan();
    set.category=CAT_FACTORY;patch_select(FACTORY500_BASE);tap(B_SAVE);
    CHECK(factory500_has(cur_index) && bank_count==BANK_SLOTS && !strcmp(ui.msg[1],"BANK FULL"),"full user bank allowed factory overwrite");
    CHECK(bank_used(0) && bank_used(1023),"full-bank SAVE damaged user slots");
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
    CHECK(strstr(screen, "Saw Bass|") && strstr(screen, "PLAY / FILTER|") && strstr(screen, "CUTOFF|"), "screen: %s", screen);

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
    CHECK(cur_index == FACTORY500_END - 1u, "PRESET wraps to %u", (unsigned)cur_index);
    patch_select(7);
    run(1400);                           /* (the patch's name leaves the header) */
    tap(B_SEL);
    CHECK(ui.page == 2 && strstr(screen, "FILTER / MAIN|"), "SEL: page %u", (unsigned)ui.page);
    got = cur.values[S1P_FILTER_RES];
    turn(EN_K3, 2);                      /* a fast turn: 2 detents in a frame = 4 steps */
    CHECK(cur.values[S1P_FILTER_RES] == got + 4 && cur_dirty, "K3 on FILTER: %d -> %d", got, (int)cur.values[S1P_FILTER_RES]);
    CHECK(s1_get_param(&synth, S1P_FILTER_RES) == got + 4, "the engine did not get the edit");
    CHECK(strstr(screen, "EDIT|") != 0, "no EDIT mark");
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
    CHECK(sysex_last[5] == 0x43 && sysex_last[6] == 5 && (sysex_last[8] | sysex_last[9] << 7) == 1024 &&
          sysex_n == 12 && sysex_last[10] == 3u, "count answer, with the record read and the parts announced");
    {   /* the parts: part L to slot 5 by SysEx 09, read back by 08; an empty slot refused */
        uint8_t pick[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x09, 0, 4, 0, 0xF7}, bad[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x09, 0, 2, 0, 0xF7};
        uint8_t query[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x08, 0x7F, 0x7F, 0xF7};
        send_sysex(pick, sizeof pick);
        CHECK(sysex_n == 11 && sysex_last[5] == 0x49 && sysex_last[7] == 4 && sysex_last[9] == 0 && cur_index == NFACT + 4,
              "09 did not put part L on slot 5: %u", (unsigned)cur_index);
        send_sysex(query, sizeof query);
        CHECK(sysex_n == 16 && sysex_last[5] == 0x48 && sysex_last[6] == 0 && sysex_last[9] == 2 && sysex_last[10] == 3 &&
              (sysex_last[11] | sysex_last[12] << 7) == 5, "08 did not report the channels and part L's slot");
        send_sysex(bad, sizeof bad);
        CHECK(sysex_last[5] == 0x49 && sysex_last[9] == 1 && cur_index == NFACT + 4, "09 took an empty slot");
    }
    turn(EN_PRESET, -1);                 /* from built-in 4 down ... */
    patch_select(NFACT + 4);
    run(1400);                           /* (the name of the patch before leaves the header) */
    CHECK(!strcmp(cur.name, "Test Patch 7"), "slot 5 is '%s'", cur.name);
    CHECK(cur.values[0] == 3 && cur.values[14] == 2 && cur.values[19] == 77 && cur.values[9] == -12 &&
          cur.values[86] == 45057 && cur.values[87] == 44, "slot 5's values did not survive");
    CHECK(strstr(screen, "005|"), "slot 5 on the screen: %s", screen);
    turn(EN_PRESET, 1);                  /* the list skips the empty slots and wraps to the first */
    CHECK(cur_index == FACTORY500_BASE, "factory bank did not follow user sounds: %u", (unsigned)cur_index);
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
        {   /* a slot's whole record read back in two parts, byte for byte; an empty slot answers without one */
            uint8_t part0[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x07, 4, 0, 0, 0xF7}, part1[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x07, 4, 0, 1, 0xF7};
            uint8_t none[] = {0xF0, 0x7D, 0x53, 0x59, 0x31, 0x07, 5, 0, 0, 0xF7}, back[BANK_PAYLOAD];
            uint32_t got = 0, g, b, before = ev_w;
            send_sysex(part0, sizeof part0);
            CHECK(sysex_n == 10u + 17u * 8u + 1u && sysex_last[5] == 0x47 && sysex_last[6] == 4 && sysex_last[8] == 0 &&
                  sysex_last[9] == 1 && sysex_last[sysex_n - 1u] == 0xF7, "record part 0: %u bytes", (unsigned)sysex_n);
            for (g = 10u; g + 8u < sysex_n && got < BANK_PART; g += 8u)
                for (b = 0; b < 7u && got < BANK_PART; b++)
                    back[got++] = (uint8_t)(sysex_last[g + 1u + b] | ((sysex_last[g] >> b & 1u) << 7));
            send_sysex(part1, sizeof part1);
            CHECK(sysex_n == 10u + 16u * 8u + 1u && sysex_last[5] == 0x47 && sysex_last[8] == 1 && sysex_last[9] == 1,
                  "record part 1: %u bytes", (unsigned)sysex_n);
            for (g = 10u; g + 8u < sysex_n && got < BANK_PAYLOAD; g += 8u)
                for (b = 0; b < 7u && got < BANK_PAYLOAD; b++)
                    back[got++] = (uint8_t)(sysex_last[g + 1u + b] | ((sysex_last[g] >> b & 1u) << 7));
            CHECK(got == BANK_PAYLOAD && !memcmp(back, bank_rec(4), BANK_PAYLOAD), "slot 5's record did not read back whole");
            send_sysex(none, sizeof none);
            CHECK(sysex_n == 11 && sysex_last[5] == 0x47 && sysex_last[6] == 5 && sysex_last[9] == 0, "an empty slot's record");
            CHECK(ev_w == before, "reading a record disturbed the sound");
        }
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
    test_nord_ux();
    test_display_styles();
    test_global_page();
    test_randomizer();
    test_factory500();
    printf("app: %d checks, %d failed (%u screens drawn)\n", checks, fails, (unsigned)draws);
    return fails != 0;
}
