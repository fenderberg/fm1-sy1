// The emulator page's patch reader (app/web/emu/app.js) against the C one: for a set of patch texts, the frames
// it makes must be byte for byte what build/sy1pack writes. Also its zip reader.
//   make test-web      (node 22.16 or later: DecompressionStream deflate-raw)
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { deflateRawSync } from "node:zlib";
import { parseSy1, frameFor, nameText, unzipSy1 } from "../app/web/emu/app.js";

const dir = mkdtempSync(join(tmpdir(), "sy1-"));
let fails = 0, checks = 0;
const check = (ok, what) => { checks++; if (!ok) { fails++; console.log("FAIL " + what); } };

// patch texts: today's format, old versions, and input a reader must survive
let rng = 7;
const rnd = (n) => (rng = (rng * 1664525 + 1013904223) >>> 0) % n;
const texts = [
  "Plain\ncolor=red\nver=112\n0,1\n14,2\n19,77\n9,-12\n38,1\n86,45057\n87,44\n",
  "CRLF and spaces\r\ncolor=blue\r\nver=113\r\n 0 , 3 \r\n19,\t300\r\n9,-99\r\n200,5\r\nbad line\r\n45,x\r\n,5\r\n7,\r\n",
  "No version line\n0,2\n27,64\n43,90\n50,100\n38,1\n75,80\n",
  "Old 105\nver=105\n13,40\n21,100\n37,64\n52,10\n55,99\n38,0\n",
  "Old 107 mono\nver=107\n38,2\n75,75\n93,7\n",
  "Old 108 poly\nver=108\n38,0\n75,20\n",
  "A name that is much longer than twenty-three characters\nver=112\n0,0\n",
  "Näme wïtñ bytes\nver=112\n5,64\n",
  "\nver=112\n5,64\n",
  "Only a name",
  "ver=110 as the name\nver=xyz\nver= 109 \n1,4\n1,0\n1,9\n",
];
for (let k = 0; k < 40; k++) {            // random patches in random versions, values in and out of range
  let t = `Random ${k}\ncolor=green\n` + (k % 5 ? `ver=${[100, 105, 106, 107, 108, 109, 112, 113][rnd(8)]}\n` : "");
  for (let i = 0; i < 99; i++) if (rnd(4)) t += `${i},${rnd(3) ? rnd(128) : rnd(70000) - 200}\n`;
  texts.push(t);
}

const files = texts.map((t, i) => {
  const f = join(dir, `p${String(i).padStart(3, "0")}.sy1`);
  writeFileSync(f, Buffer.from(t, "latin1"));
  return f;
});
const syx = join(dir, "out.syx");
let listing = "";
try {
  listing = execFileSync("build/sy1pack", [syx, "-first", "3", ...files], { encoding: "latin1", stdio: ["ignore", "pipe", "ignore"] });
} catch (e) {
  listing = e.stdout || "";               // (it exits 1 when it skipped every file; here some are skipped on purpose)
}
const want = readFileSync(syx);

let at = 0, slot = 2, made = 0;
for (const [i, t] of texts.entries()) {
  const p = parseSy1(new Uint8Array(Buffer.from(t, "latin1")));
  if (!p.lines) continue;                 // sy1pack skips these too: the frames must still line up
  const f = frameFor(p, slot++);
  const c = want.subarray(at, at + f.length);
  check(Buffer.compare(Buffer.from(f), c) === 0, `patch ${i} (${JSON.stringify(nameText(p))}, ver ${p.version}): the frame differs from sy1pack's`);
  at += f.length;
  made++;
}
check(at === want.length, `sy1pack wrote ${want.length} bytes, the page's reader ${at}`);
check(made === texts.length - 1, `${made} patches made of ${texts.length} texts (one has no parameter lines)`);
check(listing.includes("old version, converted"), "sy1pack's listing names the old versions");

// a zip bank: stored and deflated entries, a folder, a file that is not a patch, a macOS resource fork
function zip(entries) {
  const parts = [], central = [];
  let off = 0;
  for (const { name, data, deflate } of entries) {
    const body = deflate ? deflateRawSync(data) : data, n = Buffer.from(name, "latin1");
    const head = Buffer.alloc(30);
    head.writeUInt32LE(0x04034b50, 0); head.writeUInt16LE(20, 4); head.writeUInt16LE(deflate ? 8 : 0, 8);
    head.writeUInt32LE(body.length, 18); head.writeUInt32LE(data.length, 22); head.writeUInt16LE(n.length, 26);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 6); c.writeUInt16LE(deflate ? 8 : 0, 10);
    c.writeUInt32LE(body.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(n.length, 28);
    c.writeUInt32LE(off, 42);
    parts.push(head, n, body);
    central.push(c, n);
    off += 30 + n.length + body.length;
  }
  const cd = Buffer.concat(central), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(off, 16);
  return Buffer.concat([...parts, cd, end]);
}
const z = zip([
  { name: "bank/002.sy1", data: Buffer.from(texts[1], "latin1"), deflate: true },
  { name: "bank/readme.txt", data: Buffer.from("hello"), deflate: false },
  { name: "bank/001.sy1", data: Buffer.from(texts[0], "latin1"), deflate: false },
  { name: "__MACOSX/bank/._001.sy1", data: Buffer.from("junk"), deflate: false },
  { name: "bank/003.SY1", data: Buffer.from(texts[3], "latin1"), deflate: true },
]);
const got = await unzipSy1(z.buffer.slice(z.byteOffset, z.byteOffset + z.length));
check(got.length === 3 && got[0].name === "bank/001.sy1" && got[1].name === "bank/002.sy1" && got[2].name === "bank/003.SY1",
  "zip: " + got.map((g) => g.name).join(", "));
check(got.length === 3 && Buffer.from(got[0].bytes).toString("latin1") === texts[0] && Buffer.from(got[1].bytes).toString("latin1") === texts[1]
  && Buffer.from(got[2].bytes).toString("latin1") === texts[3], "zip: the entries' bytes");
let threw = false;
await unzipSy1(new Uint8Array(100).buffer).catch(() => { threw = true; });
check(threw, "a file that is not a zip is refused");

const nested = await unzipSy1(zip([{name:'Collection.zip',data:zip([{name:'Bank.zip',data:z,deflate:true}]),deflate:true}]));
check(nested.length===3 && nested[0].name==='Collection.zip/Bank.zip/bank/001.sy1','nested banks preserve provenance and bytes');
async function refuses(bytes,what){let refused=false;try{await unzipSy1(bytes);}catch{refused=true;}check(refused,what);}
const damaged=Buffer.from(z);damaged.writeUInt32LE(0xffffffff,z.length-6);await refuses(damaged,'bad central directory rejected');
const encrypted=Buffer.from(z),cd=encrypted.readUInt32LE(encrypted.length-6);encrypted.writeUInt16LE(1,cd+8);await refuses(encrypted,'password-protected ZIP rejected');
const huge=Buffer.from(z);huge.writeUInt32LE(65*1024*1024,cd+24);await refuses(huge,'oversized expanded archive rejected before allocation');
const mismatch=Buffer.from(z);mismatch.writeUInt32LE(1,cd+24);await refuses(mismatch,'wrong expanded length rejected');
let deep=z;for(let i=0;i<6;i++)deep=zip([{name:'nested.zip',data:deep}]);await refuses(deep,'excessive ZIP nesting rejected');

console.log(`web loader: ${checks} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
