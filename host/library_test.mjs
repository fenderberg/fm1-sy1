// Catalogue integrity, local-import naming and slot placement without hardware access.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {CATEGORIES,category,fold,patchName,bankName,placement} from '../app/site/manager/library.js';
import {unzipSy1,parseSy1} from '../app/web/emu/app.js';
const catalog=JSON.parse(await readFile(new URL('../app/site/library/catalog.json',import.meta.url)));
assert.equal(catalog.bankCount,27);assert.equal(catalog.patchCount,3174);
let total=0;const ids=new Set();
for(const bank of catalog.banks){
  assert.ok(bank.notes.length,bank.name+' must retain original notes');
  assert.ok(!/Mr Wobble|peterdh|Schiffbauer|EinTon|Summa/.test(bank.name));
  const raw=await readFile(new URL('../app/site/library/'+bank.file,import.meta.url));
  assert.equal(createHash('sha256').update(raw).digest('hex'),bank.sha256);
  const files=await unzipSy1(raw),byPath=new Map(files.map(f=>[f.name,f.bytes]));
  assert.equal(files.length,bank.count);assert.equal(bank.patches.length,bank.count);
  for(const p of bank.patches){
    assert.ok(!ids.has(p.id));ids.add(p.id);assert.ok(CATEGORIES.includes(p.category));
    const parsed=parseSy1(byPath.get(p.path));assert.ok(parsed.lines);assert.equal(patchName(parsed,p.path),p.name);total++;
  }
}
assert.equal(total,catalog.patchCount);
assert.equal(fold('Écho PÄD'),'echo pad');assert.equal(category('Warm Strings'),'Pad');
assert.equal(bankName('collection/Bass.zip/folder/001.sy1'),'Bass');assert.equal(bankName('folder/001.sy1'),'folder');
assert.equal(patchName({name:new Uint8Array()},'a/Empty.sy1'),'Empty');
assert.equal(patchName({name:new Uint8Array([0xe9,0x63,0x68,0x6f])}),'écho');
const occupied=new Set([1,3,10,1024]);
assert.deepEqual(placement(2,3,occupied),{start:2,end:4,overlaps:1,fits:true});
assert.equal(placement(2,3,occupied,true).fits,false);
assert.equal(placement(1024,2,occupied).fits,false);assert.equal(placement(1024,1,new Set(),true).fits,true);
assert.equal(placement(1,0,occupied).fits,false);assert.equal(placement(1,1024,new Set()).fits,true);
console.log(`library: ${total} patches / ${catalog.banks.length} original banks verified; naming, categories and capacity checks passed`);
