/* Musical roles for browsing. Old/imported records have no category: infer it from the name.
 * Explicit categories use the record's spare bytes, leaving Synth1's 99 parameters untouched.
 * The 13 sound categories are the Patch Manager's (app/site/manager/library.js), with the same IDs: a record's
 * byte [2] is the manager's index + 1. BASS..OTHER keep the IDs of 0.4; the five after them are new in 0.5. */
#pragma once

enum { CAT_ALL, CAT_BASS, CAT_PAD, CAT_LEAD, CAT_KEYS, CAT_PLUCK, CAT_ARP, CAT_FX, CAT_OTHER,
       CAT_DRUMS, CAT_STRINGS, CAT_BRASS, CAT_ORGAN, CAT_VOCAL, CAT_USER, CAT_FACTORY, CAT_COUNT };
#define CAT_LAST CAT_VOCAL               /* the last sound category; USER and FACTORY are filters */
static const char *const CATEGORY[] = {"ALL", "BASS", "PAD", "LEAD", "KEYS", "PLUCK", "ARP", "FX", "OTHER",
                                       "DRUMS", "STRINGS", "BRASS", "ORGAN", "VOCAL", "USER", "FACTORY"};
/* the order the browser steps through them, as the Patch Manager sorts: instruments first, FX and OTHER last */
static const uint8_t CAT_ORDER[CAT_COUNT] = {CAT_ALL, CAT_BASS, CAT_PAD, CAT_LEAD, CAT_KEYS, CAT_PLUCK, CAT_ARP,
    CAT_DRUMS, CAT_STRINGS, CAT_BRASS, CAT_ORGAN, CAT_VOCAL, CAT_FX, CAT_OTHER, CAT_USER, CAT_FACTORY};
#define CAT_MARK 0xC1u

/* The name as the manager reads it: camelCase split, upper case, every run of non-letters one space, and a space
 * at both ends, so " WORD " finds a whole word and " WORD" a word's start. */
static void name_words(const char *name, char *w, uint32_t size)
{
    uint32_t n = 0, i;
    char prev = 0;
    w[n++] = ' ';
    for (i = 0; name[i] && n + 3u < size; i++) {
        const char c = name[i];
        const int lower = c >= 'a' && c <= 'z', upper = c >= 'A' && c <= 'Z';
        if (lower || upper) {
            if (upper && prev >= 'a' && prev <= 'z' && w[n - 1u] != ' ')
                w[n++] = ' ';
            w[n++] = lower ? (char)(c - ('a' - 'A')) : c;
        } else if (w[n - 1u] != ' ') {
            w[n++] = ' ';
        }
        prev = c;
    }
    if (w[n - 1u] != ' ')
        w[n++] = ' ';
    w[n] = 0;
}

static int name_has(const char *w, const char *part)
{
    uint32_t i, j;
    for (i = 0; w[i]; i++) {
        for (j = 0; part[j] && w[i + j] == part[j]; j++) {}
        if (!part[j])
            return 1;
    }
    return 0;
}

/* any of a space-separated list of parts (each as given: " X " a whole word, " X" a word's start, "X" anywhere) */
static int name_any(const char *w, const char *const *parts)
{
    for (; *parts; parts++)
        if (name_has(w, *parts))
            return 1;
    return 0;
}

static uint8_t category_guess(const char *name)
{
    static const char *const drums[] = {"KICK", "SNARE", "DRUM", "CYMBAL", "HIHAT", "HI HAT", "COWBELL", "COW BELL",
        " BD ", " SD ", " HH ", " CLAP ", " CLAPS ", " HAT ", " HATS ", " TOM ", " TOMS ", " SYNTOM ", " RIMSHOT ",
        " RIM SHOT ", " SHAKER ", " TAMBOURINE ", " CONGA ", " CONGAS ", " BONGO ", " BONGOS ", 0};
    static const char *const bass[] = {"BASS", " BS ", " BAS ", 0};
    static const char *const lead[] = {"LEAD", " LD ", 0};
    static const char *const arp[] = {" ARP ", " ARPS ", " SEQ ", " SEQUENCE ", " SEQUENCES ", "ARPEG", 0};
    static const char *const pluck[] = {"PLUCK", "GUITAR", " GTR ", " HARP", "PIZZ", " BELL ", " BELLS ", "MALLET",
        "MARIMBA", "VIBRAPHONE", "CELESTA", "GLOCKENSPIEL", " VIBES ", 0};
    static const char *const organ[] = {"ORGAN", "HAMMOND", "DRAWBAR", 0};
    static const char *const strings[] = {"STRING", "VIOLIN", "VIOLA", "CELLO", 0};
    static const char *const brass[] = {"BRASS", "TRUMPET", "TROMBONE", "TUBA", " HORN ", " HORNS ", "SAX", "FLUTE",
        "CLARINET", "OBOE", "PICCOLO", "WOODWIND", 0};
    static const char *const vocal[] = {"CHOIR", "VOICE", "VOCAL", " VOX ", 0};
    static const char *const perc[] = {"PERCUSSION", " PERC ", 0};
    static const char *const keys[] = {"KEY", "PIANO", " EP ", "CLAV", "RHODES", "WURLI", 0};
    static const char *const fx[] = {"FX", "NOISE", "WIND", "SWEEP", "RISER", 0};
    char w[64];
    name_words(name, w, sizeof w);
    if (name_any(w, drums)) return CAT_DRUMS;
    if (name_has(w, "BASSOON")) return CAT_BRASS;
    if (name_any(w, bass)) return CAT_BASS;
    if (name_has(w, "PAD")) return CAT_PAD;
    if (name_any(w, lead)) return CAT_LEAD;
    if (name_any(w, arp)) return CAT_ARP;
    if (name_has(w, "HARPSICHORD")) return CAT_KEYS;
    if (name_any(w, pluck)) return CAT_PLUCK;
    if (name_any(w, organ)) return CAT_ORGAN;
    if (name_any(w, strings)) return CAT_STRINGS;
    if (name_any(w, brass)) return CAT_BRASS;
    if (name_any(w, vocal)) return CAT_VOCAL;
    if (name_any(w, perc)) return CAT_DRUMS;
    if (name_any(w, keys)) return CAT_KEYS;
    if (name_has(w, "ARP") && !name_has(w, "SHARP") && !name_has(w, "WARP") && !name_has(w, "CARP")) return CAT_ARP;
    if (name_any(w, fx)) return CAT_FX;
    return CAT_OTHER;
}
