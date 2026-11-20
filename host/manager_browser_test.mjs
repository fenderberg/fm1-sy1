// Optional browser integration QA. Serve app/build/site, set PLAYWRIGHT_MODULE to an installed module
// (playwright, or playwright-core with SY1_TEST_CHANNEL=chrome|msedge to drive an installed browser).
// No physical MIDI ports: every SysEx response below comes from this stand-in, which keeps whole records like the FM-1.
import assert from 'node:assert/strict';
import {mkdir} from 'node:fs/promises';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.env.SY1_TEST_URL||'http://127.0.0.1:8473';
const browser=await chromium.launch({headless:true,...(process.env.SY1_TEST_CHANNEL?{channel:process.env.SY1_TEST_CHANNEL}:{})});
try {
const context=await browser.newContext({viewport:{width:1440,height:1000}}),errors=[];
context.on('page',p=>p.on('pageerror',e=>errors.push(e.message)));
await context.addInitScript(()=>{
  if(window.top!==window)return;
  const head=[0xf0,0x7d,0x53,0x59,0x31],slots=new Map();
  const record=name=>{const r=new Uint8Array(226);r[0]=0xa5;r[1]=1;r[2]=r[3]=0xff;[...name].forEach((c,i)=>{r[4+i]=c.charCodeAt(0);});r[28]=name.length;r[29]=0x80;return r;};
  const name=r=>{let t='';for(let i=4;i<27&&r[i];i++)t+=String.fromCharCode(r[i]);return t;};
  const pack=part=>{const out=[];for(let i=0;i<part.length;i+=7){let top=0;for(let j=0;j<7;j++)top|=((part[i+j]||0)>>7)<<j;out.push(top);for(let j=0;j<7;j++)out.push((part[i+j]||0)&127);}return out;};
  slots.set(0,record('Existing Bass'));slots.set(5,record('Old Lead'));
  const input={state:'connected',name:'Test FM-1',onmidimessage:null};
  window.mock={writes:[],clears:[],erased:0,failNext:false,delay:2,legacy:false,noParts:false,quad:true,routing:false,layer:false,parts:{on:0,layout:1,ch:[3,4,3,4],slot:[-1,-1]},slots,
    names:()=>[...slots.keys()].sort((a,b)=>a-b).map(s=>s+':'+name(slots.get(s))),bytes:(s,i)=>slots.get(s)[i]};
  const out={state:'connected',name:'Test FM-1',send(frame){
    const c=frame[5],slot=frame[6]|frame[7]<<7;let response;
    if(c===3){const used=slots.size?Math.max(...slots.keys())+1:0;response=[...head,0x43,used&127,used>>7,0,8,...(window.mock.legacy?[]:[window.mock.noParts?1:(window.mock.quad?7:3)|(window.mock.routing?8:0)]),0xf7];}
    else if(c===8){const m=window.mock.parts;if(frame[6]<=(window.mock.quad?2:1)){m.on=frame[6];if(m.on===2&&m.slot.length===2){m.slot.push(-1,-1);m.ch=[1,2,3,4];}if(m.on!==2&&m.slot.length===4){m.slot=m.slot.slice(0,2);m.ch=[3,4,3,4];}}if(frame[7]<=2)m.layout=frame[7];const sl=m.slot.map(x=>x+1);
      response=[...head,0x48,m.on,m.layout,0,m.ch[0]-1,m.ch[1]-1,sl[0]&127,sl[0]>>7,sl[1]&127,sl[1]>>7,...(m.on===2?[m.ch[2]-1,m.ch[3]-1,sl[2]&127,sl[2]>>7,sl[3]&127,sl[3]>>7]:[]),0xf7];}
    else if(c===10){const m=window.mock.parts;for(let p=0;p<4;p++)if(frame[p+8]<16)m.ch[p]=frame[p+8]+1;response=[...head,0x4a,0,m.layout,...m.ch.map(ch=>ch-1),0xf7];}
    else if(c===11 && window.mock.routing){const m=window.mock.parts;if(frame[6]<2)m.layout=frame[6];if(frame[7]<2)window.mock.layer=!!frame[7];
      for(let p=0;p<2;p++)if(frame[p+8]<16)m.ch[p]=frame[p+8]+1;
      response=[...head,0x4b,m.layout,+window.mock.layer,m.ch[0]-1,m.ch[1]-1,0xf7];}
    else if(c===9){const part=frame[6],s=frame[7]|frame[8]<<7,ok=slots.has(s);if(ok)window.mock.parts.slot[part]=s;response=[...head,0x49,part,frame[7],frame[8],ok?0:1,0xf7];}
    else if(c===4)response=[...head,0x44,slot&127,slot>>7,slots.has(slot)?1:0,...(slots.has(slot)?[...name(slots.get(slot))].map(x=>x.charCodeAt(0)):[]),0xf7];
    else if(c===7){if(window.mock.legacy){window.mock.stuck=true;return;}const r=slots.get(slot),part=frame[8];
      response=[...head,0x47,slot&127,slot>>7,part,r?1:0,...(r?pack(part?r.subarray(119):r.subarray(0,119)):[]),0xf7];}
    else if(c===1){const fail=window.mock.failNext;window.mock.failNext=false;window.mock.writes.push({slot,fail});
      if(!fail){const r=new Uint8Array(226);let k=0;for(let i=8;i+8<frame.length&&k<226;i+=8)for(let j=0;j<7&&k<226;j++)r[k++]=frame[i+1+j]|((frame[i]>>j&1)<<7);slots.set(slot,r);}
      response=[...head,0x41,slot&127,slot>>7,fail?1:0,0xf7];
      // A stale acknowledgement for another slot must be ignored.
      setTimeout(()=>input.onmidimessage?.({target:input,data:Uint8Array.from([...head,0x41,(slot+1)&127,(slot+1)>>7,0,0xf7])}),0);
    }else if(c===5){slots.delete(slot);window.mock.clears.push(slot);response=[...head,0x45,slot&127,slot>>7,0,0xf7];}
    else if(c===2){slots.clear();window.mock.erased++;response=[...head,0x42,0,0xf7];}
    if(response)setTimeout(()=>input.onmidimessage?.({target:input,data:Uint8Array.from(response)}),c===1?window.mock.delay:1);
  }};
  Object.defineProperty(navigator,'requestMIDIAccess',{value:async()=>({inputs:new Map([['test',input]]),outputs:new Map([['test',out]])})});
});
const page=await context.newPage(),text=id=>page.locator(id).textContent();
const loaded=()=>page.waitForFunction(()=>document.getElementById('total-patches').textContent==='3,174');
const idle=()=>page.waitForFunction(()=>!document.getElementById('connect').disabled&&document.getElementById('cancel').hidden);
const synced=async()=>{await page.locator('#sync').click();await page.waitForFunction(()=>/confirmed/.test(document.getElementById('result').textContent));await idle();};
const names=()=>page.evaluate(()=>mock.names());
await page.goto(base+'/manager/');await loaded();

// The library: virtual rows, search, character counts, the factory bank and favourites that persist.
assert.equal(await text('#lcount'),'3,174 of 3,174 sounds');
const rows=await page.locator('#llist .prow').count();assert.ok(rows>5&&rows<80,'only the rows in view are rendered: '+rows);
await page.locator('#factory-only').click();assert.equal(await text('#lcount'),'256 of 3,174 sounds');await page.locator('#factory-only').click();
await page.locator('#search').fill('warm');assert.match(await text('#lcount'),/^\d+ of 3,174 sounds$/);assert.notEqual(await text('#lcount'),'3,174 of 3,174 sounds');
await page.locator('#llist [data-act=fav]').first().click();await page.locator('#lchips [data-tog=fav]').click();assert.equal(await page.locator('#llist .prow').count(),1);
await page.reload();await loaded();await page.locator('#lchips [data-tog=fav]').click();assert.equal(await page.locator('#llist .prow').count(),1);
await page.locator('#lchips [data-tog=fav]').click();
const bank=await page.locator('#bank option').evaluateAll(options=>options.find(o=>o.textContent.includes('A-00')).value);await page.selectOption('#bank',bank);
assert.equal(await text('#lcount'),'128 of 3,174 sounds');assert.ok(await page.locator('#bank-download').getAttribute('href'));
await page.selectOption('#bank','');

// Without an FM-1: sounds wait, nothing can be synced.
for(let i=0;i<3;i++)await page.locator('#llist input[type=checkbox]').nth(i).check();
assert.equal(await text('#add'),'Add 3 to FM-1 →');await page.locator('#add').click();
assert.match(await text('#syncmsg'),/3 added.*Connect your FM-1/);assert.equal(await page.locator('#sync').isDisabled(),true);
const staged=await page.locator('#dlist .prow .nm').allTextContents();assert.equal(staged.length,3);

// Connect: the FM-1's two patches are read whole, the waiting sounds go after its last patch.
await page.locator('#connect').click();await page.waitForFunction(()=>document.getElementById('connection-label').textContent==='FM-1 CONNECTED');await idle();
assert.equal(await text('#dcount'),'5 / 1,024 slots used · 1,019 free');assert.equal(await page.locator('#legacy').isHidden(),true);
assert.deepEqual(await page.locator('#dlist .prow .no').allTextContents(),['001','006','007','008','009']);
assert.equal(await text('#sync'),'Sync 3 patch slots');assert.equal(await page.evaluate(()=>mock.writes.length),0);
await synced();
assert.deepEqual(await page.evaluate(()=>mock.writes.map(w=>w.slot)),[6,7,8]);assert.match(await text('#syncmsg'),/matches this layout/);
assert.deepEqual((await names()).slice(2).map(n=>n.slice(2)),staged);
assert.deepEqual(await page.locator('#llist .badge.on:visible').allTextContents().then(a=>a.slice(0,3)),['007','008','009']);

// A refused write keeps everything staged; the retry writes it.
for(const i of [3,4])await page.locator('#llist input[type=checkbox]').nth(i).check();
await page.locator('#add').click();await page.evaluate(()=>{mock.failNext=true;});
await page.locator('#sync').click();await page.waitForFunction(()=>document.getElementById('result').textContent.includes('did not confirm slot 010'));await idle();
assert.match(await text('#result'),/0 slots written and confirmed\. 2 still staged/);assert.match(await text('#syncmsg'),/2 added/);
await synced();assert.deepEqual(await page.evaluate(()=>mock.writes.filter(w=>!w.fail).map(w=>w.slot)),[6,7,8,9,10]);

// Arrange: close the gaps, undo, redo, sync. The patches move; a backup of the old layout is kept.
const before=await names();
await page.locator('#m-arr summary').click();await page.locator('[data-arr=gaps]').click();
assert.match(await text('#syncmsg'),/6 moved/);assert.equal(await text('#sync'),'Sync 10 patch slots');
await page.locator('#undo').click();assert.match(await text('#syncmsg'),/matches this layout/);
await page.locator('#m-arr summary').click();await page.locator('[data-arr=gaps]').click();await synced();
assert.deepEqual(await names(),before.map((n,i)=>i+n.slice(n.indexOf(':'))));
assert.equal(await page.evaluate(()=>JSON.parse(localStorage.getItem('sy1-sets'))[0].name),'Auto-backup before last sync');

// Rename, another character, a move and a removal, synced in one go.
await page.locator('#dlist .prow .nm').first().dblclick();await page.locator('#dlist .rename').fill('Renamed Bass');await page.keyboard.press('Enter');
await page.locator('#dlist .prow select').nth(1).selectOption('6');
assert.match(await text('#syncmsg'),/2 edited/);
await page.locator('#desel').click();await page.locator('#dlist input[type=checkbox]').nth(2).check();await page.locator('#moveto').fill('1');await page.locator('#move').click();
await page.locator('#dlist [data-act=del]').last().click();
assert.match(await text('#syncmsg'),/3 moved · 1 removed/);
let asked=0;page.once('dialog',async d=>{asked++;await d.dismiss();});const untouched=await names();await page.locator('#sync').click();assert.equal(asked,1);
assert.deepEqual(await names(),untouched,'a declined removal writes nothing');
page.once('dialog',d=>d.accept());await synced();
const after=await names();assert.equal(after.length,6);assert.equal(after[0],'0:'+before[2].slice(2));assert.equal(after[1],'1:Renamed Bass');assert.equal(after[2],'2:Old Lead');
assert.deepEqual(await page.evaluate(()=>[mock.bytes(2,2),mock.bytes(2,3),mock.bytes(1,29),mock.clears.at(-1)]),[7,0xc1,0x80,6]);

// A set brings a saved layout back as staged changes; discard drops them.
await page.locator('#m-sets summary').click();await page.locator('#setlist .load').first().click();
assert.match(await text('#syncmsg'),/slots to write/);await page.locator('#discard').click();assert.match(await text('#syncmsg'),/matches this layout/);

// Reading again keeps what the FM-1 holds; a staged sound survives when it changed behind our back.
await page.locator('#llist input[type=checkbox]').nth(8).check();await page.locator('#add').click();
await page.evaluate(()=>{mock.slots.delete(0);});await page.locator('#connect').click();await idle();
assert.equal(await text('#dcount'),'6 / 1,024 slots used · 1,018 free');assert.match(await text('#syncmsg'),/1 added/);await synced();

// A pair in the library: L pinned, R auditioned with it, both staged side by side.
await page.locator('#llist [data-act=pin]').first().click();assert.equal(await page.locator('#pairbar').isVisible(),true);
assert.match(await text('#pairname'),/\S/);
await page.locator('#llist [data-act=play]').nth(3).click();
await page.waitForFunction(()=>/\(L\) with|^Preview failed/.test(document.getElementById('preview-status').textContent),{},{timeout:45000});
const pairText=await text('#preview-status');
assert.equal(await page.locator('#pairadd').isDisabled(),false);
await page.locator('#pairoff').click();assert.equal(await page.locator('#pairbar').isHidden(),true);

// Preview plays in the page: the hidden browser synth answers for the patch.
const usb=await page.evaluate(()=>mock.writes.length);
await page.locator('#llist [data-act=play]').first().click();
await page.waitForFunction(()=>/^▶ Playing|^Preview failed/.test(document.getElementById('preview-status').textContent),{},{timeout:45000});
assert.match(await text('#preview-status'),/^▶ Playing/);
await page.locator('#llist [data-act=play]').nth(1).click();await page.waitForFunction(()=>{const rows=document.querySelectorAll('#llist .prow .nm');return document.getElementById('preview-status').textContent.includes(rows[1].textContent);},{},{timeout:15000});
assert.equal(await page.evaluate(()=>mock.writes.length),usb,'preview never writes to USB');
await mkdir('app/build/manager-qa',{recursive:true});
await page.evaluate(()=>{document.activeElement?.blur();window.scrollTo(0,0);});await page.screenshot({path:'app/build/manager-qa/desktop.png'});

// An older SY1 without the record read: adding and removing work, reordering is off.
await page.evaluate(()=>{mock.legacy=true;});await page.locator('#connect').click();await idle();
assert.equal(await page.locator('#legacy').isVisible(),true);assert.equal(await page.evaluate(()=>!!mock.stuck),false,'an older SY1 never gets command 07');assert.equal(await page.locator('#m-arr').evaluate(e=>e.classList.contains('off')),true);
assert.equal(await page.locator('#dlist .prow[draggable]').count(),0);
const count=await page.evaluate(()=>mock.slots.size);
await page.locator('#llist input[type=checkbox]').nth(10).check();await page.locator('#add').click();await synced();
assert.equal(await page.evaluate(()=>mock.slots.size),count+1);
await page.evaluate(()=>{mock.legacy=false;});await page.locator('#connect').click();await idle();assert.equal(await page.locator('#legacy').isHidden(),true);

// Drag: a library sound onto a slot pushes later patches down; a slot dragged to the top moves there.
const order=await page.locator('#dlist .prow .nm').allTextContents();
await page.locator('#search').fill('choir');const dragged=await page.locator('#llist .prow .nm').first().textContent();
await page.locator('#llist .prow').first().dragTo(page.locator('#dlist .prow').first());
assert.deepEqual(await page.locator('#dlist .prow .nm').allTextContents(),[dragged,...order]);
await page.locator('#dlist .prow').nth(2).dragTo(page.locator('#dlist .prow').first());
assert.deepEqual(await page.locator('#dlist .prow .nm').allTextContents(),[order[1],dragged,order[0],...order.slice(2)]);
assert.match(await text('#syncmsg'),/1 added/);await page.locator('#discard').click();await page.locator('#search').fill('');

// A local import: a bank ZIP read in the page becomes its own bank, ready to add.
await page.locator('#files').setInputFiles('app/site/library/banks/azur-studio-1-f89ed5.zip');
await page.waitForFunction(()=>document.getElementById('import-status').textContent.includes('109 sounds imported into 1 banks'));
assert.equal(await text('#total-patches'),'3,283');assert.equal(await text('#lcount'),'109 of 3,283 sounds');
await page.locator('#llist input[type=checkbox]').first().check();await page.locator('#add').click();await synced();
assert.equal(await page.evaluate(()=>mock.slots.size),count+2);
await page.selectOption('#bank','');

// Bitimbral: the mode and output, a slot on each part, shown in the list, the map and the strip.
assert.equal(await page.locator('#parts').isVisible(),true);assert.equal(await page.locator('#layoutseg').isHidden(),true);
await page.locator('[data-mode="1"]').click();await page.waitForFunction(()=>!document.getElementById('layoutseg').hidden);
assert.equal(await page.evaluate(()=>mock.parts.on),1);
await page.locator('[data-layout="0"]').click();await page.waitForFunction(()=>mock.parts.layout===0);
await page.locator('#dlist .prow [data-act=part][data-part="1"]').nth(1).click();
await page.waitForFunction(()=>document.querySelectorAll('#dlist .ic.pr.on').length===1);
await page.locator('#dlist .prow [data-act=part][data-part="0"]').first().click();
await page.waitForFunction(()=>document.querySelectorAll('#dlist .ic.pl.on').length===1);
const nos=(await page.locator('#dlist .prow .no').allTextContents()).slice(0,2);
assert.deepEqual(await page.evaluate(()=>mock.parts.slot),nos.map(n=>+n-1));
assert.match(await text('#partinfo'),new RegExp(`L ch 3 · ${nos[0]} `));
assert.match(await text('#partinfo'),new RegExp(`R ch 4 · ${nos[1]} `));
assert.equal(await page.locator('#map i.pl').count(),1);assert.equal(await page.locator('#map i.pr').count(),1);
// QUAD: all four slot buttons, output choices and editable MIDI channels.
await page.locator('[data-mode="2"]').click();await idle();
assert.equal(await page.evaluate(()=>mock.parts.on),2);
assert.equal(await page.locator('[data-layout="2"]').isHidden(),true);
assert.equal(await page.locator('#quadchannels').isVisible(),true);
assert.equal(await page.locator('#dlist .prow').first().locator('[data-act=part]').count(),4);
await page.locator('#dlist .prow [data-act=part][data-part="2"]').first().click();await idle();
await page.locator('#dlist .prow [data-act=part][data-part="3"]').nth(1).click();await idle();
assert.match(await text('#partinfo'),/3 ch 3/);assert.match(await text('#partinfo'),/4 ch 4/);
await page.locator('#qch0').fill('9');await page.locator('#qch3').fill('16');
await page.evaluate(()=>{mock.parts.ch[0]=7;mock.parts.ch[1]=6;window.dispatchEvent(new Event('focus'));});
await idle();
assert.equal(await page.locator('#qch0').inputValue(),'9');
assert.equal(await page.locator('#qch1').inputValue(),'6');
assert.match(await text('#channelstatus'),/FM-1 channels are now 7, 6, 3, 4/);
await page.locator('#quadapply').click();await idle();
assert.deepEqual(await page.evaluate(()=>mock.parts.ch),[9,6,3,16]);
assert.match(await text('#partinfo'),/4 ch 16/);
await page.screenshot({path:'app/build/manager-qa/quad-desktop.png',fullPage:true});
await page.setViewportSize({width:390,height:844});
await page.locator('#tab-dev').click();
assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'QUAD channels overflow on mobile');
for (const part of [0,1,2,3]) {
  const button=page.locator(`#dlist .prow [data-part="${part}"]`).first();
  const box=await button.boundingBox();
  assert.ok(box && box.x >= 0 && box.x + box.width <= 390, `part ${part + 1} overflows`);
  await button.click();await idle();
}
assert.ok(await page.evaluate(()=>document.querySelector('#dlist').getBoundingClientRect().bottom <
  document.querySelector('.syncbar').getBoundingClientRect().top),'slot list overlaps sync');
