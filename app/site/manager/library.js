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
export const fold = text => text.normalize('NFKD').replace(/[̀-ͯ]/g,'').toLowerCase();
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

// ---- A user slot's record, as the FM-1 stores it (app/bank.h): 226 bytes.
//   [0] 0xA5 used  [1] format 1  [2] category 1..8  [3] 0xC1 when [2] is explicit  [4..27] name  [28..225] 99 values, int16 LE
export const RECORD = 226, PART = 119;
/** A name as the FM-1 shows it: accents folded, printable ASCII, 23 characters. */
export const asciiName = text => [...text.normalize('NFKD').replace(/[̀-ͯ]/g,'')].map(c=>c>=' '&&c<='~'?c:'?').join('').slice(0,23);
/** A copy with another name (null: keep) and an explicit category index 0..7 (-1: keep). */
export function withMeta(rec, name, cat=-1) {
  const r=rec.slice();
  if(name!==null){ r.fill(0,4,28); const n=asciiName(name); for(let i=0;i<n.length;i++)r[4+i]=n.charCodeAt(i); }
  if(cat>=0){ r[2]=cat+1; r[3]=0xc1; }
  return r;
}
export function recordFrom(values, name, cat=-1) {
  const rec=new Uint8Array(RECORD); rec[0]=0xa5; rec[1]=1; rec[2]=rec[3]=0xff;
  for(let i=0;i<99;i++){ rec[28+2*i]=values[i]&0xff; rec[29+2*i]=(values[i]>>8)&0xff; }
  return withMeta(rec,name,cat);
}
export function recordName(rec) {
  let t=''; for(let i=4;i<27&&rec[i];i++)t+=rec[i]>=32&&rec[i]<127?String.fromCharCode(rec[i]):'?'; return t;
}
/** The category the FM-1 browses this record under: explicit, else guessed from the name as the firmware does. */
export function recordCategory(rec) {
  const explicit=rec[3]===0xc1&&rec[2]>=1&&rec[2]<=8;
  return {explicit,cat:explicit?rec[2]-1:CATEGORIES.indexOf(category(recordName(rec)))};
}
/** The sound alone (the 99 values), for recognising a library patch that is already on the FM-1. */
export function recordKey(rec) {
  let a=0x811c9dc5,b=0x9747b28c;
  for(let i=28;i<RECORD;i++){ a=Math.imul(a^rec[i],0x01000193); b=Math.imul((b+rec[i])^(b>>>13),0x5bd1e995); }
  return (a>>>0).toString(36)+'.'+(b>>>0).toString(36);
}
export const fullKey = rec => String.fromCharCode(...rec);
/** F0 7D 53 59 31 01 <slot lo7> <slot hi7> <packed record> F7: each 7 bytes as 8, their top bits first. */
export function packFrame(rec, slot) {
  const out=[0xf0,0x7d,0x53,0x59,0x31,0x01,slot&0x7f,(slot>>7)&0x7f];
  for(let i=0;i<RECORD;i+=7){
    let top=0; for(let j=0;j<7;j++)top|=((rec[i+j]||0)>>7)<<j;
    out.push(top); for(let j=0;j<7;j++)out.push((rec[i+j]||0)&0x7f);
  }
  out.push(0xf7); return Uint8Array.from(out);
}
/** count bytes out of the 7-bit packing in data[start, end); null when there are too few. */
export function unpack7(data, start, end, count) {
  const out=new Uint8Array(count); let k=0;
  for(let i=start;i+8<=end&&k<count;i+=8)for(let j=0;j<7&&k<count;j++)out[k++]=data[i+1+j]|((data[i]>>j&1)<<7);
  return k===count?out:null;
}

// ---- Layouts: an array of 1,024 slots, each null or an item {uid, name, cat, …}. All of these return a new array.
/** Items placed from target on. ripple: later patches move down into the next gap; otherwise only gaps are filled.
 *  from: slots emptied first (the items' old places). null when there is not enough room. */
export function insertAt(slots, target, items, from=[], ripple=true) {
  const out=slots.slice(); for(const s of from)out[s]=null;
  let free=0; for(let s=target;s<out.length;s++)if(!out[s])free++;
  if(target<0||free<items.length)return null;
  if(!ripple){ let s=target; for(const it of items){ while(out[s])s++; out[s++]=it; } return out; }
  items.forEach((it,k)=>{ let carry=it,q=target+k; while(carry){ const t=out[q]; out[q]=carry; carry=t; q++; } });
  return out;
}
/** After the last used slot; when that does not fit, into the gaps from the start. */
export function appendSlots(slots, items) {
  let last=-1,used=0; slots.forEach((it,s)=>{ if(it){ last=s; used++; } });
  if(used+items.length>slots.length)return null;
  return insertAt(slots,last+1+items.length<=slots.length?last+1:0,items,[],false);
}
export function arrange(slots, how, bankOf=()=>'') {
  const key=it=>fold(it.name),cmp=(a,b)=>key(a)<key(b)?-1:key(a)>key(b)?1:0,items=slots.filter(Boolean);
  if(how==='name')items.sort(cmp);
  else if(how==='cat')items.sort((a,b)=>a.cat-b.cat||cmp(a,b));
  else if(how==='bank')items.sort((a,b)=>bankOf(a).localeCompare(bankOf(b))||cmp(a,b));
  return items.concat(new Array(slots.length-items.length).fill(null));
}
/** What a sync must do to turn base (the FM-1) into slots: the slots to write and to empty, and what that means. */
export function changes(base, slots) {
  const was=new Map(),now=new Set(),writes=[],clears=[]; let added=0,moved=0,edited=0,removed=0;
  base.forEach((it,s)=>{ if(it)was.set(it.uid,s); });
  slots.forEach((it,s)=>{
    if(it){ now.add(it.uid); const from=was.get(it.uid); if(from===undefined)added++; else if(from!==s)moved++; else if(base[s]!==it)edited++; }
    if(it!==base[s])(it?writes:clears).push(s);
  });
  for(const u of was.keys())if(!now.has(u))removed++;
  return {writes,clears,added,moved,edited,removed,total:writes.length+clears.length};
}
