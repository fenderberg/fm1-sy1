// Deterministic factory selection from the redistributable website banks: tools/factory_pick.mjs decides which
// patches qualify and as what (name and sound must agree); here they are shared out near evenly across the 13
// categories, round-robin across source banks, at most two of one name series (SYNTH BASS 001, 002 ..). Sounds
// that were in the previous factory bank come last in every pool, so a new bank is new where the library allows.
// (The files keep their factory500 names: links and firmware identifiers stay as they were.)
// Archive contents are data; original selected .sy1 files and full readmes are retained.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {unzipEntries,parseSy1,nameText} from '../app/web/emu/app.js';
import {CATEGORIES,ORDER,classify,quotas as share} from './factory_pick.mjs';
const COUNT=256,OTHER=8,PER_SERIES=2;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const lib=path.join(root,'app/site/library');
const catalog=JSON.parse(await readFile(path.join(lib,'catalog.json')));
const previous=new Set(await readFile(path.join(lib,'factory500.json')).then(b=>JSON.parse(b).patches.map(p=>p.soundSha256)).catch(()=>[]));
const keep=process.argv.includes('--keep');   // rebuild the same bank: the previous one is not avoided
const hash=b=>createHash('sha256').update(b).digest('hex');
const encode=s=>new TextEncoder().encode(s);
const ascii=s=>s.normalize('NFKD').replace(/[\u0300-\u036f]/g,'').replace(/[“”]/g,'"').replace(/[‘’]/g,"'").replace(/[^\x20-\x7e]/g,'?');
const series=n=>n.toUpperCase().replace(/[^A-Z]+/g,' ').trim(),seriesSeen=new Map();
const pools=new Map(CATEGORIES.map(c=>[c,new Map()])),bankFiles=new Map(),available={},seenAll=new Set();
for(const bank of catalog.banks){
 const entries=await unzipEntries(await readFile(path.join(lib,bank.file)));
 const files=new Map(entries.map(e=>[e.name,e.bytes]));bankFiles.set(bank.id,entries);
 for(const meta of bank.patches){
  const bytes=files.get(meta.path);if(!bytes)throw new Error('Missing '+meta.path);
  const patch=parseSy1(bytes);if(!patch.lines)throw new Error('Invalid '+meta.path);
  const soundHash=hash(Buffer.from(patch.values.join(',')));
  const verdict=classify(meta.name,patch.values);if(!verdict.category||seenAll.has(soundHash))continue;
  seenAll.add(soundHash);
  // what a category can offer: at most PER_SERIES of one name series, so VOICES 001..013 count as two
  const key=verdict.category+'|'+series(meta.name);seriesSeen.set(key,(seriesSeen.get(key)||0)+1);
  if(seriesSeen.get(key)<=PER_SERIES)available[verdict.category]=(available[verdict.category]||0)+1;
  const pool=pools.get(verdict.category);if(!pool.has(bank.id))pool.set(bank.id,[]);
  pool.get(bank.id).push({meta:{...meta,category:verdict.category},bank,patch,bytes,soundHash});
 }
}
if(!keep)for(const pool of pools.values())for(const list of pool.values())list.sort((a,b)=>previous.has(a.soundHash)-previous.has(b.soundHash));
const quotas=share(available,COUNT,OTHER);
const selected=[],chosen=new Set();
for(const category of ORDER){
 const count=quotas[category],taken=new Map();let added=0;
 // at most PER_SERIES of one name series (the quotas already allow for it)
 for(const limit of [PER_SERIES]){
  const banks=[...pools.get(category).values()].map(e=>e.slice());
  for(let progress=true;added<count&&progress;){progress=false;
   for(const entries of banks){
    while(entries.length&&(chosen.has(entries[0])||(taken.get(series(entries[0].meta.name))||0)>=limit))entries.shift();
    if(!entries.length||added>=count)continue;
    const p=entries.shift();chosen.add(p);selected.push(p);added++;progress=true;
    taken.set(series(p.meta.name),(taken.get(series(p.meta.name))||0)+1);
   }
  }
  if(added>=count)break;
 }
 if(added<count)throw new Error('Not enough sounds for '+category);
}
const sourceBanks=catalog.banks.filter(b=>selected.some(p=>p.bank.id===b.id));
const bankIndex=new Map(sourceBanks.map((b,i)=>[b.id,i]));
const records=selected.map(({patch,meta})=>{
 const r=new Uint8Array(226);r[0]=0xa5;r[1]=1;r[3]=0xc1;
 // Map the stable firmware category IDs explicitly.
 r[2]=CATEGORIES.indexOf(meta.category)+1;
 for(let i=0;i<23&&i<patch.name.length&&patch.name[i];i++)r[4+i]=patch.name[i]>=32&&patch.name[i]<127?patch.name[i]:63;
 for(let i=0;i<99;i++){r[28+2*i]=patch.values[i]&255;r[29+2*i]=(patch.values[i]>>8)&255;}
 return r;
});
let credits='SY1 factory presets\n\n'+COUNT+' parameter-distinct Synth1 community presets. These presets have their authors\' own terms and are separate from the SY1 source-code MIT license.\nSource: '+(catalog.source?.url||'website community library')+'\nCompilation credit: '+(catalog.source?.compilationCredit||'see source')+'\n\n';
for(const b of sourceBanks){credits+='\n=== '+b.name+' ===\nAuthor: '+b.author+'\nTerms: '+b.terms+'\n';for(const note of b.notes)credits+='\n--- '+note.path+' ---\n'+note.text+'\n';}
const creditBytes=encode(credits);
const format=bytes=>[...bytes].map(n=>String(n)).join(',');
let header='/* GENERATED by tools/make_factory500.mjs. Presets retain their own authors\' terms.\n * See app/presets/CREDITS.txt and app/site/library/factory500.zip for complete original notes. */\n#pragma once\n#define FACTORY500_COUNT '+COUNT+'u\n';
header+='static const uint8_t FACTORY500[FACTORY500_COUNT][BANK_PAYLOAD] = {\n'+records.map(r=>'    {'+format(r)+'},').join('\n')+'\n};\n';
header+='static const uint8_t FACTORY500_BANK[FACTORY500_COUNT] = {'+format(selected.map(p=>bankIndex.get(p.bank.id)))+'};\n';
header+='static const struct { const char *name, *author; } FACTORY500_SOURCE[] = {\n'+sourceBanks.map(b=>'    {'+JSON.stringify(ascii(b.name))+','+JSON.stringify(ascii(b.author))+'},').join('\n')+'\n};\n';
header+='static const uint8_t FACTORY500_CREDITS[] = {\n';for(let i=0;i<creditBytes.length;i+=100)header+='    '+format(creditBytes.subarray(i,i+100))+',\n';header+='};\n';
// Minimal stored ZIP writer: original files, valid CRCs, UTF-8 paths, no dependencies.
const crcTable=Array.from({length:256},(_,n)=>{for(let i=0;i<8;i++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
const crc=bytes=>{let c=0xffffffff;for(const b of bytes)c=crcTable[(c^b)&255]^(c>>>8);return(c^0xffffffff)>>>0;};
function zip(files){const local=[],central=[];let offset=0;for(const [name,raw] of files){const data=Buffer.from(raw),n=Buffer.from(name),sum=crc(data),h=Buffer.alloc(30),c=Buffer.alloc(46);h.writeUInt32LE(0x04034b50);h.writeUInt16LE(20,4);h.writeUInt16LE(0x800,6);h.writeUInt16LE(0x21,12);h.writeUInt32LE(sum,14);h.writeUInt32LE(data.length,18);h.writeUInt32LE(data.length,22);h.writeUInt16LE(n.length,26);c.writeUInt32LE(0x02014b50);c.writeUInt16LE(20,6);c.writeUInt16LE(0x800,8);c.writeUInt16LE(0x21,14);c.writeUInt32LE(sum,16);c.writeUInt32LE(data.length,20);c.writeUInt32LE(data.length,24);c.writeUInt16LE(n.length,28);c.writeUInt32LE(offset,42);local.push(h,n,data);central.push(c,n);offset+=h.length+n.length+data.length;}const cd=Buffer.concat(central),end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(files.length,8);end.writeUInt16LE(files.length,10);end.writeUInt32LE(cd.length,12);end.writeUInt32LE(offset,16);return Buffer.concat([...local,cd,end]);}
const download=selected.map((p,i)=>['patches/'+String(i+1).padStart(3,'0')+'-'+p.bank.id+'/'+p.meta.path,p.bytes]);
for(const b of sourceBanks)for(const f of bankFiles.get(b.id))if(/\.txt$/i.test(f.name))download.push(['readmes/'+b.id+'/'+f.name,f.bytes]);
download.push(['CREDITS.txt',creditBytes]);
const manifest={schema:1,title:'SY1 Factory '+COUNT,count:COUNT,selection:'Unique parameter sets whose sound agrees with the category their name gives in the Patch Manager (tools/factory_pick.mjs): near-even shares of 13 categories, Other kept small, round-robin across source banks, at most two of one name series (a category short of variety gives its places to the others), sounds new to the factory bank first. Every one renders audible, finite audio in the host test. This is not a claim of individual listening tests.',categories:Object.fromEntries(ORDER.map(c=>[c,quotas[c]])),bankCount:sourceBanks.length,source:catalog.source,creditsFile:'factory500-credits.txt',download:'factory500.zip',creditsSha256:hash(creditBytes),patches:selected.map((p,i)=>({number:i+1,id:p.meta.id,name:p.meta.name,deviceName:nameText(p.patch),category:p.meta.category,bank:p.bank.id,bankName:p.bank.name,author:p.bank.author,path:p.meta.path,soundSha256:p.soundHash,recordSha256:hash(records[i])}))};
await mkdir(path.join(root,'app/presets'),{recursive:true});
await writeFile(path.join(root,'app/presets/factory500.h'),header);
await writeFile(path.join(root,'app/presets/CREDITS.txt'),creditBytes);
await writeFile(path.join(lib,'factory500-credits.txt'),creditBytes);
await writeFile(path.join(lib,'factory500.json'),JSON.stringify(manifest));
await writeFile(path.join(lib,'factory500.zip'),zip(download));
console.log('Factory:',selected.length,'sounds,',selected.filter(p=>!previous.has(p.soundHash)).length,'new;',JSON.stringify(manifest.categories),'from available',JSON.stringify(available),';','unique sounds from',sourceBanks.length,'banks;',creditBytes.length,'bytes of full credits/readmes.');
