// The main thread keeps track of the keys while the audio worklet starts.
// A note released during startup must never start playing afterwards.
export function keyboard(getValues, report) {
  const el = document.getElementById('keyboard'), octave = document.getElementById('octave');
  const hold = document.getElementById('hold'), owners = new Map(), keys = [];
  const shortcuts = 'awsedftgyhujk';
  const notes = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
  let ctx, node, starting;
  const send = data => node?.port.postMessage(data);
  const base = () => (Number(octave.value) + 1) * 12;
  const pressed = note => [...owners.values()].includes(note);
  function lights() {
    keys.forEach((b,i) => b.setAttribute('aria-pressed', pressed(base() + i)));
    hold.setAttribute('aria-pressed', owners.has('hold'));
    hold.textContent = (owners.has('hold') ? 'Release C' : 'Hold C') + octave.value;
  }
  function stop() {
    owners.clear();
    send({type:'stop'});
    lights();
  }
  async function start() {
    if (node) { if (ctx.state !== 'running') await ctx.resume(); return; }
    if (starting) return starting;
    starting = (async () => {
      ctx = new AudioContext({latencyHint:'interactive'});
      const [module] = await Promise.all([
        fetch('engine.wasm').then(r => { if (!r.ok) throw Error('Audio engine unavailable'); return r.arrayBuffer(); }).then(b => WebAssembly.compile(b)),
        ctx.audioWorklet.addModule('processor.js'), ctx.resume()
      ]);
      node = new AudioWorkletNode(ctx,'sy1-editor',{numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{module}});
      node.connect(ctx.destination);
      send({type:'patch',values:getValues()});
      for (const note of new Set(owners.values())) send({type:'note',note,down:true});
    })().catch(async e => {
      node = null; stop(); await ctx?.close(); throw e;
    }).finally(() => { starting = null; });
    return starting;
  }
  function press(owner, note) {
    if (owners.has(owner)) return;
    const already = pressed(note);
    owners.set(owner,note);
    if (!already) send({type:'note',note,down:true});
    lights();
    start().catch(e => report('Audio could not start. ' + e.message, true));
  }
  function release(owner) {
    if (!owners.has(owner)) return;
    const note = owners.get(owner);
    owners.delete(owner);
    if (!pressed(note)) send({type:'note',note,down:false});
    lights();
  }
  function labelKeys() {
    keys.forEach((b,i) => {
      const name = notes[i % 12] + (Number(octave.value) + Math.floor(i / 12));
      b.setAttribute('aria-label', 'Play ' + name);
      b.textContent = (shortcuts[i] || '').toUpperCase() || (i % 12 === 0 ? name : '');
      b.title = name;
    });
    lights();
  }
  for (let i = 1; i <= 6; i++) octave.add(new Option(String(i), String(i), false, i === 4));
  let white = 0;
  for (let i = 0; i < 24; i++) {
    const b = document.createElement('button'), black = notes[i % 12].includes('♯');
    b.className = black ? 'black' : 'white';
    if (black) b.style.setProperty('--white-before', white);
    else white++;
    b.onpointerdown = e => { if (e.button !== 0) return; b.setPointerCapture(e.pointerId); press(e.pointerId, base() + i); };
    b.onlostpointercapture = b.onpointerup = b.onpointercancel = e => release(e.pointerId);
    b.onkeydown = e => { if (['Enter',' '].includes(e.key)) { e.preventDefault(); press('button', base() + i); } };
    b.onkeyup = b.onblur = () => release('button');
    b.onclick = e => { if (!e.detail && !owners.has('button') && e.pointerType === '') { press('assist',base()+i); setTimeout(() => release('assist'),300); } };
    keys.push(b); el.append(b);
  }
  octave.onchange = () => { stop(); labelKeys(); };
  hold.onclick = () => owners.has('hold') ? release('hold') : press('hold',base());
  window.addEventListener('keydown', e => {
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey || document.querySelector('dialog[open]') ||
        e.target.matches('input:not([type=range]),textarea,select,[contenteditable=true]')) return;
    const i = shortcuts.indexOf(e.key.toLowerCase());
    if (i >= 0 && e.key.length === 1) { e.preventDefault(); press(e.code, base() + i); }
  });
  window.addEventListener('keyup', e => release(e.code));
  window.addEventListener('blur', stop);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
  labelKeys();
  return {send,stop};
}
