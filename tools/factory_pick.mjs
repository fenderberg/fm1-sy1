// Which community patches can be a factory preset, and as what. The category comes from the patch name by the
// Patch Manager's own rules (app/site/manager/library.js), so the website and the FM-1 agree; a patch qualifies
// only when its sound agrees with that category too: a pad's slow or long envelope, a pluck's short decay, an
// organ's held note, the arpeggiator on for an arpeggio. Test or init patches, near silence and duplicates of an
// already chosen sound are left out. Pure: shared by tools/make_factory500.mjs and its test.
import {CATEGORIES as ALL, CATEGORY_ORDER, category} from '../app/site/manager/library.js';
export const CATEGORIES = ALL;                                  // firmware ID = index + 1 (app/category.h)
export const ORDER = CATEGORY_ORDER.map(i => ALL[i]);           // the order the FM-1 and the manager browse them in
const P = { FILTER_TYPE: 14, FILTER_FREQ: 19, FILTER_AMOUNT: 21, AMP_ATTACK: 25, AMP_DECAY: 26, AMP_SUSTAIN: 27,
  AMP_RELEASE: 28, AMP_GAIN: 29, ARP_ON: 59 };

const REJECT = /\b(INIT|INITIAL|DEFAULT|TEST|TEMPLATE|EMPTY|BLANK|UNTITLED|NONAME|NO NAME|COPY)\b/;
// Other: only names that still say what they are (a chord, a stab, a hit), not names that say nothing
const OTHER = /\b(CHORD|CHORDS|STAB|STABS|HIT|HITS|POLY|SYNTH|BLIP|BLEEP|BRAAM|HOOVER|SUPERSAW|TRANCE)\b/;
const words = name => name.replace(/([a-z])([A-Z])/g, '$1 $2').toUpperCase().replace(/[^A-Z]+/g, ' ').trim();

/** Does the sound agree with the category its name gives? (values: the 99 parameters, 0..127 for these) */
export function soundAgrees(cat, v) {
  const attack = v[P.AMP_ATTACK], decay = v[P.AMP_DECAY], sustain = v[P.AMP_SUSTAIN], release = v[P.AMP_RELEASE], arp = v[P.ARP_ON];
  if (cat === 'Arp') return arp === 1;
  if (arp === 1) return false;                         // an arpeggio under another name plays as an arpeggio
  switch (cat) {
    case 'Bass': return attack <= 20 && release <= 90;                              // starts at once, does not hang on
    case 'Pad': return (attack >= 25 || release >= 70) && sustain >= 40;            // swells or lingers, and holds
    case 'Lead': return attack <= 40 && sustain >= 40 && release <= 100;           // speaks quickly and holds the note
    case 'Keys': return attack <= 20;                                               // struck: no slow swell
    case 'Plucks': return attack <= 15 && sustain <= 40 && decay <= 100;            // a short decay, little sustain
    case 'Drums & Percussion': return attack <= 10 && sustain <= 40;                // a hit that dies away
    case 'Strings': return sustain >= 40;                                           // bowed: the note holds
    case 'Brass & Woodwinds': return attack <= 60 && sustain >= 40;                 // blown: speaks and holds
    case 'Organ': return attack <= 15 && sustain >= 60;                             // on at once, held at full
    case 'Vocal & Choir': return sustain >= 40;
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
  const w = words(name);
  if (w.replace(/ /g, '').length < 3) return { why: 'no name' };
  if (REJECT.test(w)) return { why: 'init or test' };
  const cat = category(name);
  if (cat === 'Other' && !OTHER.test(w)) return { why: 'name says nothing' };
  if (!healthy(values)) return { why: 'silent or closed' };
  if (!soundAgrees(cat, values)) return { why: cat + ' name, other sound' };
  return { category: cat };
}

/** How many of each, near even: Other kept small, the rest shared out, a short category's share given to the others. */
export function quotas(available, total, other) {
  const q = { Other: Math.min(other, available.Other || 0) }, rest = ORDER.filter(c => c !== 'Other');
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
