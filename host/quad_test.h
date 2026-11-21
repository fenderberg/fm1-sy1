/* Integration tests against the app, including audio routing and legacy migration. */
static void test_part_settings(void)
{
    memset(flash, 0xff, sizeof flash);
    memset(store_len, 0, sizeof store_len);
    power_on();

    const uint32_t global = NPAGES - 1u;
    CHECK(cell_at(global, GLOBAL_MODE, 0) == X_BI && cell_at(global, GLOBAL_MODE, 1) == X_NONE &&
          cell_at(global, GLOBAL_PART, 0) == X_NONE && cell_at(global, GLOBAL_CHANNELS, 0) == X_NONE,
          "Standard offers settings of another mode");
    mode_set(1); run(50);
    CHECK(cell_at(global, GLOBAL_MODE, 2) == X_LAYER && cell_at(global, GLOBAL_CHANNELS, 2) == X_NONE,
          "Bitimbral lists the wrong part controls");
    const uint8_t split_layer[] = {0xf0,0x7d,0x53,0x59,0x31,0x0b,0,1,0x7f,0x7f,0xf7};
    send_sysex(split_layer, sizeof split_layer); run(30);
    CHECK(sysex_n == 11 && sysex_last[5] == 0x4b && BI_AUDIO == LAY_SPLIT && BI_LAYER &&
          a_layout == LAY_SPLIT && a_layer, "Split + Layer did not apply");
    midi(0x92, 60, 100); run(50);
    CHECK(s1_active_voices(PART[0]) && s1_active_voices(PART[1]), "Split + Layer did not play both parts");
    send_sysex(split_layer, sizeof split_layer); run(30);
    CHECK(s1_active_voices(PART[0]) && s1_active_voices(PART[1]), "repeating routing stopped notes");
    all_off();
    settings_save(); power_on(); run(50);
    CHECK(BI_AUDIO == LAY_SPLIT && BI_LAYER, "Split + Layer did not survive restart");

    bi_routing_set(LAY_STEREO, 0);
    channel_set(0, 1, CH_L);
    run(50); midi(0x92, 60, 100); run(50);
    CHECK(s1_active_voices(PART[0]) && s1_active_voices(PART[1]), "shared Bitimbral channel did not reach both parts");
    midi(0xc2, 6, 0); run(70);
    CHECK(cur_index == 6 && other.index == 6, "shared Program Change did not reach both parts");

    mode_set(2); run(50);
    CHECK(cell_at(global, GLOBAL_MODE, 2) == X_NONE && cell_at(global, GLOBAL_CHANNELS, 3) == X_QCH4,
          "QUAD lists the wrong routing/channel controls");
    quad_select(0, 0); quad_select(3, 0);
    run(50); midi(0x90, 60, 100); midi(0x93, 67, 100); run(60);
    tap(B_SEQ);
    const uint32_t events_before = ev_w;
    cell_edit(X_CHL, -1);
    cell_edit(X_LAYOUT, -1);
    CHECK(ev_w == events_before, "turning beyond a QUAD limit queued an audio event");
    channel_set(1, 3, 9); run(50);
    CHECK(s1_active_voices(PART[0]) && PART[0]->hold && !s1_active_voices(PART[3]),
          "changing channel 4 stopped a different part or left old notes hanging");

    all_off();
    for (int tick = 0; tick < 26; tick++) { midi(0xf8, 0, 0); run(21); }
    CHECK(ext_tempo && clk_live, "external clock did not lock");
    const uint32_t external = ext_tempo;
    cell_edit(X_TEMPO, 11); run(40);
    CHECK(ext_tempo == external && PART[0]->tempo == (float)external && set.tempo == 131,
          "editing fallback BPM changed the active external tempo");
    run(2200);
    CHECK(!ext_tempo && PART[0]->tempo == 131.0f, "clock dropout did not use the edited fallback BPM");

    char label[40]; const char *unit;
    cur.values[S1P_DELAY_SPREAD] = 64;
    set.tempo = 120;
    ux_value(S1P_DELAY_TIME, 12, label, &unit);
    CHECK(!strcmp(label, "125") && !strcmp(unit, "ms FIT"), "QUAD delay display does not show the fitted 125 ms");
}

