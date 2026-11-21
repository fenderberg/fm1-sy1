import {parseSy1, unzipSy1} from '../emu/app_own.js';
import {patchName, fold} from '../manager/library.js';
import t from './tables.js';
import {controls} from './controls.js?v=classic';
import {keyboard} from './keyboard.js';
import {exportPatch} from './patch.js';

const $ = id => document.getElementById(id);
let values = t.param_def.slice(), original = null, source = 'Initial patch', dirty = false;
let request = 0, catalogue, localBank;
const report = (text, error = false) => { $('message').textContent = text; $('message').classList.toggle('error',error); };
const sound = keyboard(() => values, report);
const panel = controls(() => values, (id,n) => {
  if (values[id] === n) return;
  values[id] = n;
  sound.send({type:'param',id,value:n});
  modified(true); panel.update();
});
function modified(on) {
  dirty = on; $('source').textContent = source + (on ? ' · Edited' : '');
}
function replace(name, next, origin, raw = null) {
  if (dirty && !confirm('Replace the edited patch? Save a .sy1 file to keep your changes.')) return false;
  sound.stop();
  values = next; original = raw; source = origin;
  $('patch-name').value = name; modified(false);
  sound.send({type:'patch',values}); panel.render();
  return true;
}
function loadBytes(bytes, filename, origin) {
  const p = parseSy1(bytes);
  if (!p.lines) throw Error('This file contains no Synth1 patch parameters.');
  const name = patchName(p,filename);
  if (replace(name,p.values,origin,{bytes,name,version:p.version})) {
    report('Loaded ' + name + '.'); return true;
  }
  return false;
}
$('patch-name').oninput = () => modified(true);
$('init').onclick = () => { request++; if (replace('Init',t.param_def.slice(),'Initial patch')) report('Initial patch loaded.'); };
$('save').onclick = () => {
  const name = $('patch-name').value.trim() || 'Untitled';
  $('patch-name').value = name;
  const url = URL.createObjectURL(exportPatch(name,values,original));
  const link = document.createElement('a');
  link.href = url; link.download = (name.replace(/[<>:"/\\|?*\x00-\x1f]/g,'_') || 'Patch') + '.sy1';
  link.click(); setTimeout(() => URL.revokeObjectURL(url),60000);
  modified(false); report('Downloaded ' + link.download + '.');
};
window.addEventListener('beforeunload', e => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

async function catalog() {
  if (!catalogue) catalogue = fetch('../library/catalog.json?v=categories13').then(r => {
    if (!r.ok) throw Error('Could not load the community library.');
    return r.json();
  }).then(data => data.banks).catch(e => { catalogue = null; throw e; });
  return catalogue;
}
let available = [], shown = [], cachedBank, cachedEntries;
function fillPresets() {
  const bank = available[$('banks').selectedIndex], q = fold($('find').value.trim());
  shown = bank ? bank.patches.filter(p => fold(p.name + ' ' + (p.category || '')).includes(q)) : [];
  $('presets').replaceChildren(...shown.map((p,i) => new Option(p.name,String(i))));
  if (shown.length) $('presets').selectedIndex = 0;
  $('load').disabled = !shown.length;
  $('bank-info').textContent = bank?.terms ? bank.author + ' · ' + bank.terms : '';
  $('library-status').textContent = shown.length ? '' : 'No matching patches.';
}
async function showBrowser(bank = null) {
  const generation = ++request;
  $('library').showModal();
  $('find').value = ''; $('banks').replaceChildren(); $('presets').replaceChildren();
  $('load').disabled = true; $('bank-info').textContent = ''; $('library-status').textContent = 'Loading presets…';
  try {
    const banks = bank ? [bank] : [...(localBank ? [localBank] : []), ...await catalog()];
    if (generation !== request) return;
    available = banks;
    $('banks').replaceChildren(...banks.map((b,i) => new Option(b.name,String(i))));
    fillPresets(); $('find').focus();
  } catch (e) { if (generation === request) $('library-status').textContent = e.message; }
}
async function bytesFor(bank,p) {
  if (p.bytes) return p.bytes;
  if (cachedBank !== bank) {
    cachedBank = bank;
    cachedEntries = fetch('../library/' + bank.file).then(async r => {
      if (!r.ok) throw Error('Could not download this bank.');
      return new Map((await unzipSy1(await r.arrayBuffer())).map(x => [x.name,x.bytes]));
    }).catch(e => { if (cachedBank === bank) cachedBank = null; throw e; });
  }
  const bytes = (await cachedEntries).get(p.path);
  if (!bytes) throw Error('The selected patch is missing from this bank.');
  return bytes;
}
async function selectPreset() {
  const bank = available[$('banks').selectedIndex], p = shown[$('presets').selectedIndex];
  if (!p) return;
  const generation = ++request;
  $('load').disabled = true; $('library-status').textContent = 'Loading ' + p.name + '…';
  try {
    const bytes = await bytesFor(bank,p);
    if (generation === request && loadBytes(bytes,p.path,bank.name)) $('library').close();
    else if (generation === request) $('library-status').textContent = '';
  } catch (e) { if (generation === request) $('library-status').textContent = e.message; }
  finally { if (generation === request) $('load').disabled = false; }
}
$('browse').onclick = () => showBrowser();
$('close').onclick = () => $('library').close();
$('library').addEventListener('close', () => { request++; });
$('find').oninput = $('banks').onchange = () => { request++; fillPresets(); };
$('presets').onchange = () => { request++; $('load').disabled = !shown.length; $('library-status').textContent = ''; };
$('load').onclick = $('presets').ondblclick = selectPreset;
$('presets').onkeydown = e => { if (e.key === 'Enter') { e.preventDefault(); selectPreset(); } };
async function openFile(file) {
  if (!file) return;
  const generation = ++request;
  try {
    if (!/\.(zip|sy1)$/i.test(file.name)) throw Error('Choose a .sy1 patch or a ZIP bank.');
    if (file.size > 64 * 1024 * 1024) throw Error('Choose a file smaller than 64 MB.');
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (generation !== request) return;
    if (/\.zip$/i.test(file.name)) {
      const parts = await unzipSy1(bytes), patches = [];
      for (const part of parts) {
        const patch = parseSy1(part.bytes);
        if (patch.lines) patches.push({name:patchName(patch,part.name),path:part.name,bytes:part.bytes});
      }
      if (generation !== request) return;
      if (!patches.length) throw Error('This ZIP contains no Synth1 patches.');
      localBank = {name:file.name,patches}; await showBrowser(localBank);
    } else loadBytes(bytes,file.name,file.name);
  } catch (e) { if (generation === request) report(e.message,true); }
}
$('open').onclick = () => $('file').click();
$('file').onchange = () => { openFile($('file').files[0]); $('file').value = ''; };
window.addEventListener('dragover',e => { if (e.dataTransfer.types.includes('Files')) e.preventDefault(); });
window.addEventListener('drop',e => {
  if (!e.dataTransfer.files.length) return;
  e.preventDefault(); if (!$('library').open) openFile(e.dataTransfer.files[0]);
});

// Manager links identify a hosted patch, or carry a same-origin blob for a local import.
async function fromManager() {
  const args = new URLSearchParams(location.hash.slice(1)), generation = ++request;
  try {
    if (args.has('preset')) {
      const banks = await catalog(), id = args.get('preset');
      const bank = banks.find(b => b.patches.some(p => p.id === id));
      if (!bank) throw Error('This preset is missing from the library.');
      const p = bank.patches.find(p => p.id === id), bytes = await bytesFor(bank,p);
      if (generation === request) loadBytes(bytes,p.path,bank.name);
    } else if (args.has('file')) {
      const url = new URL(args.get('file'));
      if (url.protocol !== 'blob:' || url.origin !== location.origin) throw Error('Invalid local patch link.');
      const r = await fetch(url); if (!r.ok) throw Error('Reopen this patch from the Patch Manager.');
      const bytes = new Uint8Array(await r.arrayBuffer());
      if (generation === request) loadBytes(bytes,args.get('name') || 'Local patch','Patch Manager import');
    }
  } catch (e) { if (generation === request) report(e.message,true); }
}
fromManager();
