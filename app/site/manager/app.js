// The Patch Manager: a library on the left, the FM-1's 1,024 user slots on the right.
// Slot edits are staged for Sync; playback and device settings are applied over MIDI.
import { parseSy1, unzipSy1 } from '../emu/app_own.js';
import { CATEGORIES, CATEGORY_ORDER, compareCategory, category, fold, patchName, bankName, asciiName, RECORD, PART, recordFrom, withMeta, recordName,
  recordCategory, recordKey, fullKey, packFrame, unpack7, insertAt, appendSlots, arrange, changes } from './library.js?v=categories13';

const $ = id => document.getElementById(id);
const HEAD = [0xf0,0x7d,0x53,0x59,0x31], SLOTS = 1024, ROW = 36;
const pad = n => String(n).padStart(3,'0'), num = n => n.toLocaleString('en-US');
const esc = t => String(t).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const b64 = u => btoa(String.fromCharCode(...u)), unb64 = t => Uint8Array.from(atob(t), c => c.charCodeAt(0));
const stored = (key, fallback) => { try { return JSON.parse(localStorage.getItem(key) || 'null') ?? fallback; } catch { return fallback; } };
const store = (key, value) => { try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch { return false; } };

// ---- state
let entries = [], banks = new Map(), byId = new Map(), factoryIds = new Set(), localNumber = 0, importing = false;
const bankCache = new Map();
const favorites = new Set(stored('sy1-favorites', []));
let known = stored('sy1-known', {});                 // a sound's key -> the library patch it came from
let sets = stored('sy1-sets', []);
// An item: {uid, name, cat, explicit, rec, entry, locked}. rec: the 226-byte record once known (read from the FM-1,
// or written to it); entry: the library patch it came from; locked: on an older SY1, where only the name is known.
let base = new Array(SLOTS).fill(null), slots = base.slice(), history = [], uid = 1;
let baseSlot = new Map(), curSlot = new Map(), libSlot = new Map();
let lview = [], dview = [], lsel = new Set(), dsel = new Set(), lanchor = 0, danchor = 0;
let lcat = -1, dcat = -1, lfav = false, lnew = false, lfactory = false, drag = null;
let access = null, input = null, output = null, waiting = null, busy = false, cancelled = false, legacy = false;
// Bitimbral (SY1 with SysEx 08/09): the FM-1's mode, output, channels and the user slots parts L and R play (-1: none).
let partsOK = false, quadOK = false, routingOK = false, bi = null, pinned = null, lastR = null;
const channelDrafts = new Map();
const devRowHeight = () => bi?.mode === 2 ? 68 : ROW;
const LAYOUTS = ['Split', 'Stereo', 'Double'];

const bankOf = it => it.entry ? banks.get(it.entry.bank).name : 'On the FM-1';
const newItem = (name, cat, more) => ({uid: uid++, name, cat, explicit: false, rec: null, entry: null, locked: false, ...more});
function fromRecord(rec) {
  const c = recordCategory(rec);
  return newItem(recordName(rec), c.cat, {explicit: c.explicit, rec, entry: byId.get(known[recordKey(rec)]) || null});
}
const sig = it => (it.rec ? fullKey(it.rec) : it.entry ? 'e:' + it.entry.id : 'n:') + '|' + it.name + '|' + (it.explicit ? it.cat : -1);

function toast(text) { const t = $('toast'); t.textContent = text; t.hidden = false; clearTimeout(toast.t); toast.t = setTimeout(() => { t.hidden = true; }, 3600); }
function say(text, bad = false) { $('status').textContent = text; $('status').className = bad ? 'bad' : 'muted'; }
/** Stage a new layout (null: it did not fit). Undo brings the old one back. */
function commit(next) {
  if (busy) return false;
  if (!next) { toast('That does not fit in the free slots. Pick an earlier slot or remove some patches first.'); return false; }
  history.push(slots); if (history.length > 100) history.shift();
  slots = next; refresh(); return true;
}

// ---- lists: only the rows in view exist
function vlist(el, count, render, rowHeight = () => ROW) {
  const sp = el.firstElementChild;
  const draw = () => {
    const height = rowHeight(), n = count();
    const a = Math.max(0, Math.floor(el.scrollTop / height) - 5), b = Math.min(n, Math.ceil((el.scrollTop + el.clientHeight) / height) + 5);
    sp.style.height = n * height + 'px';
    let h = ''; for (let i = a; i < b; i++) h += render(i, i * height);
    sp.innerHTML = n ? h : '<p class="none">' + esc(el.dataset.none || '') + '</p>';
  };
  el.addEventListener('scroll', () => { if (!el.querySelector('.rename')) draw(); }, {passive: true});
  return draw;
}
const drawLib = vlist($('llist'), () => lview.length, (k, y) => {
  const p = lview[k], on = libSlot.get(p.id), sel = lsel.has(p.id), fav = favorites.has(p.id), name = esc(p.name);
  return `<div class="prow${sel ? ' sel' : ''}" style="top:${y}px" data-k="${k}" draggable="true"><input type="checkbox" data-act="pick"${sel ? ' checked' : ''} aria-label="Select ${name}"><i class="dot c${p.cat}" title="${CATEGORIES[p.cat]}"></i><span class="nm">${name}</span><span class="bk">${esc(banks.get(p.bank).name)}</span>${on === undefined ? '<span class="badge on" style="visibility:hidden"></span>' : `<span class="badge on" title="Already in slot ${pad(on + 1)}">${pad(on + 1)}</span>`}<button type="button" class="ic${fav ? ' on' : ''}" data-act="fav" aria-pressed="${fav}" aria-label="Favourite ${name}">${fav ? '★' : '☆'}</button><button type="button" class="ic pl${pinned === p ? ' on' : ''}" data-act="pin" aria-pressed="${pinned === p}" aria-label="Pair ${name} as part L" title="Pair as part L: ▶ then plays other sounds as R with it">L</button><button type="button" class="ic" data-act="play" aria-label="Preview ${name}${pinned && pinned !== p ? ' as part R with ' + esc(pinned.name) : ''}">▶</button></div>`;
});
function status(s) {
  const it = slots[s], b = base[s];
  if (it) { const from = baseSlot.get(it.uid); return from === undefined ? ['new', 'new'] : from !== s ? ['moved', 'from ' + pad(from + 1)] : b !== it ? ['edited', 'edited'] : null; }
  return b && !curSlot.has(b.uid) ? ['removed', 'removed'] : null;
}
const drawDev = vlist($('dlist'), () => dview.length, (k, y) => {
  const s = dview[k], it = slots[s], st = status(s), badge = st ? `<span class="badge ${st[0]}">${st[1]}</span>` : '<span class="badge"></span>';
  if (!it) return `<div class="prow empty${st ? ' gone' : ''}" style="top:${y}px" data-k="${k}"><span class="no">${pad(s + 1)}</span><span class="nm">${st ? esc(base[s].name) : 'empty'}</span>${badge}</div>`;
  const sel = dsel.has(it.uid), name = esc(it.name), fixed = it.locked || busy ? ' disabled' : '';
  const quad = bi?.mode === 2, buttons = partButtons(s, it, name);
  return `<div class="prow${quad ? ' quad-row' : ''}${sel ? ' sel' : ''}" style="top:${y}px" data-k="${k}"${it.locked || legacy ? '' : ' draggable="true"'}><div class="dev-main"><span class="no">${pad(s + 1)}</span><input type="checkbox" data-act="pick"${sel ? ' checked' : ''} aria-label="Select ${name}"><i class="dot c${it.cat}"></i><span class="nm"${it.locked ? '' : ' title="Double-click to rename"'}>${name}</span><span class="bk">${esc(bankOf(it))}</span>${badge}<select data-act="cat" aria-label="Character of ${name}"${fixed}>${CATEGORY_ORDER.map(i => `<option value="${i}"${i === it.cat ? ' selected' : ''}>${esc(CATEGORIES[i])}</option>`).join('')}</select>${quad ? '' : buttons}<button type="button" class="ic" data-act="del" aria-label="Remove ${name}"${busy ? ' disabled' : ''}>✕</button></div>${quad ? `<div class="quad-actions"><span>Assign to part</span>${buttons}</div>` : ''}</div>`;
}, devRowHeight);
/** Assign a synced slot to one of the active hardware parts. */
function partButtons(s, it, name) {
  if (!output || !partsOK || !bi) return '';
  const synced = it === base[s] && !busy, off = synced ? '' : ' disabled', why = synced ? '' : ' (sync first)';
  const one = (p, label) => `<button type="button" class="ic p${bi.layout === 0 ? (bi.mode === 2 ? (p < 2 ? 'l' : 'r') : 'lr'[p]) : 'part'}${bi.slot[p] === s ? ' on' : ''}" data-act="part" data-part="${p}" aria-pressed="${bi.slot[p] === s}" aria-label="Assign ${name} to part ${label}" title="Assign to FM-1 part ${label}${why}"${off}>${label}</button>`;
  return bi.mode === 2 ? [0, 1, 2, 3].map(p => one(p, String(p + 1))).join('') : bi.on ? one(0, 'L') + one(1, 'R') : `<button type="button" class="ic ppart${bi.slot[0] === s ? ' on' : ''}" data-act="part" data-part="0" aria-pressed="${bi.slot[0] === s}" aria-label="Select ${name} on the FM-1" title="Select this patch on the FM-1${why}"${off}>▶</button>`;
}
$('llist').dataset.none = 'No sounds match. Try another search, bank or character.';

