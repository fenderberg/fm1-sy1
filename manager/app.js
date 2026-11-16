// Browser-local catalogue and imports. MIDI writes happen only through the explicit Send action.
import { parseSy1, frameFor, unzipSy1 } from '../emu/app_own.js';
import { CATEGORIES, category, fold, patchName, bankName, placement } from './library.js';
const $ = id => document.getElementById(id);
const HEAD = [0xf0,0x7d,0x53,0x59,0x31], SLOTS = 1024, PAGE = 48;
const pad = n => String(n).padStart(3,'0');
const element = (tag, text='', cls='') => { const e=document.createElement(tag); e.textContent=text; if(cls)e.className=cls; return e; };
let access=null,input=null,output=null,waiting=null,busy=false,importing=false,cancelled=false,used=0,page=0,localNumber=0;
let entries=[],filtered=[],queue=[],occupied=new Set(),banks=new Map();
const bankCache=new Map();
let favorites;
try { favorites=new Set(JSON.parse(localStorage.getItem('sy1-favorites')||'[]')); } catch { favorites=new Set(); }
const favoriteSave=()=>{try{localStorage.setItem('sy1-favorites',JSON.stringify([...favorites]));}catch{}};
function addEntry(p,bank) { entries.push({...p,bank:bank.id,search:fold(`${p.name} ${bank.name} ${bank.author} ${p.category}`)}); }