static void test_quad(void)
{
    memset(flash, 0xFF, sizeof flash);
    memset(store_len, 0, sizeof store_len);
    power_on();
    mode_set(1); run(30);
    patch_select(5); cell_edit(S1P_FILTER_FREQ, 2);
    const s1_patch legacy_l = cur;
    const uint8_t legacy_voices = set.voices;
    focus_set(1); patch_select(6); cell_edit(S1P_FILTER_FREQ, -3);
    const s1_patch legacy_r = cur;
    const settings_t legacy_settings = set;

    mode_set(2); run(50);
    CHECK(QUAD_ON && a_quad && audio_parts() == 4 && voice_budget() == 12, "QUAD was not enabled");
    CHECK(sizeof fx_arena == 262144u, "QUAD changed the legacy FX allocation");
    for (uint32_t p = 0; p < NPARTS; p++) {
        CHECK(set.quad_ch[p] == p + 1 && PART[p]->cfg.max_voices == 3 &&
              PART[p]->cfg.delay_len == QUAD_DELAY_LEN, "QUAD default part %u", p);
        quad_select(p, 0);
    }
    run(50);
    for (uint32_t p = 0; p < NPARTS; p++) midi(0x90u + p, 48u + p * 4, 100);
    run(80);
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(s1_active_voices(PART[p]) == 1, "channel %u missed its part", p + 1);
    CHECK(shown_voices == 4, "QUAD footer did not count all four parts");
    midi(0x99, 36, 100); run(30);
    CHECK(all_voices() == 4, "drum channel leaked into QUAD");

    /* The synth tracks use CC80..87; the MIDI tracks use the default template. */
    const int32_t cutoff = s1_get_param(PART[0], S1P_FILTER_FREQ);
    midi(0xB0, 84, 127); midi(0xB2, 71, 0); run(300);
    CHECK(s1_get_param(PART[0], S1P_FILTER_FREQ) > cutoff &&
          s1_get_param(PART[2], S1P_FILTER_FREQ) < cutoff, "Circuit synth/MIDI macro maps did not work");
    CHECK(s1_get_param(PART[1], S1P_FILTER_FREQ) == cutoff &&
          s1_get_param(PART[3], S1P_FILTER_FREQ) == cutoff, "Circuit macros changed other parts");
    static const uint8_t circuit_cc[2][8] = {
        {80, 81, 82, 83, 84, 85, 86, 87},
        {1, 2, 5, 11, 71, 74, 12, 13}
    };
    uint8_t targets[NPARTS][8];
    for (uint32_t p = 0; p < NPARTS; p++)
        memcpy(targets[p], mac[p].target, sizeof targets[p]);
    for (uint32_t p = 0; p < NPARTS; p++) {
        const uint8_t *const cc = circuit_cc[p >= 2];
        for (uint32_t knob = 0; knob < 8; knob++) {
            targets[p][knob] = (uint8_t)(16u + knob * 12u);
            midi(0xB0u + p, cc[knob], targets[p][knob]);
            run(15);
            for (uint32_t other = 0; other < NPARTS; other++)
                CHECK(!memcmp(mac[other].target, targets[other], sizeof targets[other]),
                      "Circuit channel %u knob %u (CC%u) misrouted to part %u", p + 1, knob + 1, cc[knob], other + 1);
        }
    }
    for (uint32_t p = 0; p < NPARTS; p++) {
        quad_select(p, p);
        quad_focus(p);
        cell_edit(S1P_FILTER_FREQ, p + 1);
        cell_edit(X_DRONE, 1);
        octave_step((int32_t)p - 1);
        quad_sync();
    }
    quad_focus(2);
    const s1_patch edited = cur;
    quad_focus(0); quad_focus(2);
    CHECK(!memcmp(&cur, &edited, sizeof cur) && set.octave == 1 && cur_dirty,
          "QUAD focus lost its edited patch or part settings");
    run(40);
    keys_held = 1u << 5; run(70); keys_held = 0; run(70);
    CHECK(s1_active_voices(PART[2]) > 0, "panel keys did not play part 3");
    tap(B_PLAY);

    /* Every part's entire stereo signal, including effects, reaches its assigned bus. */
    for (uint32_t layout = 0; layout < 2; layout++) {
        set.quad_layout = (uint8_t)layout; mode_push(); run(30);
        for (uint32_t p = 0; p < NPARTS; p++) {
            all_off();
            for (uint32_t k = 0; k < NPARTS; k++) s1_fx_reset(PART[k]);
            s1_note_on(PART[p], 60, 100);
            gain_now = 8388607.0f;
            render_span(audio, APP_BLOCK, 0);
            int audible = 0;
            for (uint32_t i = 0; i < APP_BLOCK; i++) {
                float l = PART_L[p][i], r = PART_R[p][i];
                if (!layout) { const float mono = 0.5f * (l + r); l = p < 2 ? mono : 0; r = p >= 2 ? mono : 0; }
                const int32_t el = (int32_t)(soft(l * OUT_GAIN) * gain_now);
                const int32_t er = (int32_t)(soft(r * OUT_GAIN) * gain_now);
                CHECK(audio[2*i] == el && audio[2*i+1] == er, "QUAD routing layout %u part %u frame %u", layout, p, i);
                audible |= audio[2*i] != 0 || audio[2*i+1] != 0;
            }
            CHECK(audible, "QUAD routing part %u was silent", p);
        }
    }
    all_off();
    /* Clearing one effect must never touch another part's delay or chorus. */
    for (uint32_t p = 0; p < NPARTS; p++) {
        PART[p]->cfg.delay_buf[100] = (int16_t)(100 + p);
        PART[p]->cfg.chorus_buf[100] = (float)(100 + p);
    }
    s1_fx_reset(PART[1]);
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(PART[p]->cfg.delay_buf[100] == (p == 1 ? 0 : 100 + (int)p) &&
        PART[p]->cfg.chorus_buf[100] == (p == 1 ? 0.0f : (float)(100+p)), "FX buffers alias part %u", p);

    /* Settings and channel 16, including intentionally shared channels for layers. */
    quad_focus(3); cell_edit(X_QCH4, 100);
    CHECK(set.quad_ch[3] == 5, "channel encoder skipped more than one channel");
    for (uint32_t i = 0; i < 20; i++) cell_edit(X_QCH4, 1);
    CHECK(set.quad_ch[3] == 16, "channel did not reach 16");
    run(30); midi(0x93, 60, 100); run(30);
    CHECK(all_voices() == 0, "old MIDI channel still plays");
    midi(0x9F, 60, 100); run(40);
    CHECK(s1_active_voices(PART[3]) == 1 && all_voices() == 1, "channel 16 not routed");
    all_off();
    set.quad_ch[2] = 16; mode_push(); run(30);
    midi(0x9F, 60, 100); run(40);
    CHECK(s1_active_voices(PART[2]) == 1 && s1_active_voices(PART[3]) == 1, "shared MIDI channel did not layer");
    all_off();
    for (uint32_t p = 0; p < NPARTS; p++) quad_select(p, 7);
    run(30); midi(0xFA, 0, 0); midi(0xF8, 0, 0); run(20);
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(PART[p]->arp_ext && PART[p]->clk_pos == 1, "part %u missed clock", p);
    run(2200);
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(!PART[p]->arp_ext, "part %u did not release external clock", p);

    /* One-core and dropped-job paths render each part exactly once. */
    for (uint32_t fallback = 0; fallback < 2; fallback++) {
        test_cpu2_on = fallback != 0; test_cpu2_drop = fallback != 0;
        uint64_t before[NPARTS];
        for (uint32_t p = 0; p < NPARTS; p++) before[p] = PART[p]->now;
        render_span(audio, APP_BLOCK, 0);
        for (uint32_t p = 0; p < NPARTS; p++) CHECK(PART[p]->now == before[p] + APP_BLOCK, "fallback rendered part %u incorrectly", p);
    }
    test_cpu2_on = 1; test_cpu2_drop = 0;
    quad_focus(0);
    cell_edit(X_VOICES, 1);
    CHECK(set.voices == 3, "QUAD exceeded its per-core voice budget");
    cell_edit(X_VOICES, -1); run(30);
    quad_focus(2); cell_edit(X_VOICES, 1); run(30);
    CHECK(set.voices == 4 && set.quad[0].voices == 2, "voice budget could not be redistributed");
    for (uint32_t i = 0; i < 24; i++) { app_overload(); run(32); }
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(set.quad[p].voices == 1 && PART[p]->cfg.max_voices == 1, "overload did not reach mono for part %u", p);

    /* QUAD leaves the original bitimbral sounds/settings and buffer lengths intact. */
    mode_set(1); run(50);
    CHECK(!QUAD_ON && cur_part == 1 && !memcmp(&cur, &legacy_r, sizeof cur), "legacy R edits were lost");
    CHECK(set.voices == legacy_settings.voices && set.voices_b == legacy_settings.voices_b &&
          BI_LAYOUT == ((legacy_settings.bi >> 1) & 3u), "legacy settings changed");
    focus_set(0);
    CHECK(!memcmp(&cur, &legacy_l, sizeof cur) && set.voices == legacy_voices, "legacy L edits were lost");
    CHECK(synth.cfg.delay_len == DELAY_LEN && synth_b.cfg.delay_len == DELAY_LEN_B &&
          synth_c.cfg.delay_buf == 0, "legacy FX buffers were not restored");
    mode_set(0); run(30); midi(0x9F, 60, 100); run(40);
    CHECK(s1_active_voices(&synth) == 1 && !s1_active_voices(&synth_c), "standard omni mode regressed");
    all_off();
    mode_set(2); run(30); quad_focus(3); patch_select(4); octave_step(-1);
    set.quad_layout = 0; quad_sync(); mode_push(); run(30);
    const settings_t expected = set;
    CHECK(settings_save() == 0, "QUAD settings could not be saved");
    power_on();
    CHECK(QUAD_ON && cur_part == 3 && cur_index == 4 && set.octave == expected.quad[3].octave &&
          set.quad_ch[3] == 16 && set.quad_layout == 0, "QUAD settings did not survive reboot");
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(PART[p]->cfg.max_voices == expected.quad[p].voices,
        "reboot changed QUAD voice limit part %u", p);

    const uint8_t query[] = {0xF0,0x7D,0x53,0x59,0x31,0x08,0x7F,0x7F,0xF7};
    send_sysex(query, sizeof query);
    CHECK(sysex_n == 22 && sysex_last[6] == 2 && sysex_last[15] == 15 && sysex_last[16] == 15,
          "QUAD status is incomplete");
    const uint8_t channels[] = {0xF0,0x7D,0x53,0x59,0x31,0x0A,2,1,4,5,6,7,0xF7};
    send_sysex(channels, sizeof channels);
    CHECK(cur_part == 2 && set.quad_layout == 1 && set.quad_ch[0] == 5 && set.quad_ch[3] == 8 &&
          sysex_last[5] == 0x4A, "QUAD settings SysEx failed");

    /* Whole-bank actions while QUAD is active must update all four cached panels. */
    s1_patch uploaded;
    fact_load(0, &uploaded);
    uint8_t frame[700];
    uint32_t length = sy1_frame(&uploaded, 0, frame);
    send_sysex(frame, length);
    for (uint32_t p = 0; p < NPARTS; p++) {
        uint8_t pick[] = {0xF0,0x7D,0x53,0x59,0x31,0x09,(uint8_t)p,0,0,0xF7};
        send_sysex(pick, sizeof pick);
        CHECK(sysex_last[9] == 0 && (cur_part == p ? cur_index : quad_panel[p].index) == NFACT,
              "slot selection failed for QUAD part %u", p);
    }
    quad_focus(2); cell_edit(S1P_FILTER_FREQ, 2); run(30);
    const int32_t unsaved_cutoff = cur.values[S1P_FILTER_FREQ];
    uploaded.values[S1P_FILTER_FREQ] += 8;
    length = sy1_frame(&uploaded, 0, frame); send_sysex(frame, length);
    CHECK(QUAD_ON && cur_part == 2 && cur.values[S1P_FILTER_FREQ] == unsaved_cutoff && cur_dirty &&
          saved_values[S1P_FILTER_FREQ] == uploaded.values[S1P_FILTER_FREQ], "bank write erased an edited QUAD patch");
    quad_focus(3);
    CHECK(cur.values[S1P_FILTER_FREQ] == uploaded.values[S1P_FILTER_FREQ] && !cur_dirty,
          "bank write left stale QUAD sound");
    const uint8_t invalid[] = {0xF0,0x7D,0x53,0x59,0x31,0x09,4,0,0,0xF7};
    send_sysex(invalid, sizeof invalid);
    CHECK(sysex_last[9] == 1, "invalid fifth part was accepted");
    all_off(); s1_note_on(PART[3], 60, 100);
    const uint8_t qquery[] = {0xF0,0x7D,0x53,0x59,0x31,0x0A,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0xF7};
    send_sysex(qquery, sizeof qquery);
    CHECK(s1_active_voices(PART[3]) == 1, "QUAD settings query stopped a note");
    const uint8_t remove[] = {0xF0,0x7D,0x53,0x59,0x31,0x05,0,0,0xF7};
    send_sysex(remove, sizeof remove);
    CHECK(QUAD_ON && cur_index == 0 && bank_count == 0, "slot removal left the active QUAD panel stale");
    for (uint32_t p = 0; p < NPARTS; p++) CHECK(set.quad[p].patch == 0 &&
        (p == cur_part ? cur_index : quad_panel[p].index) == 0, "slot removal left part %u stale", p);

    /* Older settings migrate without changing the existing two-part configuration. */
    mode_set(1); run(30);
    settings_t old = set;
    old.voices = 5; old.voices_b = 7; old.octave = -2; old.ch_l = 11; old.ch_r = 12;
    plat_store_save(OBJ_SETTINGS, &old, SET_V4_BYTES); power_on();
    CHECK(MODE_VALUE == 1 && set.voices == 5 && set.voices_b == 7 && set.octave == -2 &&
          CH_L == 11 && CH_R == 12 && set.quad_ch[0] == 1 && set.quad[0].voices == 3,
          "24-byte legacy settings were not migrated");
}