function chips(el, counts, active, extra = '') {
  el.innerHTML = `<button type="button" class="chip" data-cat="-1" aria-pressed="${active < 0}">All</button>` +
    CATEGORY_ORDER.map(i => `<button type="button" class="chip" data-cat="${i}" aria-pressed="${active === i}"><i class="dot c${i}"></i>${esc(CATEGORIES[i])} <b>${num(counts[i])}</b></button>`).join('') + extra;
}
function filterLib() {
  const words = fold($('search').value).split(/\s+/).filter(Boolean), bank = $('bank').value, sort = $('lsort').value, counts = new Array(CATEGORIES.length).fill(0);
  lview = [];
  for (const p of entries) {
    if (bank && p.bank !== bank || lfav && !favorites.has(p.id) || lnew && libSlot.has(p.id) || lfactory && !factoryIds.has(p.id) || !words.every(w => p.search.includes(w))) continue;
    counts[p.cat]++; if (lcat < 0 || p.cat === lcat) lview.push(p);
  }
  const byName = (a, b) => a.k < b.k ? -1 : a.k > b.k ? 1 : a.n - b.n;
  if (sort === 'name') lview.sort(byName); else if (sort === 'cat') lview.sort((a, b) => compareCategory(a.cat, b.cat) || byName(a, b));
  chips($('lchips'), counts, lcat, `<button type="button" class="chip" data-tog="fav" aria-pressed="${lfav}">★ Favourites</button><button type="button" class="chip" data-tog="new" aria-pressed="${lnew}">Not on my FM-1</button><button type="button" class="chip" data-tog="factory" id="factory-only" aria-pressed="${lfactory}"${factoryIds.size ? '' : ' disabled'}>Factory ${factoryIds.size || ''}</button>`);
  $('lcount').textContent = `${num(lview.length)} of ${num(entries.length)} sounds`;
  const b = banks.get(bank); $('bank-info').hidden = !b;
  if (b) { $('bank-credit').textContent = `${b.author} · ${b.terms}`; $('bank-notes').textContent = (b.notes || []).map(n => n.path + '\n\n' + n.text).join('\n\n');
    $('bank-download').hidden = !b.file; if (b.file) $('bank-download').href = '../library/' + b.file; }
  $('edit-patch').disabled = lsel.size !== 1;
  const fresh = [...lsel].filter(id => !libSlot.has(id)).length;
  $('add').disabled = !fresh || busy; $('add').textContent = fresh ? `Add ${num(fresh)} to FM-1 →` : 'Add to FM-1 →';
  drawLib();
}
function filterDev() {
  const words = fold($('dq').value).split(/\s+/).filter(Boolean), empty = $('dempty').checked, pend = $('dpend').checked, counts = new Array(CATEGORIES.length).fill(0), filtering = words.length > 0 || dcat >= 0;
  dview = []; let used = 0;
  for (let s = 0; s < SLOTS; s++) {
    const it = slots[s];
    if (it) { used++; counts[it.cat]++; }
    if (pend && it === base[s]) continue;
    if (!it) { if (pend || !filtering && (empty || status(s))) dview.push(s); continue; }
    if (dcat >= 0 && it.cat !== dcat || !words.every(w => fold(it.name + ' ' + bankOf(it)).includes(w))) continue;
    dview.push(s);
  }
  chips($('dchips'), counts, dcat);
  $('dlist').style.setProperty('--row-height', devRowHeight() + 'px');
  $('dcount').textContent = output ? `${num(used)} / ${num(SLOTS)} slots used · ${num(SLOTS - used)} free` : used ? `${num(used)} waiting for your FM-1` : 'not connected';
  $('capfill').style.width = used / SLOTS * 100 + '%';
  const shown = new Set(dview); let h = '';
  for (let s = 0; s < SLOTS; s++) { const it = slots[s];
    h += `<i data-s="${s}" class="${it ? 'c' + it.cat : ''}${it !== base[s] ? ' pend' : ''}${it && dsel.has(it.uid) ? ' hit' : ''}${filtering && it && !shown.has(s) ? ' dim' : ''}${bi && bi.on && bi.layout === 0 && (bi.slot[0] === s || bi.mode === 2 && bi.slot[1] === s) ? ' pl' : ''}${bi && bi.on && bi.layout === 0 && (bi.mode === 2 ? bi.slot[2] === s || bi.slot[3] === s : bi.slot[1] === s) ? ' pr' : ''}${bi && ((!bi.on && bi.slot[0] === s) || (bi.on && bi.layout !== 0 && bi.slot.includes(s))) ? ' sounding' : ''}" title="${pad(s + 1)} · ${it ? esc(it.name) : 'empty'}${bi ? bi.slot.map((slot, p) => slot === s ? bi.mode === 2 ? ' · part ' + (p + 1) : p === 0 ? bi.on ? ' · part L' : ' · playing' : bi.on ? ' · part R' : '' : '').join('') : ''}"></i>`; }
  $('map').innerHTML = h;
  // Always there, so the list does not jump under the pointer when the first patch is selected.
  $('selcount').textContent = dsel.size ? `${dsel.size} selected` : 'Select patches to move or remove them'; $('movebox').hidden = legacy;
  $('move').disabled = $('remove').disabled = $('moveto').disabled = busy || !dsel.size; $('desel').hidden = !dsel.size;
  $('dlist').dataset.none = filtering || pend ? 'Nothing matches this filter.' : output ? 'No patches in the user slots yet. Add sounds from the library, then sync.'
    : 'Connect your FM-1 to see its 1,024 slots. Sounds you add now wait here and go after your last patch when you connect.';
  drawDev();
}
function syncBar() {
  const ch = changes(base, slots);
  const parts = [ch.added && `<b>${num(ch.added)}</b> added`, ch.moved && `<b>${num(ch.moved)}</b> moved`, ch.edited && `<b>${num(ch.edited)}</b> edited`, ch.removed && `<b>${num(ch.removed)}</b> removed`].filter(Boolean);
  $('syncmsg').innerHTML = busy ? 'Working…' : !ch.total ? `<span class="muted">${output ? 'Your FM-1 matches this layout. Add sounds, reorder or tidy up, then sync once.' : 'Add sounds from the library. Connect your FM-1 to arrange its slots and sync.'}</span>`
    : `${parts.join(' · ')} <span class="muted">· ${output ? `${num(ch.total)} slots to write. Your FM-1 is untouched until you sync.` : 'Connect your FM-1 to sync.'}</span>`;
  $('sync').disabled = !ch.total || busy || !output; $('sync').textContent = ch.total && output ? `Sync ${num(ch.total)} patch slots` : 'Sync patch slots';
  $('discard').disabled = !ch.total || busy; $('undo').disabled = !history.length || busy;
}
function refresh() {
  baseSlot = new Map(); curSlot = new Map(); libSlot = new Map();
  base.forEach((it, s) => { if (it) baseSlot.set(it.uid, s); });
  slots.forEach((it, s) => { if (it) { curSlot.set(it.uid, s); if (it.entry) libSlot.set(it.entry.id, s); } });
  for (const u of [...dsel]) if (!curSlot.has(u)) dsel.delete(u);
  for (const id of ['m-arr', 'm-sets']) $(id).classList.toggle('off', legacy || busy);
  drawParts(); drawPair();
  $('legacy').hidden = !legacy;
  filterLib(); filterDev(); syncBar();
}

