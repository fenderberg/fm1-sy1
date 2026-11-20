/* Experimental four-part mode. Included after the legacy patch/panel helpers.
 * Panel state is independent of standard/bitimbral. Only the audio interrupt
 * rebinds the shared FX arena; the main loop communicates through events. */
#pragma once

typedef struct {
    s1_patch cur;
    uint32_t index, page, row;
    int32_t saved_values[S1_NPARAMS];
    char saved_name[S1_NAME_LEN];
    uint8_t ux_rows[8], dirty, category, saved_category;
    uint8_t browse, voices, drone;
    int8_t octave;
} quad_panel_t;
static quad_panel_t quad_panel[NPARTS], legacy_panel;
static variation_t legacy_variation[2], quad_variation[NPARTS];
static uint8_t quad_initialized, legacy_focus;
static const char *const QUAD_LABEL[NPARTS] = {"1 ", "2 ", "3 ", "4 "};
static const char *const QUAD_NAME[NPARTS] = {"PART 1", "PART 2", "PART 3", "PART 4"};

static void quad_defaults(void)
{
    set.quad_enabled = set.quad_layout = set.quad_focus = 0;
    set.quad_version = 1;
    for (uint32_t p = 0; p < NPARTS; p++) {
        set.quad_ch[p] = (uint8_t)(p + 1u);
        set.quad[p] = (quad_part_t){(uint16_t)p, 0, 3, 0, CAT_ALL};
    }
}

static void panel_capture(quad_panel_t *dst)
{
    dst->cur = cur; dst->index = cur_index;
    dst->page = ui.page; dst->row = ui.row;
    dst->dirty = cur_dirty; dst->category = cur_category; dst->saved_category = saved_category;
    for (uint32_t i = 0; i < S1_NPARAMS; i++) dst->saved_values[i] = saved_values[i];
    for (uint32_t i = 0; i < S1_NAME_LEN; i++) dst->saved_name[i] = saved_name[i];
    for (uint32_t i = 0; i < 8u; i++) dst->ux_rows[i] = ui.ux_rows[i];
    dst->browse = set.category; dst->voices = set.voices;
    dst->octave = set.octave; dst->drone = set.drone_replace;
}

static void panel_restore(const quad_panel_t *src)
{
    cur = src->cur; cur_index = src->index;
    ui.page = src->page; ui.row = src->row;
    cur_dirty = src->dirty; cur_category = src->category; saved_category = src->saved_category;
    for (uint32_t i = 0; i < S1_NPARAMS; i++) saved_values[i] = src->saved_values[i];
    for (uint32_t i = 0; i < S1_NAME_LEN; i++) saved_name[i] = src->saved_name[i];
    for (uint32_t i = 0; i < 8u; i++) ui.ux_rows[i] = src->ux_rows[i];
    set.category = src->browse; set.voices = src->voices;
    set.octave = src->octave; set.drone_replace = src->drone;
    ui.ref_serial++; ui.redraw = UX_ALL;
    ux_live_reset(); ux_wanted_generation++; ux_refocus = 1;
}

static void quad_sync(void)
{
    if (!QUAD_ON) return;
    quad_part_t *q = &set.quad[cur_part];
    q->patch = (uint16_t)cur_index; q->voices = set.voices;
    q->octave = set.octave; q->drone = set.drone_replace; q->category = set.category;
    set.quad_focus = cur_part;
}

static int settings_save(void)
{
    quad_sync();
    settings_t persisted = set;
    if (QUAD_ON) {                      /* the original fields always describe legacy mode on disk */
        persisted.category = legacy_panel.browse; persisted.voices = legacy_panel.voices;
        persisted.octave = legacy_panel.octave; persisted.drone_replace = legacy_panel.drone;
    }
    return plat_store_save(OBJ_SETTINGS, &persisted, sizeof persisted);
}

static uint32_t all_voices(void)
{
    uint32_t n = 0;
    for (uint32_t p = 0; p < NPARTS; p++) n += (uint32_t)s1_active_voices(PART[p]);
    return n;
}

static uint32_t voice_budget(void)
{
    if (!QUAD_ON) return set.voices + (BI_ON ? set.voices_b : 0u);
    uint32_t n = 0;
    for (uint32_t p = 0; p < NPARTS; p++) n += p == cur_part ? set.voices : set.quad[p].voices;
    return n;
}

static void queue_mode_patches(void)
{
    for (uint32_t p = 0; p < (QUAD_ON ? NPARTS : 2u); p++) {
        const int32_t *values = p == cur_part ? cur.values : QUAD_ON ? quad_panel[p].cur.values : other.cur.values;
        patch_ready[p] = 0;
        for (uint32_t i = 0; i < S1_NPARAMS; i++) patch_next[p][i] = values[i];
        patch_ready[p] = 1;
        want_program[p] = -1;
    }
}

