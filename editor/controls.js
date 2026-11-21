import t from './tables.js';
import {options, readout, modules} from './panel.js?v=classic';

const waveforms = {
  Sine: [[0,8],[2,4],[4,2],[6,3],[8,8],[10,13],[12,14],[14,12],[16,8]],
  Saw: [[0,14],[7,2],[7,14],[15,2],[15,14]],
  Pulse: [[0,13],[2,13],[2,3],[7,3],[7,13],[16,13]],
  Square: [[0,13],[2,13],[2,3],[10,3],[10,13],[16,13]],
  Triangle: [[0,13],[5,3],[11,13],[16,3]],
  'Sample & hold': [[0,11],[4,11],[4,3],[9,3],[9,10],[14,10],[14,6],[16,6]],
  Random: [[0,12],[4,3],[9,11],[14,6],[16,7]],
  Noise: [[0,10],[2,4],[4,12],[6,6],[8,10],[10,2],[12,14],[14,5],[16,9]],
};
const radioIds = new Set([0,1,96,97,14,31,32,38,41,46]);
const lfoTargets = ['osc2','osc1,2','filter','amp','p/w','FM','pan'];
const compactNames = {
  41:lfoTargets, 46:lfoTargets,
  14:['LP12','LP24','HP12','BP12','LPDL'], 31:['updown','up','down','random'],
  32:['1 oct','2 oct','3 oct','4 oct'], 38:['poly','mono','legato'],
  64:['×1','×2 I','×2 II','×4'], 78:['a.d.1','a.d.2','d.d.','deci','ring','comp','ph.1','ph.2','ph.3','ph.4'],
  82:['ST','cross','P-P'], 97:['0 oct','−1 oct'],
};
function waveIcon(wave) {
  const canvas = document.createElement('canvas');
  canvas.width = 32; canvas.height = 32; canvas.setAttribute('aria-hidden','true');
  const ctx = canvas.getContext('2d'); ctx.scale(2,2); ctx.strokeStyle = '#ffe8d7';
  ctx.lineWidth = 1.3; ctx.lineJoin = 'round'; ctx.beginPath();
  for (const [x,y] of waveforms[wave]) ctx.lineTo(x,y);
  ctx.stroke(); return canvas;
}

export function controls(getValues, edit) {
  const layout = document.getElementById('modules');
  let refreshers;
  function makeControl(id, label) {
    const el = document.createElement('div'), title = document.createElement('label');
    el.className = 'control';
    el.dataset.param = id;
    const name = t.param_name[id], list = options(id), min = t.param_min[id], max = t.param_max[id];
    title.textContent = label;
    title.htmlFor = 'p' + id;
    const value = () => getValues()[id];
    let field, update;
    if (radioIds.has(id)) {
      const isWave = [0,1,96].includes(id);
      el.classList.add('choices',isWave ? 'waves' : 'modes');
      field = document.createElement('div'); field.setAttribute('role','radiogroup');
      const inputs = [];
      list.forEach(([n,text],i) => {
        const option = document.createElement('label'), radio = document.createElement('input');
        radio.type = 'radio'; radio.name = 'choice' + id; radio.value = n;
        radio.setAttribute('aria-label',text); option.title = text;
        radio.onchange = () => { if (radio.checked) edit(id,n); };
        option.append(radio, isWave ? waveIcon(text) : compactNames[id][i]);
        field.append(option); inputs.push(radio);
      });
      el.append(field);
      update = () => { inputs.forEach(input => { input.checked = Number(input.value) === value(); }); };
    } else if (list) {
      el.classList.add('menu');
      field = document.createElement('select');
      list.forEach(([n,text],i) => {
        const opt = new Option(compactNames[id]?.[i] ?? text,String(n));
        opt.title = text; field.add(opt);
      });
      if (!list.some(([n]) => n === value())) field.add(new Option('Stored ' + value(),String(value())));
      field.onchange = () => edit(id, Number(field.value));
      el.append(field,title);
      let wave;
      if (id === 42 || id === 47) el.classList.add('wave-menu');
      update = () => {
        field.value = value(); field.title = field.selectedOptions[0]?.title || name;
        if (el.classList.contains('wave-menu') && wave !== field.title) {
          wave = field.title;
          el.querySelector('canvas')?.remove();
          el.append(waveIcon(wave));
        }
      };
    } else if ([9,40,93,94].includes(id)) {
      el.classList.add('numeric');
      field = document.createElement('input');
      field.type = 'number'; field.min = min; field.max = max; field.step = 1;
      field.onchange = () => {
        const n = field.valueAsNumber;
        if (Number.isFinite(n)) edit(id,Math.max(min,Math.min(max,Math.round(n))));
        field.value = value();
      };
      el.append(field,title);
      update = () => { if (document.activeElement !== field) field.value = value(); };
    } else if (max - min === 1) {
      el.classList.add('toggle');
      field = document.createElement('button');
      field.className = 'switch';
      field.onclick = () => edit(id, value() === min ? max : min);
      field.textContent = label; field.title = name;
      el.append(field);
      update = () => {
        const on = value() !== min;
        field.setAttribute('aria-pressed', on);
        if (label === 'on') field.textContent = on ? 'on' : 'off';
      };
    } else {
      const dial = document.createElement('div'), output = document.createElement('span');
      dial.className = 'dial'; output.className = 'readout';
      field = document.createElement('input');
      field.type = 'range'; field.min = min; field.max = max; field.step = 1;
      field.oninput = () => edit(id, Number(field.value));
      field.title = 'Drag up or right. Shift-drag for fine adjustments. Double-click to reset.';
      let drag;
      field.onpointerdown = e => {
        if (e.button !== 0) return;
        e.preventDefault(); field.focus({preventScroll:true}); field.setPointerCapture(e.pointerId);
        drag = {x:e.clientX,y:e.clientY,value:value()};
      };
      field.onpointermove = e => {
        if (!drag || !field.hasPointerCapture(e.pointerId)) return;
        const distance = e.clientX - drag.x + drag.y - e.clientY;
        edit(id, Math.max(min, Math.min(max, drag.value + Math.round(distance * (max-min) / (e.shiftKey ? 550 : 115)))));
      };
      field.onlostpointercapture = () => { drag = null; };
      field.ondblclick = () => edit(id,t.param_def[id]);
      dial.append(field); el.append(dial,title,output);
      update = () => {
        field.value = value();
        dial.style.setProperty('--arc', (value() - min) / (max - min) * 270 + 'deg');
        output.textContent = readout(id,getValues()); field.setAttribute('aria-valuetext',output.textContent);
      };
    }
    field.id = 'p' + id;
    field.setAttribute('aria-label', name);
    refreshers.push(update); update();
    return el;
  }
  function render() {
    refreshers = []; layout.replaceChildren();
    const effects = document.createElement('div'); effects.className = 'right-column';
    for (const {area,name,groups} of modules) {
      const module = document.createElement('section'), heading = document.createElement('h2');
      module.className = 'module ' + area; heading.textContent = name; module.append(heading);
      const body = document.createElement('div'); body.className = 'module-body';
      for (const [key,legend,params] of groups) {
        const row = document.createElement('div'); row.className = 'control-row ' + key;
        if (legend) {
          const tag = document.createElement('span'); tag.className = 'legend'; tag.textContent = legend;
          row.append(tag);
        }
        for (const [id,label] of params) row.append(makeControl(id,label));
        body.append(row);
      }
      module.append(body);
      (['fx','eq','delay','chorus','voice'].includes(area) ? effects : layout).append(module);
    }
    layout.append(effects);
  }
  render();
  return {render, update: () => refreshers.forEach(fn => fn())};
}