// ---- the library: the hosted catalogue and local imports
function addEntry(p, bank) {
  const character = CATEGORIES.includes(p.category) ? p.category : category(p.name);
  const e = {...p, bank: bank.id, n: entries.length, cat: CATEGORIES.indexOf(character), k: fold(p.name), search: fold(`${p.name} ${bank.name} ${bank.author} ${character}`)};
  entries.push(e); byId.set(e.id, e);
}
async function catalogue() {
  try {
    const r = await fetch('../library/catalog.json?v=categories13'); if (!r.ok) throw new Error('Catalogue unavailable');
    const data = await r.json();
    try {
      const response = await fetch('../library/factory500.json');
      if (response.ok) factoryIds = new Set((await response.json()).patches.map(p => p.id));
    } catch { /* The main library stays available when the factory list cannot be reached. */ }
    for (const bank of data.banks) { banks.set(bank.id, bank); for (const p of bank.patches) addEntry(p, bank); }
    for (const it of new Set([...base, ...slots])) if (it && it.rec && !it.entry) it.entry = byId.get(known[recordKey(it.rec)]) || null;
    refreshBanks(); refresh();
  } catch { note('The online collection could not be loaded. Reload to try again, or import your own banks.'); }
}
function note(text) { $('import-status').textContent = text; $('import-status').hidden = !text; }
function refreshBanks() {
  const current = $('bank').value; $('bank').replaceChildren(new Option('All banks', ''));
  for (const local of [false, true]) {
    const group = document.createElement('optgroup'); group.label = local ? 'Your local imports' : 'Community library';
    for (const b of [...banks.values()].filter(b => !!b.local === local).sort((a, b) => a.name.localeCompare(b.name))) group.append(new Option(`${b.name} (${b.count})`, b.id));
    if (group.children.length) $('bank').append(group);
  }
  $('bank').value = current; $('total-patches').textContent = num(entries.length); $('total-banks').textContent = banks.size;
}
async function patchBytes(p) {
  if (p.bytes) return p.bytes;
  const bank = banks.get(p.bank);
  if (!bankCache.has(bank.id)) bankCache.set(bank.id, (async () => {
    const r = await fetch('../library/' + bank.file); if (!r.ok) throw new Error('Could not download ' + bank.name);
    return new Map((await unzipSy1(await r.arrayBuffer())).map(f => [f.name, f.bytes]));
  })().catch(e => { bankCache.delete(bank.id); throw e; }));
  const bytes = (await bankCache.get(bank.id)).get(p.path); if (!bytes) throw new Error('Patch missing from ' + bank.name); return bytes;
}
async function entryFiles(entry, out) {
  if (entry.isFile) out.push(await new Promise((ok, no) => entry.file(ok, no)));
  else if (entry.isDirectory) { const r = entry.createReader(); for (;;) { const batch = await new Promise((ok, no) => r.readEntries(ok, no)); if (!batch.length) break; for (const e of batch) await entryFiles(e, out); } }
}
async function addFiles(files) {
  if (importing) return; importing = true; $('pick').disabled = $('pickdir').disabled = true; $('m-imp').open = false;
  let added = 0, skipped = 0; const errors = [], localBanks = new Map();
  const sorted = [...files].filter(f => !f.name.startsWith('._')).sort((a, b) => (a.webkitRelativePath || a.name).localeCompare(b.webkitRelativePath || b.name, undefined, {numeric: true}));
  try {
    for (const f of sorted) {
      if (!/\.(zip|sy1)$/i.test(f.name)) { skipped++; continue; }
      note(`Reading ${f.name}…`); await new Promise(r => setTimeout(r, 0));
      try {
        const raw = await f.arrayBuffer(), zip = /\.zip$/i.test(f.name), parts = zip ? await unzipSy1(raw) : [{name: f.webkitRelativePath || f.name, bytes: new Uint8Array(raw)}];
        if (!parts.length) skipped++;
        for (const part of parts) {
          const patch = parseSy1(part.bytes); if (!patch.lines) { skipped++; continue; }
          const title = patchName(patch, part.name), bn = bankName(part.name, zip ? f.name.replace(/\.zip$/i, '') : 'Your patches');
          if (!localBanks.has(bn)) { const b = {id: 'local-' + (++localNumber), local: true, name: bn, author: 'Your local import', terms: 'Use under the original author’s terms. Files stay in this browser tab.', count: 0, notes: []}; localBanks.set(bn, b); banks.set(b.id, b); }
          const bank = localBanks.get(bn); bank.count++;
          addEntry({id: bank.id + ':' + bank.count, name: title, path: part.name, category: category(title), bytes: part.bytes}, bank); added++;
        }
      } catch (e) { errors.push(f.name + ': ' + e.message); }
    }
    refreshBanks(); $('bank').value = localBanks.size === 1 ? [...localBanks.values()][0].id : '';
    $('search').value = ''; lcat = -1; lfav = lnew = lfactory = false; $('llist').scrollTop = 0; refresh();
    note(`${num(added)} sounds imported into ${localBanks.size} banks.` + (skipped ? ` ${skipped} non-patch files skipped.` : '') + (errors.length ? ' ' + errors.join(' · ') : ''));
  } finally { importing = false; $('pick').disabled = $('pickdir').disabled = false; }
}

// ---- preview: the browser synth in a hidden frame, switched on by the first Play
let player = null, previewId = 0;
const fromPlayer = (frame, test, ms) => new Promise((ok, no) => {
  const done = e => { if (e.source !== frame.contentWindow || e.origin !== location.origin || !e.data || !test(e.data)) return; clearTimeout(timer); removeEventListener('message', done); ok(e.data); };
  const timer = setTimeout(() => { removeEventListener('message', done); no(new Error('The browser synth did not answer.')); }, ms);
  addEventListener('message', done);
});
function startPlayer() {
  return new Promise((ok, no) => {
    const frame = document.createElement('iframe'); frame.className = 'pm-player'; frame.title = 'SY1 preview synth'; frame.setAttribute('aria-hidden', 'true'); frame.tabIndex = -1;
    frame.onload = () => { try { const ready = fromPlayer(frame, d => d.sy1 === 'ready', 30000); frame.contentDocument.getElementById('go').click(); ready.then(() => ok(frame), no); } catch (e) { no(e); } };
    frame.onerror = () => no(new Error('The browser synth could not be loaded.'));
    frame.src = '../emu/?embed=1'; document.body.append(frame);
  });
}
async function preview(p) {
  const id = ++previewId, line = $('preview-status'), with_ = pinned && pinned !== p ? pinned : null;
  line.textContent = `Loading ${p.name}…`;
  try {
    const bytes = await patchBytes(p), pair = with_ ? {name: with_.name, bytes: await patchBytes(with_)} : null;
    if (with_) { lastR = p; drawPair(); }
    if (!player) { line.textContent = 'Switching on the browser synth…'; player = startPlayer().catch(e => { player = null; throw e; }); }
    const frame = await player; if (id !== previewId) return;
    const answer = fromPlayer(frame, d => d.sy1 === 'previewed' && d.id === id, 8000);
    frame.contentWindow.postMessage({sy1: 'preview', id, name: p.name, bytes, pair}, location.origin);
    const a = await answer; if (!a.ok) throw new Error(a.error || 'The browser synth refused this patch.');
    if (id === previewId) line.textContent = with_ ? `▶ Playing ${with_.name} (L) with ${p.name} (R)` : `▶ Playing ${p.name} · ${banks.get(p.bank).name}`;
  } catch (e) { if (id === previewId) line.innerHTML = `Preview failed: ${esc(e.message)} <a href="../emu/">Open the browser synth ↗</a>`; }
}

