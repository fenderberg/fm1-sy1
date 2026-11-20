// Verify every bundled sound against its original website bank and generated firmware record.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {unzipEntries,unzipSy1,parseSy1,nameText} from '../app/web/emu/app.js';
import {CATEGORIES,classify} from '../tools/factory_pick.mjs';
const hash=b=>createHash('sha256').update(b).digest('hex');
const root=new URL('../',import.meta.url),read=p=>readFile(new URL(p,root));
const manifest=JSON.parse(await read('app/site/library/factory500.json'));
const catalog=JSON.parse(await read('app/site/library/catalog.json'));
assert.equal(manifest.count,256);assert.equal(manifest.patches.length,256);assert.equal(manifest.bankCount,27);
const source=new Map(),bankNotes=[];
for(const bank of catalog.banks){const entries=await unzipEntries(await read('app/site/library/'+bank.file));for(const f of entries)source.set(bank.id+':'+f.name,f.bytes);for(const f of entries)if(/\.txt$/i.test(f.name))bankNotes.push([bank.id,f]);}
const header=(await read('app/presets/factory500.h')).toString().replaceAll('\r\n','\n');
const block=header.split('FACTORY500[FACTORY500_COUNT][BANK_PAYLOAD] = {\n')[1].split('\n};')[0];
const records=[...block.matchAll(/\{([\d,]+)\}/g)].map(m=>Buffer.from(m[1].split(',').map(Number)));
assert.equal(records.length,256);
const zip=await unzipEntries(await read('app/site/library/factory500.zip')),files=new Map(zip.map(f=>[f.name,f.bytes]));
const selected=await unzipSy1(await read('app/site/library/factory500.zip'));assert.equal(selected.length,256);
const bySound=new Set(),byRecord=new Set(),byId=new Set(),counts={};
for(const [i,p] of manifest.patches.entries()){
 assert.equal(p.number,i+1);assert.ok(!byId.has(p.id));byId.add(p.id);
 const raw=source.get(p.bank+':'+p.path),parsed=parseSy1(raw),record=records[i];
 assert.ok(parsed.lines);assert.equal(nameText(parsed),p.deviceName);assert.equal(hash(Buffer.from(parsed.values.join(','))),p.soundSha256);
 assert.ok(!bySound.has(p.soundSha256));bySound.add(p.soundSha256);
 assert.ok(!byRecord.has(hash(record.subarray(28))));byRecord.add(hash(record.subarray(28)));
 assert.equal(record.length,226);assert.equal(hash(record),p.recordSha256);assert.equal(record[0],0xa5);assert.equal(record[3],0xc1);
 for(let param=0;param<99;param++)assert.equal(record.readUInt16LE(28+param*2),parsed.values[param]&0xffff);
 const original=files.get('patches/'+String(i+1).padStart(3,'0')+'-'+p.bank+'/'+p.path);assert.deepEqual(Buffer.from(original),Buffer.from(raw));
 counts[p.category]=(counts[p.category]||0)+1;
 // name and sound still agree on the category the firmware gets (record[2]: the Patch Manager's index + 1)
 assert.equal(classify(p.name,parsed.values).category,p.category,p.name);
 assert.equal(CATEGORIES[record[2]-1],p.category);
}
// a cross-section: all 13 categories, the full ones within 2 of each other (a category short of variety, such as
// Vocal & Choir, has fewer), Other at most a sixteenth
assert.deepEqual(Object.keys(counts).sort(),[...CATEGORIES].sort());
const shares=Object.entries(counts).filter(([c,n])=>c!=='Other'&&n>=20).map(([,n])=>n);
assert.ok(shares.length>=11&&Math.max(...shares)-Math.min(...shares)<=2);assert.ok(counts.Other<=256/16);
assert.deepEqual(counts,manifest.categories);
for(const [id,file] of bankNotes)assert.deepEqual(Buffer.from(files.get('readmes/'+id+'/'+file.name)),Buffer.from(file.bytes));
const credits=await read('app/presets/CREDITS.txt');assert.equal(hash(credits),manifest.creditsSha256);assert.deepEqual(await read('app/site/library/factory500-credits.txt'),credits);
assert.deepEqual(Buffer.from(files.get('CREDITS.txt')),credits);
assert.ok(credits.toString().includes('If you want to spread this preset bank around'));
console.log('Factory: all 256 sounds match source files and firmware records; 27 banks, parameter uniqueness, name-and-sound categories, 13-category balance and complete original readmes verified.');
