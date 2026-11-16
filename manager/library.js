// Pure library helpers, shared by the catalogue importer, manager and tests.
export const CATEGORIES = ['Bass','Pad','Lead','Keys','Pluck','Arp','FX','Other'];
export function category(name) {
  const s=name.toUpperCase();
  if (/BASS|BS |BAS /.test(s)) return 'Bass';
  if (/PAD|STRING|CHOIR/.test(s)) return 'Pad';
  if (/LEAD|LD /.test(s)) return 'Lead';
  if (/PLUCK|HARP|PIZZ/.test(s)) return 'Pluck';
  if (/ARP/.test(s)) return 'Arp';
  if (/KEY|PIANO|BELL|ORGAN|EP |CLAV/.test(s)) return 'Keys';
  if (/FX|NOISE|WIND|SWEEP/.test(s)) return 'FX';
  return 'Other';
}
export const fold = text => text.normalize('NFKD').replace(/[\u0300-\u036f]/g,'').toLowerCase();
export function patchName(patch, filename='') {
  let name;
  try { name=new TextDecoder('utf-8',{fatal:true}).decode(patch.name); }
  catch { name=new TextDecoder('windows-1252').decode(patch.name); }
  return name.replace(/\0.*$/,'').trim() || filename.split('/').pop().replace(/\.sy1$/i,'') || 'Untitled patch';
}
export function bankName(path, fallback='Your patches') {
  const chunks=path.split('/');
  const zip=chunks.filter(x=>/\.zip$/i.test(x));
  return zip.length ? zip.at(-1).replace(/\.zip$/i,'') : chunks.length>1 ? chunks.at(-2) : fallback;
}
export function placement(first, count, occupied, append=false) {
  const start=Math.min(1024,Math.max(1,Number.parseInt(first,10)||1));
  const end=start+count-1;
  let overlaps=0;
  for(let slot=start;slot<=Math.min(end,1024);slot++) if(occupied.has(slot))overlaps++;
  return {start,end,overlaps,fits:count>0&&end<=1024&&(!append||overlaps===0)};
}