// ---- MIDI: one outstanding request, matched by command, input port, slot and part
const cmd = (c, ...data) => [...HEAD, c, ...data, 0xf7], slotBytes = s => [s & 127, (s >> 7) & 127];
function onMessage(e) {
  const m = e.data, w = waiting; if (!w || m.length < 8 || HEAD.some((v, i) => m[i] !== v) || m.at(-1) !== 0xf7 || m[5] !== w.cmd || (input && e.target !== input)) return;
  const isSlot = [0x41, 0x44, 0x45, 0x47].includes(w.cmd);
  if (w.cmd === 0x49 && (m.length < 11 || m[6] !== w.part || (m[7] | m[8] << 7) !== w.slot)) return;
  const minimum = w.cmd === 0x4a ? 13 : w.cmd === 0x43 || w.cmd === 0x47 || w.cmd === 0x4b ? 11 : isSlot ? 10 : 8;
  if (m.length < minimum || isSlot && (m[6] | m[7] << 7) !== w.slot || w.cmd === 0x47 && m[8] !== w.part) return;
  waiting = null; clearTimeout(w.timer); w.done({data: m, port: e.target});
}
function ask(out, frame, command, ms = 3000, part = 0) {
  if (waiting) throw new Error('Another MIDI request is still running');
  const at = command === 0x49 ? 7 : 6;   // (09's frame: <part> <slot lo> <hi>)
  return new Promise(done => { const w = {cmd: command, slot: frame[at] | frame[at + 1] << 7, part, done, timer: setTimeout(() => { if (waiting === w) waiting = null; done(null); }, ms)}; waiting = w;
    try { out.send(frame); } catch { clearTimeout(w.timer); waiting = null; done(null); } });
}
/** The FM-1 drops about one SysEx exchange in fifty over USB. Every request asked this way reads, or writes one slot
 *  with the same record, so asking again is safe; a late answer to the first try is just as good. */
