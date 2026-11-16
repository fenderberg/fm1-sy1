/* Musical roles for browsing. Old/imported records have no category: infer it from the name.
 * Explicit categories use the record's spare bytes, leaving Synth1's 99 parameters untouched. */
#pragma once

enum { CAT_ALL, CAT_BASS, CAT_PAD, CAT_LEAD, CAT_KEYS, CAT_PLUCK, CAT_ARP, CAT_FX, CAT_OTHER, CAT_USER, CAT_FACTORY, CAT_COUNT };
static const char *const CATEGORY[] = {"ALL", "BASS", "PAD", "LEAD", "KEYS", "PLUCK", "ARP", "FX", "OTHER", "USER", "FACTORY"};
#define CAT_MARK 0xC1u

static int name_has(const char *name, const char *word)
{
    uint32_t i, j;
    for (i = 0; name[i]; i++) {
        for (j = 0; word[j] && name[i + j]; j++) {
            const char c = name[i + j] >= 'a' && name[i + j] <= 'z' ? name[i + j] - ('a' - 'A') : name[i + j];
            if (c != word[j])
                break;
        }
        if (!word[j])
            return 1;
    }
    return 0;
}

static uint8_t category_guess(const char *name)
{
    if (name_has(name, "BASS") || name_has(name, "BS ") || name_has(name, "BAS ")) return CAT_BASS;
    if (name_has(name, "PAD") || name_has(name, "STRING") || name_has(name, "CHOIR")) return CAT_PAD;
    if (name_has(name, "LEAD") || name_has(name, "LD ")) return CAT_LEAD;
    if (name_has(name, "PLUCK") || name_has(name, "HARP") || name_has(name, "PIZZ")) return CAT_PLUCK;
    if (name_has(name, "ARP")) return CAT_ARP;
    if (name_has(name, "KEY") || name_has(name, "PIANO") || name_has(name, "BELL") ||
        name_has(name, "ORGAN") || name_has(name, "EP ") || name_has(name, "CLAV")) return CAT_KEYS;
    if (name_has(name, "FX") || name_has(name, "NOISE") || name_has(name, "WIND") || name_has(name, "SWEEP")) return CAT_FX;
    return CAT_OTHER;
}