static void quad_focus(uint32_t part)
{
    if (!QUAD_ON || part >= NPARTS || part == cur_part) return;
    quad_sync(); panel_capture(&quad_panel[cur_part]);
    cur_part = (uint8_t)part;
    panel_restore(&quad_panel[part]);
    w_focus = cur_part;
    set.quad_focus = cur_part; set_dirty = 1;
    app_say(QUAD_NAME[part], cur.name);
}

static void quad_select(uint32_t part, uint32_t index)
{
    if (!QUAD_ON || part >= NPARTS) return;
    const uint32_t previous = cur_part;
    quad_focus(part); patch_select(index); quad_sync();
    panel_capture(&quad_panel[part]);
    quad_focus(previous);
}

static void quad_enter(void)
{
    if (QUAD_ON) return;
    legacy_focus = cur_part;
    panel_capture(&legacy_panel);
    for (uint32_t p = 0; p < 2u; p++) legacy_variation[p] = variations[p];
    set.quad_enabled = 1;
    const uint32_t focus = set.quad_focus;
    if (!quad_initialized) {
        for (uint32_t p = 0; p < NPARTS; p++) {
            const quad_part_t q = set.quad[p];
            cur_part = (uint8_t)p;
            set.voices = q.voices; set.octave = q.octave;
            set.category = q.category; set.drone_replace = q.drone;
            ui.page = ui.row = 0;
            for (uint32_t i = 0; i < 8; i++) ui.ux_rows[i] = 0;
            patch_select(q.patch);
            panel_capture(&quad_panel[p]);
        }
        quad_initialized = 1;
    } else {
        for (uint32_t p = 0; p < NPARTS; p++) variations[p] = quad_variation[p];
    }
    cur_part = (uint8_t)focus;
    panel_restore(&quad_panel[focus]);
    w_focus = cur_part;
    quad_sync(); queue_mode_patches();
}

static void quad_leave(void)
{
    quad_sync(); panel_capture(&quad_panel[cur_part]);
    for (uint32_t p = 0; p < NPARTS; p++) quad_variation[p] = variations[p];
    set.quad_enabled = 0;
    cur_part = legacy_focus;
    panel_restore(&legacy_panel);
    for (uint32_t p = 0; p < 2u; p++) variations[p] = legacy_variation[p];
    w_focus = cur_part;
}

static void quad_bank_refresh(int32_t slot)
{
    /* Called with the legacy panel active, after a successful librarian write.
     * Keep unsaved sound edits, but compare against the new saved record. */
    if (!quad_initialized) return;
    for (uint32_t p = 0; p < NPARTS; p++) {
        quad_panel_t *q = &quad_panel[p];
        if (!user_index(q->index) || (slot >= 0 && q->index != NFACT + (uint32_t)slot)) continue;
        s1_patch stored;
        if (slot < 0 || !list_has(q->index)) {
            q->index = 0; q->dirty = 0;
            fact_load(0, &stored);
        } else bank_load(q->index - NFACT, &stored);
        const uint8_t category = patch_category(q->index);
        if (!q->dirty) { q->cur = stored; q->category = category; }
        q->saved_category = category;
        q->dirty = q->category != category || !ux_equal(q->cur.name, stored.name);
        for (uint32_t i = 0; i < S1_NPARAMS; i++) {
            q->saved_values[i] = stored.values[i];
            if (q->cur.values[i] != stored.values[i]) q->dirty = 1;
        }
        for (uint32_t i = 0; i < S1_NAME_LEN; i++) q->saved_name[i] = stored.name[i];
        set.quad[p].patch = (uint16_t)q->index;
        quad_variation[p] = (variation_t){0};
    }
}

static void quad_shed(void)
{
    /* Protect the busier core. Unlike legacy, QUAD can fall back to one note per part. */
    uint32_t load[2] = {0, 0}, chosen = NPARTS;
    for (uint32_t p = 0; p < NPARTS; p++) load[p & 1u] += (uint32_t)s1_active_voices(PART[p]);
    for (uint32_t pass = 0; pass < 2 && chosen == NPARTS; pass++) {
        const uint32_t core = (load[1] > load[0] ? 1u : 0u) ^ pass;
        for (uint32_t p = core; p < NPARTS; p += 2)
            if (set.quad[p].voices > 1 && (chosen == NPARTS ||
                s1_active_voices(PART[p]) > s1_active_voices(PART[chosen]))) chosen = p;
    }
    if (chosen == NPARTS) return;
    const uint8_t limit = --set.quad[chosen].voices;
    if (chosen == cur_part) set.voices = limit;
    else quad_panel[chosen].voices = limit;
    ev_push_to(chosen, EV_VOICES, 0, limit);
    set_dirty = 1; ui.redraw = UX_ALL;
    app_say("CPU FULL", QUAD_NAME[chosen]);
}