async function askAgain(out, frame, command, ms, part = 0, tries = 3) {
  for (let t = 0; t < tries; t++) { const r = await ask(out, frame, command, ms, part); if (r || !output) return r; }
  return null;
}
function setConnected() {
  const on = !!output; $('connection').dataset.state = on ? 'connected' : 'disconnected';
  $('connection-label').textContent = on ? 'FM-1 CONNECTED' : 'FM-1 NOT CONNECTED'; $('connect').textContent = on ? 'Read again' : 'Connect to the FM-1';
  $('connect').className = on ? '' : 'primary'; $('connect').disabled = busy;
  if (!on) { legacy = false; partsOK = false; quadOK = false; routingOK = false; bi = null; channelDrafts.clear(); }
}
/** The FM-1's parts, from an answer to 08. */
function partsFrom(m) {
  if (m.length < 16 || m[6] > 2 || m[6] === 2 && m.length < 22) return null;
  const state = {mode: m[6], on: !!m[6], layout: m[7], layer: m[6] === 1 && m[7] === 2, focus: m[8], ch: [m[9] + 1, m[10] + 1], slot: [(m[11] | m[12] << 7) - 1, (m[13] | m[14] << 7) - 1]};
  if (state.mode === 2) {
    state.ch.push(m[15] + 1, m[16] + 1);
    state.slot.push((m[17] | m[18] << 7) - 1, (m[19] | m[20] << 7) - 1);
  }
  return state;
}
async function readParts() {
  if (!output || !partsOK) return;
  const r = await askAgain(output, cmd(0x08, 0x7f, 0x7f), 0x48, 800);
  if (r) bi = partsFrom(r.data) || bi;
  await readBiRouting();
}
function updateBiRouting(message) {
  bi.layout = message[6];
  bi.layer = !!message[7];
  bi.ch = [message[8] + 1, message[9] + 1];
}
async function readBiRouting() {
  if (!routingOK || bi?.mode !== 1) return;
  const r = await askAgain(output, cmd(0x0b, 0x7f, 0x7f, 0x7f, 0x7f), 0x4b, 800);
  if (r?.data.length >= 11) updateBiRouting(r.data);
}
/** One request that changes the FM-1's parts, then its state read back. */
async function partsDo(frame, answer, part, done, applied) {
  if (busy || !output || !partsOK) return;
  busy = true; setConnected(); refresh();
  try { const r = await askAgain(output, frame, answer, 1500, part); if (!r) throw new Error('The FM-1 did not answer. Press Read again.');
    if (answer === 0x48) bi = partsFrom(r.data) || bi; else if (answer === 0x49 && r.data[9]) throw new Error('That slot is empty on the FM-1. Press Read again.');
    if (answer === 0x48) await readBiRouting();
    else if (answer === 0x4b && r.data.length >= 11) updateBiRouting(r.data);
    else if (answer === 0x4a && r.data.length >= 13) {
      bi.focus = r.data[6]; bi.layout = r.data[7];
      bi.ch = Array.from(r.data.slice(8, 12), ch => ch + 1);
    } else await readParts();
    applied?.();
    if (done) toast(done()); }
  catch (e) { say(e.message, true); }
  finally { busy = false; setConnected(); refresh(); }
}
const partLabel = (mode, p) => mode === 2 ? String(p + 1) : 'LR'[p];
const channelInputs = Array.from({length: 4}, (_, p) => $('qch' + p));
function drawChannels() {
  const editable = bi.mode === 2 || (bi.mode === 1 && routingOK);
  $('quadchannels').hidden = !editable;
  if (!editable) return;
  const draft = channelDrafts.get(bi.mode);
  const labels = bi.mode === 2 ? ['1 · Synth 1', '2 · Synth 2', '3 · MIDI 1', '4 · MIDI 2'] : ['Part L', 'Part R'];
  channelInputs.forEach((input, p) => {
    input.parentElement.hidden = p >= labels.length;
    input.disabled = busy || p >= labels.length;
    if (p >= labels.length) return;
    input.previousElementSibling.textContent = labels[p];
    const value = draft?.[p]?.value ?? String(bi.ch[p]);
    if (input.value !== value) input.value = value;
  });
  const edited = draft?.some(Boolean);
  $('quadapply').disabled = busy || !edited;
  $('channelreset').hidden = !edited;
  $('channelreset').disabled = busy;
  const changed = draft?.some((field, p) => field && field.before !== bi.ch[p]);
  $('channelstatus').textContent = changed ? `FM-1 channels are now ${bi.ch.join(', ')}. Your edits are still here.` :
    edited ? (bi.layer ? 'Apply channels stops notes on both layered parts.' :
      'Apply channels stops notes on the parts whose channels change.') : '';
}
function drawParts() {
  const show = !!(output && partsOK && bi); $('parts').hidden = !show; if (!show) return;
  for (const b of document.querySelectorAll('[data-mode]')) { b.setAttribute('aria-pressed', String(+b.dataset.mode === bi.mode)); b.disabled = busy; b.hidden = +b.dataset.mode === 2 && !quadOK; }
  for (const b of document.querySelectorAll('[data-layout]')) {
    b.setAttribute('aria-pressed', String(+b.dataset.layout === bi.layout));
    b.disabled = busy;
    b.hidden = +b.dataset.layout === 2 && (bi.mode === 2 || routingOK);
  }
  $('layoutseg').hidden = !bi.on;
  $('layer').hidden = !routingOK || bi.mode !== 1;
  $('layer').disabled = busy;
  $('layer').setAttribute('aria-pressed', String(bi.layer));
  const plays = p => bi.slot[p] >= 0 ? `${pad(bi.slot[p] + 1)} ${esc(base[bi.slot[p]]?.name || '')}` : 'a built-in or factory sound';
  const count = bi.mode === 2 ? 4 : bi.mode === 1 ? 2 : 1;
  $('partinfo').innerHTML = bi.on ? Array.from({length: count}, (_, p) => {
    const color = bi.layout === 0 ? (p < (bi.mode === 2 ? 2 : 1) ? 'tl' : 'tr') : 'part-label';
    return `<span><b class="${color}">${partLabel(bi.mode, p)}</b> ch ${bi.ch[p]} · ${plays(p)}${p === bi.focus ? ' <strong>· panel</strong>' : ''}</span>`;
  }).join('') : `<span>Omni MIDI · Plays ${plays(0)}</span>`;
  const shared = bi.on && (bi.mode === 2 || routingOK) ? [...new Set(bi.ch)].map(ch => {
    const parts = bi.ch.flatMap((value, p) => value === ch ? [partLabel(bi.mode, p)] : []);
    return parts.length > 1 ? `Parts ${parts.join(', ')} share ch ${ch}` : '';
  }).filter(Boolean) : [];
  const split = bi.on && bi.layout === 0 ? (bi.mode === 2 ? 'Left 1+2 · Right 3+4' : 'Part L left · Part R right') : '';
  const layer = bi.on && bi.layer ? 'Notes play both parts · Other MIDI follows its channel' : '';
  $('part-routing').textContent = [split, layer, ...shared].filter(Boolean).join(' · ');
  $('part-routing').hidden = !$('part-routing').textContent;
  drawChannels();
}
function drawPair() {
  $('pairbar').hidden = !pinned; if (!pinned) return;
  $('pairname').textContent = pinned.name;
  $('pairadd').disabled = busy || !lastR || lastR === pinned; $('pairadd').textContent = lastR && lastR !== pinned ? `Add pair: ${pinned.name} + ${lastR.name}` : 'Add pair';
}
async function connect() {
  if (busy) return; busy = true; setConnected(); refresh();
  try {
    if (!navigator.requestMIDIAccess) throw new Error('Use Chrome or Edge on a computer for USB MIDI. You can still browse, preview and import sounds.');
    say('Allow MIDI with SysEx when the browser asks…'); access = access || await navigator.requestMIDIAccess({sysex: true});
    input = output = null; for (const i of access.inputs.values()) i.onmidimessage = onMessage;
    say('Looking for an FM-1 running SY1…');
    for (const out of access.outputs.values()) { const a = await ask(out, cmd(0x03), 0x43, 700); if (a) { output = out; input = a.port; break; } }
    if (!output) throw new Error('No FM-1 running SY1 answered. Connect it by USB, switch it on and install SY1.');
    access.onstatechange = () => { if (output?.state === 'disconnected' || input?.state === 'disconnected') {
      output = input = null; cancelled = true; if (waiting) { const w = waiting; waiting = null; clearTimeout(w.timer); w.done(null); }
      setConnected(); refresh(); say('FM-1 disconnected. Your staged changes are kept; connect again to sync.', true);
    } };
  } catch (e) { say(e.message, true); }
  finally { busy = false; setConnected(); refresh(); }
  if (output) await readDevice();
}
async function readDevice() {
  if (!output || busy) return; busy = true; setConnected(); refresh();
  try {
    const a = await askAgain(output, cmd(0x03), 0x43, 1500); if (!a) throw new Error('The FM-1 did not answer. Reconnect before syncing.');
    const used = Math.min(SLOTS, a.data[6] | a.data[7] << 7), device = new Array(SLOTS).fill(null);
    // Only an SY1 that announces the record read gets command 07; 0.3.1 and earlier report names only.
    const old = !(a.data.length >= 12 && a.data[10] & 1);
    partsOK = a.data.length >= 12 && !!(a.data[10] & 2);
    quadOK = a.data.length >= 12 && !!(a.data[10] & 4);
    routingOK = a.data.length >= 12 && !!(a.data[10] & 8);
    for (let s = 0; s < used; s++) {
      if (!output) throw new Error('FM-1 disconnected.');
      if (s % 8 === 0) say(`Reading slot ${s + 1} of ${used}…`);
      if (old) {
        const r = await askAgain(output, cmd(0x04, ...slotBytes(s)), 0x44, 800);
        if (!r) throw new Error('Could not read the patch slots. Install the latest SY1 and reconnect.');
        if (!r.data[8]) continue;
        const name = String.fromCharCode(...r.data.slice(9, -1)) || '(no name)';
        device[s] = newItem(name, Math.max(0, CATEGORIES.indexOf(category(name))), {locked: true});
      } else {
        const p0 = await askAgain(output, cmd(0x07, ...slotBytes(s), 0), 0x47, 800, 0);
        if (!p0) throw new Error('Could not read the patch slots. Reconnect and try again.');
        if (!p0.data[9]) continue;
        const p1 = await askAgain(output, cmd(0x07, ...slotBytes(s), 1), 0x47, 800, 1);
        const lo = unpack7(p0.data, 10, p0.data.length - 1, PART), hi = p1 && p1.data[9] ? unpack7(p1.data, 10, p1.data.length - 1, RECORD - PART) : null;
        if (!lo || !hi) throw new Error(`Slot ${pad(s + 1)} came back incomplete. Reconnect and try again.`);
        const rec = new Uint8Array(RECORD); rec.set(lo); rec.set(hi, PART); device[s] = fromRecord(rec);
      }
    }
    legacy = old; adopt(device);
    if (partsOK) await readParts();
    say(`Connected: ${output.name}. ${num(device.filter(Boolean).length)} patches read.`);
  } catch (e) { say(e.message, true); output = input = null; }
  finally { busy = false; setConnected(); refresh(); }
}
/** The FM-1 as just read becomes the baseline. Staged work survives when the FM-1 is as we last knew it;
 *  otherwise only the newly added sounds are kept, after its last patch. */
function adopt(device) {
  const same = device.every((d, s) => (d ? sig(d) : '') === (base[s] ? sig(base[s]) : ''));
  if (same && !device.some((d, s) => d && d.locked !== base[s].locked)) return;
  const fresh = slots.filter(it => it && !baseSlot.has(it.uid)), dropped = changes(base, slots).total > fresh.length;
  base = device; slots = device.slice(); history = []; dsel.clear();
  if (fresh.length) { const next = appendSlots(slots, fresh); if (next) slots = next; else toast(`Only ${SLOTS - device.filter(Boolean).length} slots are free: the sounds you added do not fit.`); }
  if (dropped) toast('Your FM-1 holds other patches than before. Staged moves were dropped; the sounds you added are kept.');
}

// ---- staging
function fromLibrary(list) {
  const fresh = list.filter(p => !libSlot.has(p.id));
  if (fresh.length < list.length) toast(`${list.length - fresh.length} already on your FM-1, skipped.`);
  return fresh.map(p => {
    const name = asciiName(p.name);
    return newItem(name, p.cat, {entry: p, explicit: category(name) !== CATEGORIES[p.cat]});
  });
}
const selectedEntries = () => { const seen = new Set(lview.filter(p => lsel.has(p.id))); return [...seen, ...[...lsel].map(id => byId.get(id)).filter(p => p && !seen.has(p))]; };
function addSelected() {
  const items = fromLibrary(selectedEntries()); if (!items.length) return;
  const next = appendSlots(slots, items);
  if (!next) return toast(`Only ${SLOTS - slots.filter(Boolean).length} slots are free. Remove some patches first.`);
  if (commit(next)) { lsel.clear(); refresh(); toast(`${items.length} staged${output ? '. Press Sync to write them.' : '. Connect your FM-1 to sync.'}`); }
}
const selectedSlots = () => [...dsel].map(u => curSlot.get(u)).sort((a, b) => a - b);
const lastUsed = () => { let last = -1; slots.forEach((it, s) => { if (it) last = s; }); return last; };
function edit(s, change) {
  const next = slots.slice(), it = {...slots[s], ...change};
  if (change.name && !it.explicit) it.cat = CATEGORIES.indexOf(category(it.name));
  next[s] = it; commit(next);
}
function pick(sel, keys, k, e, anchor) {
  const key = keys[k]; if (key == null) return anchor;
  if (e.shiftKey) { for (let i = Math.min(anchor, k); i <= Math.max(anchor, k); i++) if (keys[i] != null) sel.add(keys[i]); return anchor; }
  if (e.target.dataset.act === 'pick' || e.ctrlKey || e.metaKey) sel.has(key) ? sel.delete(key) : sel.add(key);
  else { sel.clear(); sel.add(key); }
  return k;
}
const devKeys = () => dview.map(s => slots[s] ? slots[s].uid : null);