await page.screenshot({path:'app/build/manager-qa/quad-mobile.png',fullPage:true});
await page.setViewportSize({width:1440,height:1000});
await page.locator('[data-layout="1"]').click();await idle();assert.equal(await page.evaluate(()=>mock.parts.layout),1);
await page.locator('[data-mode="1"]').click();await idle();
assert.equal(await page.locator('#quadchannels').isHidden(),true);
assert.equal(await page.locator('[data-layout="2"]').isVisible(),true);
await page.evaluate(()=>{mock.quad=false;});await page.locator('#connect').click();await idle();
assert.equal(await page.locator('[data-mode="2"]').isHidden(),true);
await page.evaluate(()=>{mock.quad=true;mock.routing=true;});await page.locator('#connect').click();await idle();
assert.equal(await page.locator('[data-layout="2"]').isHidden(),true);
assert.equal(await page.locator('#layer').isVisible(),true);
await page.locator('#layer').click();await idle();
await page.locator('[data-layout="0"]').click();await idle();
assert.deepEqual(await page.evaluate(()=>[mock.parts.layout,mock.layer]),[0,true]);
await page.locator('#qch1').fill('3');await page.locator('#quadapply').click();await idle();
assert.match(await text('#part-routing'),/Parts L, R share ch 3/);
// A staged slot cannot be played until it is synced.
await page.locator('#llist input[type=checkbox]').nth(12).check();await page.locator('#add').click();
assert.equal(await page.locator('#dlist .prow').last().locator('[data-act=part]').first().isDisabled(),true);await page.locator('#discard').click();
// An SY1 without the parts commands never gets them.
await page.evaluate(()=>{mock.noParts=true;});await page.locator('#connect').click();await idle();
assert.equal(await page.locator('#parts').isHidden(),true);assert.equal(await page.locator('#dlist [data-act=part]').count(),0);
await page.evaluate(()=>{mock.noParts=false;});await page.locator('#connect').click();await idle();
await page.locator('[data-mode="0"]').click();await page.waitForFunction(()=>mock.parts.on===0);

