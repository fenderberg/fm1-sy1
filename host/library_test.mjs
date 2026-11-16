// Catalogue integrity, local-import naming and slot placement without hardware access.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {CATEGORIES,category,fold,patchName,bankName,placement,RECORD,PART,asciiName,recordFrom,withMeta,recordName,recordCategory,
  recordKey,fullKey,packFrame,unpack7,insertAt,appendSlots,arrange,changes} from '../app/site/manager/library.js';
import {unzipSy1,parseSy1,frameFor,nameText} from '../app/web/emu/app.js';
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
// The manager's own record and frame: byte for byte the emulator loader's, and back again as the FM-1 sends it.
{
  const bank=catalog.banks[0],files=await unzipSy1(await readFile(new URL('../app/site/library/'+bank.file,import.meta.url)));
  for(const f of files.slice(0,20)){
    const parsed=parseSy1(f.bytes),rec=recordFrom(parsed.values,nameText(parsed));
    assert.deepEqual(packFrame(rec,517),frameFor(parsed,517));
    const frame=packFrame(rec,3);assert.deepEqual(unpack7(frame,8,frame.length-1,RECORD),rec);
    // The two parts of a read (command 07), packed as the firmware does.
    const pack=part=>{const out=[];for(let i=0;i<part.length;i+=7){let top=0;for(let j=0;j<7;j++)top|=((part[i+j]||0)>>7)<<j;out.push(top);for(let j=0;j<7;j++)out.push((part[i+j]||0)&127);}return out;};
    const a=pack(rec.subarray(0,PART)),b=pack(rec.subarray(PART));assert.equal(a.length,136);assert.equal(b.length,128);
    assert.deepEqual([...unpack7(a,0,a.length,PART),...unpack7(b,0,b.length,RECORD-PART)],[...rec]);
    assert.equal(unpack7(a,0,a.length-8,PART),null);
    assert.equal(recordName(rec),nameText(parsed));assert.equal(recordCategory(rec).explicit,false);
    assert.equal(CATEGORIES[recordCategory(rec).cat],category(nameText(parsed)));
  }
  const rec=recordFrom(new Array(99).fill(0).map((_,i)=>i===86?45057:i-9),'Écho Pad with a very long name');
  assert.equal(recordName(rec),'Echo Pad with a very lo');assert.equal(rec[27],0);assert.equal(rec[28+2*86]|rec[29+2*86]<<8,45057);
  const lead=withMeta(rec,null,2);assert.deepEqual(recordCategory(lead),{explicit:true,cat:2});assert.equal(recordName(lead),recordName(rec));
  assert.equal(recordKey(lead),recordKey(withMeta(rec,'Other name')));assert.notEqual(recordKey(rec),recordKey(recordFrom(new Array(99).fill(1),'x')));
  assert.notEqual(fullKey(lead),fullKey(rec));assert.equal(asciiName('日本 Bass'),'?? Bass');
}
// Layouts: placing, rippling into gaps, arranging and the sync plan.
{
  const it=(uid,name,cat=0)=>({uid,name,cat}),A=it(1,'b'),B=it(2,'a',1),C=it(3,'c'),X=it(9,'x'),Y=it(10,'y');
  const names=slots=>slots.map(s=>s?s.name:'-').join('');
  const start=[A,B,null,C,null,null];
  assert.equal(names(insertAt(start,1,[X,Y])),'bxyac-');             // later patches move down into the next gaps
  assert.equal(names(insertAt(start,1,[X,Y],[],false)),'baxcy-');    // gaps only: nothing already there moves
  assert.equal(names(insertAt(start,0,[C],[3])),'cba---');           // a move frees its old slot first
  assert.equal(names(insertAt(start,4,[A,B],[0,1])),'---cba');
  assert.equal(insertAt(start,3,[X,Y,A]),null);assert.equal(insertAt([A,B],0,[X]),null);
  assert.equal(names(appendSlots(start,[X,Y])),'ba-cxy');assert.equal(names(appendSlots([A,null,null,B],[X,Y])),'bxya');
  assert.equal(appendSlots([A,null,B],[X,Y]),null);
  assert.equal(names(arrange(start,'gaps')),'bac---');assert.equal(names(arrange(start,'name')),'abc---');
  assert.equal(names(arrange(start,'cat')),'bca---');assert.equal(names(arrange(start,'bank',x=>x===A?'z':'y')),'acb---');
  assert.equal(changes(start,start.slice()).total,0);
  const next=insertAt(start,0,[C],[3]);next[1]={...A,name:'renamed'};next[5]=X;
  assert.deepEqual(changes(start,next),{writes:[0,1,2,5],clears:[3],added:1,moved:3,edited:0,removed:0,total:5});
  const edited=start.slice();edited[0]={...A,name:'new'};edited[3]=null;
  assert.deepEqual(changes(start,edited),{writes:[0],clears:[3],added:0,moved:0,edited:1,removed:1,total:2});
  edited[3]=C;
  assert.deepEqual(changes(start,edited),{writes:[0],clears:[],added:0,moved:0,edited:1,removed:0,total:1});
}
console.log(`library: ${total} patches / ${catalog.banks.length} original banks verified; naming, categories, capacity, slot records and layout checks passed`);
