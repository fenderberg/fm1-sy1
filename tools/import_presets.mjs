// Import only banks whose accompanying notes explicitly allow free redistribution.
// Originals, including all readmes, are preserved byte-for-byte. No archive content is executed.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {unzipEntries,unzipSy1,parseSy1} from '../app/web/emu/app.js';
import {patchName,category} from '../app/site/manager/library.js';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const source=process.argv[2];
if(!source)throw new Error('Usage: node tools/import_presets.mjs COLLECTION.zip');
const output=path.join(root,'app/site/library');
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const raw=await readFile(source), banks=[], excluded=[];
const grants = name => {
  if(/^AZur Studio [123]$/.test(name))return {author:'AZur Studio',terms:'CC0, as declared in the bank readme.',url:'http://bji.yukihotaru.com/'};
  if(name==='Christoph Mueller Synthhunter')return {author:'Christoph Mueller',terms:'Redistribution and use in music permitted by the included readme.'};
  if(name==='Lunatique')return {author:'Rob “Lunatique” Chang',terms:'Redistribution permitted with the complete original readme, included in the download.',url:'https://www.ethereality.info/'};
  if(name==='Nolwenn Ann 1')return {author:'Nolwenn',terms:'Free use; no sale or inclusion in a paid library.'};
  if(name==='Reinhard Reschner Echopark')return {author:'Reinhard Reschner / Echopark Studio',terms:'Free copying and use; no sale or inclusion in a paid library without permission.'};
  if(/^SOS-Synth1-NordLeadII-A-\d\d$/.test(name))return {author:'SOS',terms:'Distribution and use permitted; do not sell.'};
  return null;
};
await mkdir(path.join(output,'banks'),{recursive:true});
for(const entry of await unzipEntries(raw)){
  if(!/\.zip$/i.test(entry.name))continue;
  const name=path.posix.basename(entry.name).replace(/\.zip$/i,''), grant=grants(name);
  if(!grant){excluded.push(name);continue;}
  const digest=hash(entry.bytes), id=name.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'')+'-'+digest.slice(0,6);
  const notes=(await unzipEntries(entry.bytes)).filter(x=>/\.txt$/i.test(x.name)).map(x=>({path:x.name,text:new TextDecoder('windows-1252').decode(x.bytes)}));
  const patches=[];
  for(const file of await unzipSy1(entry.bytes)){
    const patch=parseSy1(file.bytes);if(!patch.lines)continue;
    const title=patchName(patch,file.name);
    patches.push({id:id+':'+hash(new TextEncoder().encode(file.name)).slice(0,12),path:file.name,name:title,category:category(title)});
  }
  await writeFile(path.join(output,'banks',id+'.zip'),entry.bytes);
  banks.push({id,name,...grant,count:patches.length,sha256:digest,file:'banks/'+id+'.zip',notes,patches});
}
const catalog={schema:1,title:'SY1 community preset library',sourceSha256:hash(raw),
  bankCount:banks.length,patchCount:banks.reduce((n,b)=>n+b.count,0),banks,
  provenance:'Selected from the user-supplied zipbank.zip. Original bank notes and authors are retained. Presets are not covered by the SY1 source-code MIT license.'};
await writeFile(path.join(output,'catalog.json'),JSON.stringify(catalog));
await writeFile(path.join(output,'CREDITS.md'),'# Community preset banks\n\nThese banks are provided free of charge under their own authors’ terms. The original ZIPs and readmes are unchanged. They are separate from SY1’s MIT-licensed source code.\n\n'+banks.map(b=>`- **${b.name}** — ${b.author}: ${b.terms}`).join('\n')+'\n');
await writeFile(path.join(output,'IMPORT.md'),`# Catalogue provenance\n\nSource: user-supplied zipbank.zip\n\nSHA-256: ${hash(raw)}\n\nIncluded: ${banks.length} banks / ${catalog.patchCount} presets. Only banks with explicit redistribution wording in the accompanying notes are included. Original notes are preserved in each ZIP and the catalogue.\n\nThe remaining ${excluded.length} banks are available through local import if users supply their own collection; they are not redistributed by this repository.\n\nRebuild with \`node tools/import_presets.mjs /path/to/zipbank.zip\`. The source archive is not required for normal firmware or website builds.\n`);
console.log(`${catalog.patchCount} presets / ${banks.length} banks imported; ${excluded.length} banks kept out of the public catalogue.`);