$('edit-patch').onclick = () => {
  const p = byId.get([...lsel][0]);
  if (!p || lsel.size !== 1) return;
  const args = new URLSearchParams();
  if (p.bytes) {
    const url = URL.createObjectURL(new Blob([p.bytes]));
    args.set('file', url); args.set('name', p.name);
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  } else args.set('preset', p.id);
  window.open('../editor/#' + args, '_blank', 'noopener');
};

$('llist').addEventListener('click', e => {
  const row = e.target.closest('.prow'); if (!row) return; const k = +row.dataset.k, p = lview[k], act = e.target.dataset.act;
  if (act === 'fav') { favorites.has(p.id) ? favorites.delete(p.id) : favorites.add(p.id); store('sy1-favorites', [...favorites]); return filterLib(); }
  if (act === 'play') return preview(p);
  if (act === 'pin') { pinned = pinned === p ? null : p; if (pinned) lastR = null; drawLib(); drawPair();
    if (pinned) $('preview-status').textContent = `${p.name} is part L. ▶ another sound to hear it as part R with it.`; return; }
  lanchor = pick(lsel, lview.map(x => x.id), k, e, lanchor); filterLib();
});
$('llist').addEventListener('keydown', e => {
  if (e.target !== $('llist') || !lview.length) return;
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault(); lanchor = Math.max(0, Math.min(lview.length - 1, lsel.size ? lanchor + (e.key === 'ArrowDown' ? 1 : -1) : 0));
    lsel.clear(); lsel.add(lview[lanchor].id);
    const el = $('llist'), top = lanchor * ROW; if (top < el.scrollTop) el.scrollTop = top; else if (top + ROW > el.scrollTop + el.clientHeight) el.scrollTop = top + ROW - el.clientHeight;
    filterLib();
  } else if ((e.key === ' ' || e.key === 'Enter') && lsel.size === 1) { e.preventDefault(); preview(byId.get([...lsel][0])); }
});
$('dlist').addEventListener('click', e => {
  const row = e.target.closest('.prow'); if (!row || e.target.matches('select, .rename')) return; const k = +row.dataset.k, s = dview[k];
  if (e.target.dataset.act === 'del') { const next = slots.slice(); next[s] = null; return void commit(next); }
  if (e.target.dataset.act === 'part') { const p = +e.target.dataset.part, it = slots[s];
    return void partsDo(cmd(0x09, p, ...slotBytes(s)), 0x49, p, () => `${bi.on ? 'Part ' + (bi.mode === 2 ? p + 1 : 'LR'[p]) : 'The FM-1'} now plays ${pad(s + 1)} ${it.name}.`); }
  // The second click of a double-click: the row was redrawn by the first, so a dblclick event would hit a dead node.
  if (e.detail === 2 && e.target.closest('.nm')) return rename(row, s);
  danchor = pick(dsel, devKeys(), k, e, danchor); filterDev();
});
$('dlist').addEventListener('change', e => {
  const row = e.target.closest('.prow'); if (!row || e.target.dataset.act !== 'cat') return;
  edit(dview[+row.dataset.k], {cat: +e.target.value, explicit: true});
});
function rename(row, s) {
  const nm = row.querySelector('.nm'); if (!nm || !slots[s] || slots[s].locked || busy) return;
  const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'rename'; inp.maxLength = 23; inp.value = slots[s].name; inp.setAttribute('aria-label', 'Rename patch');
  let done = false; const end = ok => { if (done) return; done = true; const v = asciiName(inp.value.trim()); if (ok && v && v !== slots[s].name) edit(s, {name: v}); else filterDev(); };
  inp.onkeydown = ev => { ev.stopPropagation(); if (ev.key === 'Enter') end(true); if (ev.key === 'Escape') end(false); }; inp.onblur = () => end(true);
  nm.replaceWith(inp); inp.focus(); inp.select();
}
for (const [el, set] of [[$('lchips'), v => { lcat = v; }], [$('dchips'), v => { dcat = v; }]]) el.addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  if (b.dataset.tog === 'fav') lfav = !lfav; else if (b.dataset.tog === 'new') lnew = !lnew; else if (b.dataset.tog === 'factory') lfactory = !lfactory; else set(+b.dataset.cat);
  if (el === $('lchips')) { $('llist').scrollTop = 0; filterLib(); } else { $('dlist').scrollTop = 0; filterDev(); }
});
for (const id of ['search', 'bank', 'lsort']) $(id).addEventListener(id === 'search' ? 'input' : 'change', () => { $('llist').scrollTop = 0; filterLib(); });
for (const id of ['dq', 'dempty', 'dpend']) $(id).addEventListener(id === 'dq' ? 'input' : 'change', () => { $('dlist').scrollTop = 0; filterDev(); });
$('add').onclick = addSelected;
for (const b of document.querySelectorAll('[data-mode]')) b.onclick = () => { const on = +b.dataset.mode; if (!bi || on === bi.mode) return;
  partsDo(cmd(0x08, on, 0x7f), 0x48, 0, () => on === 2 ? `Quad: four parts on channels ${bi.ch.join(', ')}.` : on ? `Bitimbral: part L listens on channel ${bi.ch[0]}, part R on ${bi.ch[1]}.` : 'Standard: one sound on every channel.'); };