// Remove everything: one erase command.
await page.locator('#m-arr summary').click();await page.locator('[data-arr=clear]').click();page.once('dialog',d=>d.accept());await synced();
assert.deepEqual(await page.evaluate(()=>[mock.erased,mock.slots.size]),[1,0]);

if(process.env.SY1_TEST_COLLECTION){
  await page.locator('#files').setInputFiles(process.env.SY1_TEST_COLLECTION);
  await page.waitForFunction(()=>document.getElementById('import-status').textContent.includes('16,698 sounds imported'),{},{timeout:60000});
  assert.equal(await text('#total-patches'),'19,872');assert.equal(await text('#total-banks'),'202');
  assert.ok((await text('#import-status')).includes('175 banks'));
  await page.screenshot({path:'app/build/manager-qa/local-import.png'});
}
await page.setViewportSize({width:390,height:844});
assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'no mobile horizontal overflow');
await page.locator('#tab-dev').click();assert.equal(await page.locator('#lib').isHidden(),true);
await page.evaluate(()=>{document.activeElement?.blur();window.scrollTo(0,0);});await page.screenshot({path:'app/build/manager-qa/mobile.png',fullPage:true});
assert.deepEqual(errors,[]);console.log('manager browser: library, favourites, staging, whole-record read, sync with failure and retry, arrange, undo, rename, character, move, removal, sets, re-read, in-page preview, older firmware, drag, local import, bitimbral/QUAD parts, four-channel configuration, pairs and erase'+(process.env.SY1_TEST_COLLECTION?' plus full local collection':'')+' passed');
await context.close();
} finally {await browser.close();}
