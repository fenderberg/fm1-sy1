// Serve a built site. Uses the same Playwright environment variables as manager_browser_test.mjs.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {parseSy1,unzipSy1} from '../app/web/emu/app.js';
const {chromium} = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const base = process.env.SY1_TEST_URL || 'http://127.0.0.1:8473';
const browser = await chromium.launch({headless:true,channel:process.env.SY1_TEST_CHANNEL});
try {
  const context = await browser.newContext({viewport:{width:1440,height:960}});
  const errors = [];
  context.on('page', p => p.on('pageerror', e => errors.push(e.message)));
  // Sample the real worklet output, including changes made while the note is held.
  await context.addInitScript(() => {
    const connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function(...args) {
      if (this instanceof AudioWorkletNode && args[0] === this.context.destination) {
        const meter = this.context.createAnalyser();
        connect.call(this,meter);
        const samples = new Float32Array(meter.fftSize);
        window.energy = () => {
          meter.getFloatTimeDomainData(samples);
          return samples.reduce((sum,v) => sum + v*v,0) / samples.length;
        };
      }
      return connect.apply(this,args);
    };
  });
  const page = await context.newPage();
  page.on('dialog',d => d.accept());
  await page.goto(base + '/editor/');
  const input = page.locator('#file');
  const fixture = Buffer.from('Old Bass\r\ncolor=blue\r\nver=105\r\n9,-12\r\n19,83\r\n92,12\r\n87,65535\r\n110,42\r\n');
  await input.setInputFiles({name:'Old.sy1',mimeType:'application/octet-stream',buffer:fixture});
  await page.waitForFunction(() => document.querySelector('#patch-name').value === 'Old Bass');
  const ids = await page.locator('#modules [id^=p]:visible').evaluateAll(nodes => nodes.map(n => Number(n.id.slice(1))));
  assert.deepEqual(ids.sort((a,b)=>a-b),Array.from({length:99},(_,i)=>i));
  // The classic panel's LED selectors must update the patch and support keyboard navigation.
  await page.locator('#p0').getByRole('radio',{name:'Saw',exact:true}).check();
  await page.keyboard.press('ArrowRight');
  assert(await page.locator('#p0').getByRole('radio',{name:'Pulse',exact:true}).isChecked());
  await page.locator('#p19').focus(); await page.keyboard.press('ArrowRight');
  await page.locator('#patch-name').fill('My Bass');
  const download = page.waitForEvent('download');
  await page.locator('#save').click();
  const saved = await download;
  assert.equal(saved.suggestedFilename(),'My Bass.sy1');
  const raw = await readFile(await saved.path()), expected = parseSy1(fixture).values;
  expected[0] = 2; expected[19]++;
  assert.equal(parseSy1(raw).lines,99); assert.deepEqual(parseSy1(raw).values,expected);
  assert(raw.includes('color=blue\r\n')); assert(raw.includes('110,42\r\n'));
  await input.setInputFiles({name:'Damaged.sy1',mimeType:'application/octet-stream',buffer:Buffer.from('Bad')});
  await page.waitForFunction(() => document.querySelector('#message').classList.contains('error'));
  assert.equal(await page.locator('#patch-name').inputValue(),'My Bass');

  const zip = new URL('../app/site/library/banks/azur-studio-1-f89ed5.zip',import.meta.url);
  await input.setInputFiles({name:'Test.zip',mimeType:'application/zip',buffer:await readFile(zip)});
  await page.locator('#library[open] #load:enabled').waitFor();
  assert.equal(await page.locator('#presets option').count(),(await unzipSy1(await readFile(zip))).length);
  await page.locator('#load').click();
  await page.waitForFunction(() => document.querySelector('#patch-name').value === 'FM Slap Bass');
  await page.locator('#init').click();
  // A sustained envelope gives the analyser a stable signal, and gain 0 must mute it live.
  const set = async (id,val) => page.locator('#p'+id).evaluate((el,n) => { el.value=n; el.dispatchEvent(new Event('input')); },val);
  await set(27,127); await set(19,105);
  await page.locator('#hold').click();
  await page.waitForFunction(() => window.energy?.() > .00001);
  await set(29,0);
  await page.waitForFunction(() => window.energy() < 1e-10);
  await set(29,100);
  await page.waitForFunction(() => window.energy() > .00001);
  await page.locator('#hold').click();
  assert.equal(await page.locator('#hold').getAttribute('aria-pressed'),'false');

  await page.setViewportSize({width:390,height:844});
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await page.locator('#browse').click();
  await page.locator('#banks option:nth-child(2)').waitFor({state:'attached'});
  await page.locator('#banks').selectOption('1');
  await page.locator('#find').fill('FM Slap');
  await page.locator('#load').click();
  await page.waitForFunction(() => document.querySelector('#patch-name').value === 'FM Slap Bass');

  await page.goto(base + '/manager/');
  await page.locator('#llist .prow .nm').first().click();
  const opened = context.waitForEvent('page');
  await page.locator('#edit-patch').click();
  const editor = await opened;
  await editor.waitForFunction(() => document.querySelector('#patch-name')?.value === 'FM Slap Bass');
  await page.locator('#files').setInputFiles({name:'Old.sy1',mimeType:'application/octet-stream',buffer:fixture});
  await page.locator('#llist .nm').filter({hasText:'Old Bass'}).first().click();
  const local = context.waitForEvent('page'); await page.locator('#edit-patch').click();
  await (await local).waitForFunction(() => document.querySelector('#patch-name')?.value === 'Old Bass');
  assert.deepEqual(errors,[]);
  console.log('Editor passed: 99 controls, legacy round trip, ZIP and community presets, manager handoff, live stereo synth, mobile layout.');
} finally { await browser.close(); }