for (const b of document.querySelectorAll('[data-layout]')) b.onclick = () => {
  const value = +b.dataset.layout;
  if (!bi || value === bi.layout) return;
  if (bi.mode === 1 && routingOK) partsDo(cmd(0x0b, value, 0x7f, 0x7f, 0x7f), 0x4b, 0, () => `Audio routing: ${LAYOUTS[value]}.`);
  else partsDo(cmd(0x08, 0x7f, value), 0x48, 0, () => `Output: ${LAYOUTS[value]}.`);
};
$('layer').onclick = () => {
  if (bi?.mode === 1 && routingOK) partsDo(cmd(0x0b, 0x7f, +!bi.layer, 0x7f, 0x7f), 0x4b, 0,
    () => bi.layer ? 'Layer on. Notes play both parts.' : 'Layer off.');
};
channelInputs.forEach((input, p) => {
  input.oninput = () => {
    if (!bi?.on) return;
    const draft = channelDrafts.get(bi.mode) || [];
    const before = draft[p]?.before ?? bi.ch[p];
    draft[p] = input.value === String(bi.ch[p]) ? null : {value: input.value, before};
    channelDrafts.set(bi.mode, draft);
    drawChannels();
  };
});
$('channelreset').onclick = () => { if (bi) { channelDrafts.delete(bi.mode); drawChannels(); } };
$('quadchannels').onsubmit = e => {
  e.preventDefault();
  if (!bi?.on || $('quadapply').disabled) return;
  const mode = bi.mode, draft = channelDrafts.get(mode);
  if (draft.some(field => field && (!/^[0-9]+$/.test(field.value) || +field.value < 1 || +field.value > 16))) return;
  const channels = bi.ch.map((_, p) => draft[p] ? Number(draft[p].value) - 1 : 0x7f);
  const frame = mode === 2 ? cmd(0x0a, 0x7f, 0x7f, ...channels) : cmd(0x0b, 0x7f, 0x7f, ...channels);
  partsDo(frame, mode === 2 ? 0x4a : 0x4b, 0, () => `FM-1 MIDI channels: ${bi.ch.join(', ')}.`,
    () => channelDrafts.delete(mode));
};
$('pairoff').onclick = () => { pinned = lastR = null; drawLib(); drawPair(); $('preview-status').textContent = '▶ plays a sound here in the page.'; };
$('pairadd').onclick = () => {
  if (!pinned || !lastR) return; const items = fromLibrary([pinned, lastR]); if (!items.length) return;
  const next = appendSlots(slots, items); if (!next) return toast('Not enough free slots for the pair.');
  if (commit(next)) toast(output && bi?.mode === 2 ?
    'Pair staged. After syncing, assign the two sounds with the numbered part buttons.' :
    output && bi?.mode === 1 ? 'Pair staged. After syncing, assign the two sounds to L and R.' :
    'Pair staged. Sync the slots, then choose Bitimbral or Quad to play the sounds together.');
};
// Back from the FM-1's panel: what the parts play may have changed there.
addEventListener('focus', () => { if (!output || !partsOK || busy || waiting) return; busy = true; readParts().catch(() => {}).finally(() => { busy = false; refresh(); }); });
$('map').addEventListener('click', e => {
  const s = e.target.dataset.s; if (s === undefined) return; const it = slots[+s];
  if (!it) return toast(`Slot ${pad(+s + 1)} is empty.`);
  dsel.clear(); dsel.add(it.uid); filterDev();
  const k = dview.indexOf(+s); if (k >= 0) { $('dlist').scrollTop = Math.max(0, k * devRowHeight() - 72); danchor = k; drawDev(); }
});
$('remove').onclick = () => { const next = slots.slice(); for (const s of selectedSlots()) next[s] = null; commit(next); };
$('desel').onclick = () => { dsel.clear(); filterDev(); };
$('move').onclick = () => { if (legacy) return; const t = Math.min(SLOTS, Math.max(1, parseInt($('moveto').value, 10) || 1)) - 1, ss = selectedSlots(); commit(insertAt(slots, t, ss.map(s => slots[s]), ss)); };
for (const b of document.querySelectorAll('[data-arr]')) b.onclick = () => {
  $('m-arr').open = false; if (legacy) return;
  if (commit(b.dataset.arr === 'clear' ? new Array(SLOTS).fill(null) : arrange(slots, b.dataset.arr, bankOf))) toast('Staged. Check the new layout, then sync.');
};

// ---- sets: saved layouts, kept in this browser and exportable as a file
function snapshot(name, from) {
  const list = []; let skipped = 0;
  from.forEach((it, s) => { if (!it) return; const o = {s, n: it.name, c: it.explicit ? it.cat : -1};
    if (it.rec) o.r = b64(it.rec); else if (it.entry && !banks.get(it.entry.bank).local) o.e = it.entry.id; else { skipped++; return; }
    list.push(o); });
  return {set: {sy1set: 1, name, when: Date.now(), slots: list}, skipped};
}
function stageSet(set) {
  const pool = new Map(), out = new Array(SLOTS).fill(null), later = []; let missing = 0;
  for (const it of new Set([...base, ...slots])) if (it && !it.locked) { const k = sig(it); pool.set(k, [...(pool.get(k) || []), it]); }
  const take = (k, prefer) => { const a = pool.get(k); if (!a || !a.length) return null; const i = a.indexOf(prefer); return a.splice(i >= 0 ? i : 0, 1)[0]; };
  for (const o of Array.isArray(set.slots) ? set.slots : []) {
    if (!Number.isInteger(o.s) || o.s < 0 || o.s >= SLOTS || typeof o.n !== 'string') { missing++; continue; }
    let rec = null, entry = null; try { if (o.r) { rec = unb64(o.r); if (rec.length !== RECORD) rec = null; } else entry = byId.get(o.e) || null; } catch { rec = null; }
    if (!rec && !entry) { missing++; continue; }
    const explicit = Number.isInteger(o.c) && o.c >= 0 && o.c < CATEGORIES.length, name = asciiName(o.n), it = {name, explicit, cat: explicit ? o.c : Math.max(0, CATEGORIES.indexOf(category(name))), rec, entry: entry || (rec && byId.get(known[recordKey(rec)])) || null, locked: false};
    const k = sig(it), here = base[o.s] && sig(base[o.s]) === k ? take(k, base[o.s]) : null;
    if (here) out[o.s] = here; else later.push([o.s, k, it]);
  }
  for (const [s, k, it] of later) out[s] = take(k) || {...it, uid: uid++};
  return {out, missing};
}
function drawSets() {
  $('setlist').innerHTML = sets.map((s, i) => `<div class="setrow"><button type="button" class="load" data-set="${i}">${esc(s.name)}<span>${s.slots.length} patches · ${new Date(s.when).toLocaleDateString()}</span></button><button type="button" data-export="${i}" aria-label="Export ${esc(s.name)} as a file" title="Export as a file">⤓</button><button type="button" data-drop="${i}" aria-label="Delete ${esc(s.name)}" title="Delete">✕</button></div>`).join('') || '<p>No sets saved yet.</p>';
}
function keepSet(set, replace = false) {
  const before = sets; sets = replace ? [set, ...sets.filter(s => s.name !== set.name)] : [...sets, set];
  if (!store('sy1-sets', sets)) { sets = before; toast('This browser could not store the set. Export it as a file instead.'); return false; }
  drawSets(); return true;
}
$('setsave').onclick = () => {
  const {set, skipped} = snapshot($('setname').value.trim() || 'Layout ' + (sets.length + 1), slots);
  if (keepSet(set)) { $('setname').value = ''; toast(`Saved “${set.name}”.` + (skipped ? ` ${skipped} unsynced local sounds are not in it; sync first to include them.` : '')); }
};
$('setlist').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  if (b.dataset.set !== undefined) { const s = sets[+b.dataset.set], {out, missing} = stageSet(s); $('m-sets').open = false;
    if (commit(out)) toast(`“${s.name}” staged.` + (missing ? ` ${missing} patches could not be restored.` : '') + (output ? ' Sync to load it onto the FM-1.' : '')); }
  else if (b.dataset.drop !== undefined) { sets = sets.filter((_, i) => i !== +b.dataset.drop); store('sy1-sets', sets); drawSets(); }
  else if (b.dataset.export !== undefined) { const s = sets[+b.dataset.export], a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(s)], {type: 'application/json'})); a.download = 'sy1-set-' + s.name.replace(/[^\w-]+/g, '-').toLowerCase() + '.json'; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000); }
});
$('setimport').onclick = () => $('setfile').click();
$('setfile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try { const s = JSON.parse(await f.text()); if (s.sy1set !== 1 || !Array.isArray(s.slots)) throw new Error();
    if (keepSet({sy1set: 1, name: String(s.name || f.name).slice(0, 40), when: Number(s.when) || Date.now(), slots: s.slots})) toast('Set imported. Load it from the list to stage it.'); }
  catch { toast('That file is not an SY1 set.'); }
};
document.addEventListener('click', e => { for (const m of document.querySelectorAll('#pm details.menu[open]')) if (!m.contains(e.target)) m.open = false; });

