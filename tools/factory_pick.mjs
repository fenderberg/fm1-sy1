// Which community patches can be a factory preset, and as what. A patch qualifies only when its name and its sound
// agree on one category: a pad's name and a slow or long envelope, a pluck's name and a short decay without sustain,
// an arpeggio's name and the arpeggiator on. Names that point at two categories, test or init patches, near silence
// and duplicates of an already chosen sound are left out. Pure: shared by tools/make_factory500.mjs and its test.
export const CATEGORIES = ['Bass', 'Pad', 'Lead', 'Keys', 'Pluck', 'Arp', 'FX', 'Other'];
const P = { OSC_MIX: 5, FILTER_TYPE: 14, FILTER_FREQ: 19, FILTER_AMOUNT: 21, AMP_ATTACK: 25, AMP_DECAY: 26, AMP_SUSTAIN: 27,
  AMP_RELEASE: 28, AMP_GAIN: 29, PLAY_MODE: 38, PORTA_TIME: 39, ARP_ON: 59, KEY_SHIFT: 9, SUB_GAIN: 95, SUB_OCTAVE: 97 };

// Whole words (letters only, so "LEAD2" and "BASS-01" count). Each category's own words; Other: instruments of their own.
const WORDS = {
  Bass: /\b(BASS|BASSES|BS|SUBBASS|BASSLINE|303|ACID ?BASS|REESE)\b/,
  Pad: /\b(PAD|PADS|STRING|STRINGS|STR|CHOIR|CHOIRS|ATMOS|ATMOSPHERE|AMBIENT|WARMPAD|SWEEPPAD|DRONE)\b/,
  Lead: /\b(LEAD|LEADS|LD|SOLO|SYNLEAD|SAWLEAD|SQUARELEAD)\b/,
  Keys: /\b(PIANO|EP|EPIANO|RHODES|WURLI|WURLITZER|ORGAN|ORGANS|CLAV|CLAVI|CLAVINET|KEY|KEYS|BELL|BELLS|HARPSICHORD|MARIMBA|VIBES|VIBRAPHONE|CELESTA|GLOCKEN|GLOCK|MALLET|MUSICBOX)\b/,
  Pluck: /\b(PLUCK|PLUCKS|PLUCKED|PLK|HARP|PIZZ|PIZZICATO|KOTO|SITAR|GUITAR|GTR|BANJO|HARPSI)\b/,
  Arp: /\b(ARP|ARPS|ARPEGGIO|ARPEGGIATOR|ARPEGIO|SEQ|SEQUENCE)\b/,
  FX: /\b(FX|SFX|NOISE|WIND|SWEEP|RISER|ZAP|LASER|SIREN|UFO|ALARM|EXPLOSION|WAVES|RAIN|THUNDER|SPACE|ROBOT|SCI ?FI)\b/,
  Other: /\b(BRASS|HORN|HORNS|TRUMPET|SAX|FLUTE|OBOE|CLARINET|VOX|VOICE|VOCAL|CHORD|CHORDS|STAB|STABS|PERC|DRUM|KICK|SNARE|HIHAT|TOM|COWBELL)\b/,
};
const REJECT = /\b(INIT|INITIAL|DEFAULT|TEST|TEMPLATE|EMPTY|BLANK|UNTITLED|NONAME|NO NAME|COPY)\b/;
const words = name => name.toUpperCase().replace(/[^A-Z0-9 ]+/g, ' ').replace(/([A-Z])([0-9])/g, '$1 $2').replace(/([0-9])([A-Z])/g, '$1 $2');

/** The categories a name points at (Other only when no other one). */
const DRUMS = /\b(PERC|DRUM|DRUMS|KICK|SNARE|HIHAT|HAT|TOM|COWBELL|CLAP|RIM)\b/;
export function nameCategories(name) {
  const w = words(name), hits = CATEGORIES.filter(c => c !== 'Other' && WORDS[c].test(w));
  if (DRUMS.test(w)) hits.push('Other');              // a drum is a drum: "909 Snare w/o Noise" is no noise effect
  return hits.length ? hits : WORDS.Other.test(w) ? ['Other'] : [];
}

/** Does the sound agree with the category its name gives? (values: the 99 parameters, 0..127 for these) */
export function soundAgrees(category, v) {
  const attack = v[P.AMP_ATTACK], decay = v[P.AMP_DECAY], sustain = v[P.AMP_SUSTAIN], release = v[P.AMP_RELEASE], arp = v[P.ARP_ON];
  if (category === 'Arp') return arp === 1;
  if (arp === 1) return false;                         // an arpeggio under another name plays as an arpeggio
  switch (category) {
    case 'Bass': return attack <= 20 && release <= 90;                                  // starts at once, does not hang on
    case 'Pad': return (attack >= 25 || release >= 70) && sustain >= 40;                // swells or lingers, and holds
    case 'Lead': return attack <= 40 && sustain >= 40 && release <= 100;               // speaks quickly and holds the note
    case 'Keys': return attack <= 20;                                                   // struck: no slow swell
    case 'Pluck': return attack <= 15 && sustain <= 30 && decay <= 90;                  // a short decay, little sustain
    case 'FX': return true;
    case 'Other': return attack <= 60;
  }
  return false;
}

/** Plainly broken sounds: no level, or a low-pass closed with nothing to open it. */
export function healthy(v) {
  if (v[P.AMP_GAIN] < 20) return false;
  if (v[P.FILTER_TYPE] <= 1 && v[P.FILTER_FREQ] < 8 && v[P.FILTER_AMOUNT] <= 70) return false;
  return true;
}

/** A patch's verdict: its category, or why it does not qualify. */
export function classify(name, values) {
  if (!name.trim() || name.trim().length < 3) return { why: 'no name' };
  if (REJECT.test(words(name))) return { why: 'init or test' };
  const cats = nameCategories(name);
  if (!cats.length) return { why: 'name says nothing' };
  if (cats.length > 1) return { why: 'name points two ways: ' + cats.join('+') };
  if (!healthy(values)) return { why: 'silent or closed' };
  if (!soundAgrees(cats[0], values)) return { why: cats[0] + ' name, other sound' };
  return { category: cats[0] };
}

/** How many of each, near even: Other kept small, the rest shared out, a short category's share given to the others. */
export function quotas(available, total, other) {
  const q = { Other: Math.min(other, available.Other || 0) }, rest = CATEGORIES.filter(c => c !== 'Other');
  let left = total - q.Other, open = rest.slice();
  for (const c of rest) q[c] = 0;
  while (left > 0 && open.length) {
    const share = Math.max(1, Math.floor(left / open.length));
    for (const c of open.slice()) {
      const add = Math.min(share, (available[c] || 0) - q[c], left);
      q[c] += add; left -= add;
      if (q[c] >= (available[c] || 0)) open = open.filter(x => x !== c);
      if (!left) break;
    }
  }
  return q;
}
