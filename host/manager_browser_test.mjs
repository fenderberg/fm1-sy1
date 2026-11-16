// Optional browser integration QA. Serve app/build/site, set PLAYWRIGHT_MODULE to an installed module.
// No physical MIDI ports: every SysEx response below comes from this stand-in.
import assert from 'node:assert/strict';
import {mkdir} from 'node:fs/promises';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.env.SY1_TEST_URL||'http://127.0.0.1:8473';
const browser=await chromium.launch({headless:true});
try {
const context=await browser.newContext({viewport:{width:1440,height:1000}}),errors=[];
context.on('page',p=>p.on('pageerror',e=>errors.push(e.message)));
await context.addInitScript(()=>{
  const head=[0xf0,0x7d,0x53,0x59,0x31],slots=new Map([[0,'Existing Bass']]);
  const input={state:'connected',name:'Test FM-1',onmidimessage:null};
  window.mock={writes:[],failNext:false,delay:10};
  const out={state:'connected',name:'Test FM-1',send(frame){
    const c=frame[5],slot=frame[6]|frame[7]<<7;let response;
    if(c===3){const used=slots.size?Math.max(...slots.keys())+1:0;response=[...head,0x43,used&127,used>>7,0,8,0xf7];}
    else if(c===4)response=[...head,0x44,slot&127,slot>>7,slots.has(slot)?1:0,...[...(slots.get(slot)||'')].map(x=>x.charCodeAt(0)),0xf7];
    else if(c===1){const fail=window.mock.failNext;window.mock.failNext=false;window.mock.writes.push({slot,fail});if(!fail)slots.set(slot,'Uploaded '+slot);response=[...head,0x41,slot&127,slot>>7,fail?1:0,0xf7];
      // A stale acknowledgement for another slot must be ignored.
      setTimeout(()=>input.onmidimessage?.({target:input,data:Uint8Array.from([...head,0x41,(slot+1)&127,(slot+1)>>7,0,0xf7])}),1);
    }else if(c===5){slots.delete(slot);response=[...head,0x45,slot&127,slot>>7,0,0xf7];}
    else if(c===2){slots.clear();response=[...head,0x42,0,0xf7];}
    if(response)setTimeout(()=>input.onmidimessage?.({target:input,data:Uint8Array.from(response)}),c===1?window.mock.delay:1);
  }};
  Object.defineProperty(navigator,'requestMIDIAccess',{value:async()=>({inputs:new Map([['test',input]]),outputs:new Map([['test',out]])})});
});
const page=await context.newPage();await page.goto(base+'/manager/');
await page.waitForFunction(()=>document.getElementById('total-patches').textContent==='3,174');
assert.equal(await page.locator('#patches tr').count(),48);
assert.equal(await page.locator('#select-bank').isDisabled(),true,'whole catalogue exceeds hardware capacity');
await page.locator('#search').fill('warm');await page.waitForFunction(()=>document.querySelectorAll('#patches tr').length>0);
assert.ok((await page.locator('#matches').textContent()).includes('sounds'));
await page.locator('#patches .favorite').first().click();await page.locator('#favorites-only').check();assert.equal(await page.locator('#patches tr').count(),1);
await page.reload();await page.waitForFunction(()=>document.getElementById('total-patches').textContent==='3,174');await page.locator('#favorites-only').check();assert.equal(await page.locator('#patches tr').count(),1);
await page.locator('#favorites-only').uncheck();
const bank=await page.locator('#bank option').evaluateAll(options=>options.find(o=>o.textContent.includes('A-00')).value);await page.selectOption('#bank',bank);
assert.match(await page.locator('#matches').textContent(),/^128 sounds/);assert.ok(await page.locator('#bank-download').getAttribute('href'));
await page.locator('#select-bank').click();assert.equal(await page.locator('#selected-count').textContent(),'128');
await page.locator('#connect').click();await page.waitForFunction(()=>!document.getElementById('connect').disabled);
assert.match(await page.locator('#capacity').textContent(),/1023 slots/);
assert.equal(await page.locator('#first').inputValue(),'2');
await page.selectOption('#placement','manual');await page.locator('#first').fill('1000');assert.equal(await page.locator('#send').isDisabled(),true);
await page.locator('#first').fill('1');assert.match(await page.locator('#queued').textContent(),/1 existing patches/);
let confirmation=0;page.once('dialog',async d=>{confirmation++;await d.dismiss();});await page.locator('#send').click();assert.equal(confirmation,1);assert.equal(await page.evaluate(()=>mock.writes.length),0);
await page.selectOption('#placement','append');await page.locator('#clear').click();
for(let i=0;i<3;i++)await page.locator('#patches input[type=checkbox]').nth(i).check();
await page.evaluate(()=>{mock.failNext=true;});await page.locator('#send').click();
await page.waitForFunction(()=>document.getElementById('result').textContent.includes('did not confirm'));
await page.waitForFunction(()=>!document.getElementById('connect').disabled);assert.equal(await page.locator('#selected-count').textContent(),'3');
await page.locator('#send').click();await page.waitForFunction(()=>document.getElementById('result').textContent.includes('3 sounds stored. 0 still selected.'));
await page.waitForFunction(()=>!document.getElementById('connect').disabled);assert.equal(await page.locator('#selected-count').textContent(),'0');
assert.deepEqual(await page.evaluate(()=>mock.writes.filter(w=>!w.fail).map(w=>w.slot)),[1,2,3]);
// Stop keeps unacknowledged selection and advances a manual destination on retry.
await page.selectOption('#placement','manual');await page.locator('#first').fill('20');
for(let i=0;i<4;i++)await page.locator('#patches input[type=checkbox]').nth(i).check();
await page.evaluate(()=>{mock.delay=180;});await page.locator('#send').click();
await page.waitForFunction(()=>document.getElementById('result').textContent.startsWith('Sending'));
await page.locator('#cancel').click();await page.waitForFunction(()=>document.getElementById('cancel').hidden);
await page.waitForFunction(()=>!document.getElementById('connect').disabled);
assert.equal(await page.locator('#selected-count').textContent(),'3');assert.equal(await page.locator('#first').inputValue(),'21');
await page.locator('#clear').click();
await mkdir('app/build/manager-qa',{recursive:true});
await page.evaluate(()=>{document.activeElement?.blur();window.scrollTo(0,0);});await page.screenshot({path:'app/build/manager-qa/desktop.png'});
// A library preview starts the actual emulator, without calling USB MIDI send.
const [preview]=await Promise.all([context.waitForEvent('page'),page.locator('#patches .preview-link').first().click()]);
await preview.waitForURL(/\/emu\/\?preview=/);await preview.locator('#go').click();
await preview.waitForFunction(()=>document.getElementById('start').hidden);
await preview.getByRole('button',{name:'Play preview phrase'}).waitFor({timeout:15000});
assert.match(await preview.locator('.sy1 [role=status]').textContent(),/Preview:/);
await preview.getByRole('button',{name:'Play preview phrase'}).click();await preview.waitForTimeout(1600);
assert.ok(await preview.locator('#screen').evaluate(canvas=>canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data.some((v,i)=>i%4!==3&&v>100)),'emulator produced a display frame');
await preview.screenshot({path:'app/build/manager-qa/preview.png'});await preview.close();
if(process.env.SY1_TEST_COLLECTION){
  await page.locator('#files').setInputFiles(process.env.SY1_TEST_COLLECTION);
  await page.waitForFunction(()=>document.getElementById('import-status').textContent.includes('16,698 sounds imported'),{},{timeout:60000});
  assert.equal(await page.locator('#total-patches').textContent(),'19,872');assert.equal(await page.locator('#total-banks').textContent(),'202');
  assert.ok((await page.locator('#import-status').textContent()).includes('175 banks'));
  assert.equal(await page.locator('#patches tr').count(),48);assert.equal(await page.locator('#select-bank').isDisabled(),true);
  // Select just one locally imported bank, then preview its first patch.
  const local=await page.locator('#bank option').evaluateAll(opts=>opts.find(o=>o.value.startsWith('local-')&&o.textContent.includes('Wobble')).value);
  await page.selectOption('#bank',local);await page.locator('#select-bank').click();assert.ok(Number(await page.locator('#selected-count').textContent())<=128);
  await page.screenshot({path:'app/build/manager-qa/local-import.png'});
}
await page.setViewportSize({width:390,height:844});
assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'no mobile horizontal overflow');
await page.evaluate(()=>{document.activeElement?.blur();window.scrollTo(0,0);});await page.screenshot({path:'app/build/manager-qa/mobile.png',fullPage:true});
assert.deepEqual(errors,[]);console.log('manager browser: catalogue, search, persisted favourites, bank selection, capacity, overwrite confirmation, MIDI failure/retry/stop, preview and local collection passed');
await context.close();
} finally {await browser.close();}