// ---- sync: the staged layout onto the FM-1, each slot acknowledged
async function finalRecord(it) {
  let rec = it.rec;
  if (!rec) { const parsed = parseSy1(await patchBytes(it.entry)); if (!parsed.lines) throw new Error('Invalid patch: ' + it.name); rec = recordFrom(parsed.values, it.name); }
  if (it.name !== recordName(rec)) rec = withMeta(rec, it.name);
  if (it.explicit && (rec[2] !== it.cat + 1 || rec[3] !== 0xc1)) rec = withMeta(rec, null, it.cat);
  return rec;
}
function remember(it) {
  if (!it.entry || banks.get(it.entry.bank).local) return;
  known[recordKey(it.rec)] = it.entry.id; const keys = Object.keys(known); if (keys.length > 4000) delete known[keys[0]]; store('sy1-known', known);
}
async function sync() {
  const ch = changes(base, slots); if (!ch.total || busy || !output) return;
  if (ch.removed && !confirm(`${ch.removed} ${ch.removed === 1 ? 'patch' : 'patches'} will be removed from the FM-1. Built-in and factory sounds stay. Continue?`)) return;
  busy = true; cancelled = false; refresh(); setConnected();
  const bar = $('progress'), result = $('result'); bar.hidden = false; bar.max = ch.total; bar.value = 0; $('cancel').hidden = false; $('cancel').disabled = false; result.className = '';
  let done = 0, error = '';
  try {
    if (!legacy && (ch.removed || ch.moved || ch.edited) && base.some(Boolean)) keepSet(snapshot('Auto-backup before last sync', base).set, true);
    const recs = new Map();                     // every record resolved before the first hardware write
    for (const s of ch.writes) { result.textContent = `Preparing ${recs.size + 1} / ${ch.writes.length}: ${slots[s].name}`; recs.set(s, await finalRecord(slots[s])); }
    if (!slots.some(Boolean)) {
      result.textContent = 'Erasing all user slots…';
      const r = await ask(output, cmd(0x02), 0x42, 15000); if (!r || r.data[6] !== 0) throw new Error('The FM-1 did not confirm the erase. Read it again before retrying.');
      base = new Array(SLOTS).fill(null); done = ch.total;
    } else for (const s of [...ch.writes, ...ch.clears]) {
      if (cancelled) break; if (!output) throw new Error('FM-1 disconnected.');
      const it = slots[s]; result.textContent = `${it ? 'Writing' : 'Emptying'} slot ${pad(s + 1)} · ${done + 1} / ${ch.total}${it ? ': ' + it.name : ''}`;
      const r = it ? await askAgain(output, packFrame(recs.get(s), s), 0x41, 5000) : await askAgain(output, cmd(0x05, ...slotBytes(s)), 0x45, 5000);
      if (!r || r.data[8] !== 0) throw new Error(`The FM-1 did not confirm slot ${pad(s + 1)}. Read it again before retrying.`);
      if (it) { it.rec = recs.get(s); remember(it); }
      base = base.slice(); base[s] = it; bar.value = ++done;
    }
  } catch (e) { error = e.message; }
  finally {
    if (output && partsOK && done) await readParts();
    busy = false; bar.hidden = true; $('cancel').hidden = true;
    const left = changes(base, slots).total;
    result.textContent = `${num(done)} slots written and confirmed.` + (left ? ` ${num(left)} still staged.` : '') + (cancelled && left ? ' Sync stopped.' : '') + (error ? ' ' + error : '');
    result.className = error ? 'bad' : '';
    if (!left) history = [];
    setConnected(); refresh();
  }
}
$('undo').onclick = () => { if (!history.length || busy) return; slots = history.pop(); refresh(); };
$('discard').onclick = () => commit(base.slice());
$('sync').onclick = sync;
$('cancel').onclick = () => { cancelled = true; $('cancel').disabled = true; $('result').textContent = 'Stopping after the current slot is confirmed…'; };
$('connect').onclick = () => output ? readDevice() : connect();

// ---- drag: library sounds onto a slot, slots to reorder, files to import
$('llist').addEventListener('dragstart', e => {
  const row = e.target.closest('.prow'); if (!row || busy) return e.preventDefault(); const p = lview[+row.dataset.k];
  // No redraw here: replacing the row being dragged would cancel the drag.
  if (!lsel.has(p.id)) { lsel.clear(); lsel.add(p.id); for (const r of document.querySelectorAll('#llist .prow.sel')) r.classList.remove('sel'); row.classList.add('sel'); }
  drag = 'lib'; e.dataTransfer.setData('text/plain', 'sy1'); e.dataTransfer.effectAllowed = 'copy';
});
$('dlist').addEventListener('dragstart', e => {
  const row = e.target.closest('.prow'), it = row && slots[dview[+row.dataset.k]]; if (!it || busy || legacy) return e.preventDefault();
  if (!dsel.has(it.uid)) { dsel.clear(); dsel.add(it.uid); }
  drag = 'dev'; e.dataTransfer.setData('text/plain', 'sy1'); e.dataTransfer.effectAllowed = 'move';
});
let mark = null; const unmark = () => { if (mark) mark.classList.remove('dropat'); mark = null; };
const dropSlot = e => { const row = e.target.closest('#dlist .prow'), cell = e.target.closest('#map i'); return row ? [dview[+row.dataset.k], row] : cell ? [+cell.dataset.s, cell] : null; };
for (const el of [$('dlist'), $('map')]) {
  el.addEventListener('dragover', e => { if (!drag) return; e.preventDefault(); e.stopPropagation(); const d = dropSlot(e); if (d && d[1] !== mark) { unmark(); mark = d[1]; mark.classList.add('dropat'); } });
  el.addEventListener('dragleave', e => { if (!el.contains(e.relatedTarget)) unmark(); });
  el.addEventListener('drop', e => {
    if (!drag) return; e.preventDefault(); e.stopPropagation(); const d = dropSlot(e), target = d ? d[0] : Math.min(SLOTS - 1, lastUsed() + 1); unmark();
    if (drag === 'lib') { const items = fromLibrary(selectedEntries()); if (items.length && commit(insertAt(slots, target, items, [], !legacy))) { lsel.clear(); refresh(); } }
    else { const ss = selectedSlots(); commit(insertAt(slots, target, ss.map(s => slots[s]), ss)); }
    drag = null;
  });
}
document.addEventListener('dragend', () => { drag = null; unmark(); });
const hasFiles = e => !drag && [...(e.dataTransfer?.types || [])].includes('Files');
document.body.addEventListener('dragover', e => { if (hasFiles(e)) e.preventDefault(); });
document.body.addEventListener('drop', async e => {
  if (!hasFiles(e)) return; e.preventDefault();
  const es = [...e.dataTransfer.items].map(i => i.webkitGetAsEntry?.()).filter(Boolean); if (!es.length) return addFiles(e.dataTransfer.files);
  const files = []; try { for (const en of es) await entryFiles(en, files); await addFiles(files); } catch (err) { note(err.message); }
});
$('pick').onclick = () => $('files').click(); $('pickdir').onclick = () => $('dir').click();
for (const id of ['files', 'dir']) $(id).onchange = e => { addFiles(e.target.files); e.target.value = ''; };

document.addEventListener('keydown', e => {
  if (e.target.matches('input, select, textarea')) { if (e.key === 'Escape') e.target.blur(); return; }
  const mod = e.ctrlKey || e.metaKey, key = e.key.toLowerCase();
  if (e.key === '/') { e.preventDefault(); ($('pm').dataset.tab === 'dev' ? $('dq') : $('search')).focus(); }
  else if (mod && key === 'z') { e.preventDefault(); $('undo').click(); }
  else if ((e.key === 'Delete' || e.key === 'Backspace') && dsel.size && $('dev').contains(document.activeElement)) { e.preventDefault(); $('remove').click(); }
  else if (e.key === 'Escape') { lsel.clear(); dsel.clear(); refresh(); }
  else if (mod && key === 'a' && $('pm').contains(document.activeElement)) { e.preventDefault();
    if ($('dev').contains(document.activeElement)) { for (const u of devKeys()) if (u != null) dsel.add(u); filterDev(); } else { for (const p of lview) lsel.add(p.id); filterLib(); } }
});
function tab(t) { $('pm').dataset.tab = t; $('tab-lib').setAttribute('aria-pressed', t === 'lib'); $('tab-dev').setAttribute('aria-pressed', t === 'dev'); drawLib(); drawDev(); }
$('tab-lib').onclick = () => tab('lib'); $('tab-dev').onclick = () => tab('dev');
addEventListener('resize', () => { drawLib(); drawDev(); });
addEventListener('beforeunload', e => { if (changes(base, slots).total && output) e.preventDefault(); });

drawSets(); setConnected(); refresh(); catalogue();