async function catalogue() {
  try {
    const r=await fetch('../library/catalog.json'); if(!r.ok)throw new Error('Catalogue unavailable');
    const data=await r.json();
    for(const bank of data.banks){ banks.set(bank.id,bank); for(const p of bank.patches)addEntry(p,bank); }
    refreshBanks(); filter();
  } catch { $('import-status').textContent='The online collection could not be loaded. Reload to try again, or import your own banks.'; }
}
function refreshBanks(){
  const current=$('bank').value; $('bank').replaceChildren(new Option('All banks',''));
  for(const local of [false,true]){
    const group=element('optgroup');group.label=local?'Your local imports':'Community library';
    for(const b of [...banks.values()].filter(b=>!!b.local===local).sort((a,b)=>a.name.localeCompare(b.name)))group.append(new Option(`${b.name} (${b.count})`,b.id));
    if(group.children.length)$('bank').append(group);
  }
  $('bank').value=current; $('total-patches').textContent=entries.length.toLocaleString('en-US'); $('total-banks').textContent=banks.size;
}
function filter(reset=true){
  const words=fold($('search').value).split(/\s+/).filter(Boolean),bank=$('bank').value,cat=$('category').value;
  filtered=entries.filter(p=>(!bank||p.bank===bank)&&(!cat||p.category===cat)&&(!$('favorites-only').checked||favorites.has(p.id))&&words.every(w=>p.search.includes(w)));
  if(reset)page=0; page=Math.max(0,Math.min(page,Math.ceil(filtered.length/PAGE)-1));
  const b=banks.get(bank); $('bank-info').hidden=!b;
  if(b){$('bank-credit').textContent=`${b.author} · ${b.terms}`; $('bank-notes').textContent=(b.notes||[]).map(n=>n.path+'\n\n'+n.text).join('\n\n');
    $('bank-download').hidden=!b.file; if(b.file)$('bank-download').href='../library/'+b.file;}
  render(); showQueue();
}
function render(){
  const body=$('patches'),selected=new Set(queue.map(p=>p.id)); body.replaceChildren();
  for(const p of filtered.slice(page*PAGE,(page+1)*PAGE)){
    const row=element('tr','',''+(selected.has(p.id)?'selected':''));
    const check=element('input'); check.type='checkbox'; check.checked=selected.has(p.id); check.disabled=busy||(!check.checked&&queue.length>=SLOTS); check.setAttribute('aria-label','Select '+p.name);
    check.addEventListener('change',()=>{if(check.checked){if(queue.length<SLOTS)queue.push(p);}else queue=queue.filter(q=>q.id!==p.id); render(); showQueue();});
    const select=element('td');select.append(check);
    const name=element('td',p.name,'sound-name'),bank=element('td');bank.append(element('span',banks.get(p.bank).name,'bank-name'),element('span',p.category,'category-name'));
    const star=element('button',favorites.has(p.id)?'★':'☆','favorite');star.type='button';star.setAttribute('aria-label','Favourite '+p.name);star.setAttribute('aria-pressed',String(favorites.has(p.id)));
    star.addEventListener('click',()=>{favorites.has(p.id)?favorites.delete(p.id):favorites.add(p.id); favoriteSave(); filter(false);});
    const fav=element('td');fav.append(star);
    const play=element('button','Play ↗','preview-link');play.type='button';play.setAttribute('aria-label','Preview '+p.name+' in the browser'); play.addEventListener('click',()=>preview(p,play));
    const previewCell=element('td');previewCell.append(play);row.append(select,name,bank,fav,previewCell);body.append(row);
  }
  $('empty').hidden=!!filtered.length; $('matches').textContent=`${filtered.length.toLocaleString('en-US')} sounds`;
  $('page-status').textContent=filtered.length?`${page*PAGE+1}–${Math.min((page+1)*PAGE,filtered.length)} of ${filtered.length.toLocaleString('en-US')}`:'0 sounds';
  $('previous').disabled=page===0; $('next').disabled=(page+1)*PAGE>=filtered.length;
  const fresh=filtered.filter(p=>!selected.has(p.id)).length;
  $('select-bank').textContent=fresh?`Select ${fresh.toLocaleString('en-US')} sounds`:'All matching sounds selected';
  $('select-bank').disabled=busy||!fresh||queue.length+fresh>SLOTS;
  $('select-bank').title=queue.length+fresh>SLOTS?'Choose a bank or narrow your search to select up to 1,024 sounds.':'';
}
async function patchBytes(p){
  if(p.bytes)return p.bytes;
  const bank=banks.get(p.bank);
  if(!bankCache.has(bank.id))bankCache.set(bank.id,(async()=>{
    const r=await fetch('../library/'+bank.file);if(!r.ok)throw new Error('Could not download '+bank.name);
    return new Map((await unzipSy1(await r.arrayBuffer())).map(f=>[f.name,f.bytes]));
  })().catch(e=>{bankCache.delete(bank.id);throw e;}));
  const bytes=(await bankCache.get(bank.id)).get(p.path);if(!bytes)throw new Error('Patch missing from '+bank.name);return bytes;
}
async function preview(p,button){
  // Open immediately during the gesture, before fetching, to work with popup blockers.
  const tab=window.open('about:blank','_blank'); if(tab)tab.opener=null;
  button.disabled=true;button.textContent='…';
  try {
    const bytes=await patchBytes(p),key='sy1-preview-'+crypto.randomUUID();
    localStorage.setItem(key,JSON.stringify({name:p.name,bytes:btoa(String.fromCharCode(...bytes)),created:Date.now()}));
    const url='../emu/?preview='+encodeURIComponent(key);
    if(tab)tab.location.href=url;else window.location.href=url;
  }catch(e){if(tab)tab.close();$('import-status').textContent='Preview failed: '+e.message;}
  finally{button.disabled=false;button.textContent='Play ↗';}
}
async function entryFiles(entry,out){
  if(entry.isFile)out.push(await new Promise((ok,no)=>entry.file(ok,no)));
  else if(entry.isDirectory){const r=entry.createReader();for(;;){const batch=await new Promise((ok,no)=>r.readEntries(ok,no));if(!batch.length)break;for(const e of batch)await entryFiles(e,out);}}
}
async function addFiles(files){
  if(importing)return; importing=true; $('pick').disabled=$('pickdir').disabled=true;
  let added=0,skipped=0;const errors=[],localBanks=new Map();
  const sorted=[...files].filter(f=>!f.name.startsWith('._')).sort((a,b)=>(a.webkitRelativePath||a.name).localeCompare(b.webkitRelativePath||b.name,undefined,{numeric:true}));
  try {
    for(const f of sorted){
      if(!/\.(zip|sy1)$/i.test(f.name)){skipped++;continue;}
      $('import-status').textContent=`Reading ${f.name}…`;await new Promise(r=>setTimeout(r,0));
      try{
        const raw=await f.arrayBuffer(),zip=/\.zip$/i.test(f.name),parts=zip?await unzipSy1(raw):[{name:f.webkitRelativePath||f.name,bytes:new Uint8Array(raw)}];
        if(!parts.length)skipped++;
        for(const part of parts){
          const patch=parseSy1(part.bytes);if(!patch.lines){skipped++;continue;}
          const title=patchName(patch,part.name),bn=bankName(part.name,zip?f.name.replace(/\.zip$/i,''):'Your patches');
          if(!localBanks.has(bn)){const b={id:'local-'+(++localNumber),local:true,name:bn,author:'Your local import',terms:'Use under the original author’s terms. Files stay in this browser tab.',count:0,notes:[]};localBanks.set(bn,b);banks.set(b.id,b);}
          const bank=localBanks.get(bn);bank.count++;
          addEntry({id:bank.id+':'+bank.count,name:title,path:part.name,category:category(title),bytes:part.bytes},bank);added++;
        }
      }catch(e){errors.push(f.name+': '+e.message);}
    }
    refreshBanks();$('bank').value=localBanks.size===1?[...localBanks.values()][0].id:'';
    $('search').value='';$('category').value='';$('favorites-only').checked=false;filter();
    $('import-status').textContent=`${added.toLocaleString('en-US')} sounds imported into ${localBanks.size} banks. Choose a bank or search, then select sounds.`+(skipped?` ${skipped} non-patch files skipped.`:'')+(errors.length?' '+errors.join(' · '):'');
  }finally{importing=false;$('pick').disabled=$('pickdir').disabled=false;}
}
// One outstanding MIDI request, matched by command, input port and slot.
const cmd=(c,...data)=>[...HEAD,c,...data,0xf7],slotBytes=s=>[s&127,(s>>7)&127];
function onMessage(e){
  const m=e.data,w=waiting;if(!w||m.length<8||HEAD.some((v,i)=>m[i]!==v)||m.at(-1)!==0xf7||m[5]!==w.cmd||(input&&e.target!==input))return;
  const isSlot=[0x41,0x44,0x45].includes(w.cmd);
  if(m.length<(isSlot?10:w.cmd===0x43?11:8)||isSlot&&(m[6]|m[7]<<7)!==w.slot)return;
  waiting=null;clearTimeout(w.timer);w.done({data:m,port:e.target});
}
function ask(out,frame,command,ms=3000){
  if(waiting)throw new Error('Another MIDI request is still running');
  return new Promise(done=>{const w={cmd:command,slot:frame[6]|frame[7]<<7,done,timer:setTimeout(()=>{if(waiting===w)waiting=null;done(null);},ms)};waiting=w;
    try{out.send(frame);}catch{clearTimeout(w.timer);waiting=null;done(null);}});
}
function say(text,bad=false){$('status').textContent=text;$('status').className=bad?'bad':'muted';}
function setConnected(){
  const on=!!output;$('have').setAttribute('aria-disabled',String(!on));$('connection').dataset.state=on?'connected':'disconnected';
  $('connection-label').textContent=on?'FM-1 CONNECTED':'FM-1 NOT CONNECTED';$('connect').textContent=on?'Connect again':'Connect to the FM-1';
  if(!on){$('slots').hidden=true;$('count').textContent='Not connected.';}showQueue();
}
async function connect(){
  if(busy)return;busy=true;showQueue();
  try{
    if(!navigator.requestMIDIAccess)throw new Error('Use Chrome or Edge on a computer for USB MIDI. You can still browse, preview and import sounds.');
    say('Allow MIDI with SysEx when the browser asks…');access=access||await navigator.requestMIDIAccess({sysex:true});
    input=output=null;for(const i of access.inputs.values())i.onmidimessage=onMessage;
    say('Looking for an FM-1 running SY1…');
    for(const out of access.outputs.values()){const a=await ask(out,cmd(0x03),0x43,700);if(a){output=out;input=a.port;break;}}
    if(!output)throw new Error('No FM-1 running SY1 answered. Connect it by USB, switch it on and install SY1.');
    access.onstatechange=()=>{if(output?.state==='disconnected'||input?.state==='disconnected'){
      output=input=null;cancelled=true; if(waiting){const w=waiting;waiting=null;clearTimeout(w.timer);w.done(null);}setConnected();say('FM-1 disconnected. Unsent sounds stay selected.',true);
    }};
    say(`Connected: ${output.name}.`);
  }catch(e){say(e.message,true);}finally{busy=false;setConnected();}
  if(output)await readDevice();
}
async function readDevice(){
  if(!output||busy)return;busy=true;showQueue();
  try{
    const a=await ask(output,cmd(0x03),0x43,1500);if(!a)throw new Error('The FM-1 did not answer. Reconnect before sending.');
    used=Math.min(SLOTS,a.data[6]|a.data[7]<<7);const known=new Set(),list=$('slots');list.replaceChildren();
    for(let s=0;s<used;s++){
      $('count').textContent=`Reading slot ${s+1} of ${used}…`;
      const r=await ask(output,cmd(0x04,...slotBytes(s)),0x44,800);
      if(!r)throw new Error('Could not read the patch slots. Install the latest SY1 and reconnect.');
      if(!r.data[8])continue;known.add(s+1);
      const name=String.fromCharCode(...r.data.slice(9,-1)),li=element('li'),del=element('button','Remove','quiet');del.type='button';del.disabled=busy;del.dataset.remove='';del.setAttribute('aria-label',`Remove ${pad(s+1)} ${name}`);del.addEventListener('click',()=>removeSlot(s,name));
      li.append(element('span',pad(s+1),'n'),element('span',name||'(no name)','name'),del);list.append(li);
    }
    occupied=known;list.hidden=!known.size;$('count').textContent=`${known.size} imported patches · last slot ${used} / ${SLOTS}.`;
  }catch(e){say(e.message,true);output=input=null;setConnected();}
  finally{busy=false;showQueue();}
}
async function removeSlot(slot,name){
  if(busy||!output||!confirm(`Remove ${pad(slot+1)} ${name} from the FM-1?`))return;
  busy=true;showQueue();const r=await ask(output,cmd(0x05,...slotBytes(slot)),0x45,5000);busy=false;
  if(!r||r.data[8]!==0)say('Removal was not confirmed. Reading the device again.',true);await readDevice();
}
async function eraseAll(){
  if(busy||!output||!confirm('Erase all imported patches from the FM-1? This cannot be undone. The eight built-in patches stay.'))return;
  busy=true;showQueue();const r=await ask(output,cmd(0x02),0x42,15000);busy=false;
  if(!r||r.data[6]!==0)say('Erasing was not confirmed. Reading the device again.',true);await readDevice();
}
function target(){return placement($('placement').value==='append'?used+1:$('first').value,queue.length,occupied,$('placement').value==='append');}
function showQueue(){
  const t=target(),append=$('placement').value==='append';if(append)$('first').value=String(Math.min(used+1,SLOTS));$('first').disabled=append||busy;$('placement').disabled=busy;
  const list=$('queue');list.replaceChildren();
  for(const [i,p] of queue.entries()){
    const li=element('li'),name=element('span',p.name,'name'),remove=element('button','×','quiet');remove.type='button';remove.disabled=busy;remove.setAttribute('aria-label','Unselect '+p.name);
    name.append(element('span',banks.get(p.bank).name));remove.addEventListener('click',()=>{queue=queue.filter(q=>q.id!==p.id);render();showQueue();});
    li.append(element('span',pad(t.start+i),'n'),name,remove);list.append(li);
  }
  list.hidden=!queue.length;$('clear').hidden=!queue.length;$('clear').disabled=busy;$('selected-count').textContent=queue.length;
  $('queued').textContent=!queue.length?'Pick sounds from the library to get started.':!t.fits?'This selection does not fit. Choose fewer sounds or another starting slot.':`${queue.length} sounds → slots ${pad(t.start)}–${pad(t.end)}.`+(t.overlaps?` ${t.overlaps} existing patches will be replaced.`:'');
  $('queued').className=t.overlaps||queue.length&&!t.fits?'bad':'muted';
  $('capacity').textContent=output?`${SLOTS-used} slots available after your last patch. ${occupied.size} / ${SLOTS} occupied.`:'Connect to check your available slots.';
  $('send').disabled=!output||busy||!t.fits;$('send').textContent=busy?'Working…':queue.length?`Send ${queue.length} to the FM-1`:'Choose some sounds';
  $('connect').disabled=busy;$('refresh').disabled=$('erase').disabled=!output||busy;
  for(const b of document.querySelectorAll('[data-remove]'))b.disabled=busy;
  // Update selection controls without losing focus in the table.
  const selected=new Set(queue.map(p=>p.id));for(const c of document.querySelectorAll('#patches input'))c.disabled=busy||(!c.checked&&queue.length>=SLOTS);
  const fresh=filtered.filter(p=>!selected.has(p.id)).length;$('select-bank').disabled=busy||!fresh||queue.length+fresh>SLOTS;
  $('selection-hint').textContent=queue.length+fresh>SLOTS?'Choose a bank or narrow your search. You can select up to 1,024 sounds at a time.':'';
}
async function send(){
  const t=target();if(busy||!output||!t.fits)return;
  if(t.overlaps&&!confirm(`Replace ${t.overlaps} existing patches in slots ${pad(t.start)}–${pad(t.end)}?`))return;
  busy=true;cancelled=false;showQueue();$('cancel').hidden=false;$('cancel').disabled=false;
  const todo=queue.slice(),bar=$('progress');bar.hidden=false;bar.max=todo.length;bar.value=0;let stored=0,error='';
  try{
    // Resolve every selected patch before the first hardware write.
    const prepared=[];
    for(const p of todo){if(cancelled)break;$('result').textContent=`Preparing ${prepared.length+1} / ${todo.length}: ${p.name}`;const parsed=parseSy1(await patchBytes(p));if(!parsed.lines)throw new Error('Invalid patch: '+p.name);prepared.push(parsed);}
    for(let i=0;i<prepared.length&&!cancelled;i++){
      if(!output)throw new Error('FM-1 disconnected.');$('result').textContent=`Sending ${i+1} / ${todo.length}: ${todo[i].name}`;
      const r=await ask(output,frameFor(prepared[i],t.start-1+i),0x41,5000);
      if(!r||r.data[8]!==0)throw new Error('The FM-1 did not confirm this patch. Reconnect and check its slots before retrying.');
      stored++;occupied.add(t.start+i);used=Math.max(used,t.start+i);bar.value=stored;
      queue=queue.filter(p=>p.id!==todo[i].id);
    }
  }catch(e){error=e.message;}
  finally{
    busy=false;bar.hidden=true;$('cancel').hidden=true;
    // Advance manual placement too, so retry never replaces the patches just acknowledged.
    if($('placement').value==='manual')$('first').value=String(Math.min(SLOTS,t.start+stored));
    $('result').textContent=`${stored} sounds stored. ${queue.length} still selected.`+(cancelled?' Transfer stopped.':'')+(error?' '+error:'');$('result').className=error?'bad':'';
    render();showQueue();if(output)await readDevice();
  }
}
for(const c of CATEGORIES)$('category').add(new Option(c,c));
for(const id of ['search','bank','category','favorites-only'])$(id).addEventListener(id==='search'?'input':'change',()=>filter());
$('previous').onclick=()=>{page--;filter(false);};$('next').onclick=()=>{page++;filter(false);};
$('select-bank').onclick=()=>{if(busy)return;const selected=new Set(queue.map(p=>p.id)),fresh=filtered.filter(p=>!selected.has(p.id));if(queue.length+fresh.length>SLOTS)return;queue.push(...fresh);render();showQueue();};
$('clear').onclick=()=>{if(busy)return;queue=[];$('result').textContent='';render();showQueue();};
$('placement').onchange=showQueue;$('first').oninput=showQueue;
$('connect').onclick=connect;$('refresh').onclick=readDevice;$('erase').onclick=eraseAll;$('send').onclick=send;
$('cancel').onclick=()=>{cancelled=true;$('cancel').disabled=true;$('result').textContent='Stopping after the current patch is confirmed…';};
$('pick').onclick=()=>$('files').click();$('pickdir').onclick=()=>$('dir').click();
for(const id of ['files','dir'])$(id).onchange=e=>{addFiles(e.target.files);e.target.value='';};
const drop=$('drop'),stop=e=>{e.preventDefault();e.stopPropagation();};
for(const t of [drop,document.body]){
  t.addEventListener('dragover',e=>{stop(e);drop.classList.add('over');});t.addEventListener('dragleave',e=>{stop(e);drop.classList.remove('over');});
  t.addEventListener('drop',async e=>{stop(e);drop.classList.remove('over');const es=[...e.dataTransfer.items].map(i=>i.webkitGetAsEntry?.()).filter(Boolean);if(!es.length)return addFiles(e.dataTransfer.files);const files=[];try{for(const en of es)await entryFiles(en,files);await addFiles(files);}catch(err){$('import-status').textContent=err.message;}});
}
showQueue();catalogue();
