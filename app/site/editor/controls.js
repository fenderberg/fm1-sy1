import t from './tables.js';
import {options, readout, sections} from './panel.js';

export function controls(getValues, edit) {
  const layout = document.getElementById('modules'), tabs = document.getElementById('sections');
  let current = 'Synthesis', refreshers;
  function makeControl(id, label, group) {
    const el = document.createElement('div'), title = document.createElement('label');
    el.className = 'control';
    title.textContent = label;
    title.htmlFor = 'p' + id;
    el.append(title);
    const list = options(id), min = t.param_min[id], max = t.param_max[id];
    const value = () => getValues()[id];
    let field, update;
    if (list) {
      el.classList.add('menu');
      field = document.createElement('select');
      for (const [n,text] of list) field.add(new Option(text,String(n)));
      if (!list.some(([n]) => n === value())) field.add(new Option('Stored value ' + value(),String(value())));
      field.onchange = () => edit(id, Number(field.value));
      el.append(field);
      update = () => { field.value = value(); };
    } else if (max - min === 1) {
      field = document.createElement('button');
      field.className = 'switch';
      field.onclick = () => edit(id, value() === min ? max : min);
      el.append(field);
      update = () => { field.textContent = value() ? 'On' : 'Off'; field.setAttribute('aria-pressed', !!value()); };
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
      dial.append(field); el.append(dial,output);
      update = () => {
        field.value = value();
        dial.style.setProperty('--arc', (value() - min) / (max - min) * 270 + 'deg');
        output.textContent = readout(id,getValues()); field.setAttribute('aria-valuetext',output.textContent);
      };
    }
    field.id = 'p' + id;
    field.setAttribute('aria-label', group + ' ' + label);
    refreshers.push(update); update();
    return el;
  }
  function graph(title) {
    const osc = ['Oscillator 1','Oscillator 2'].indexOf(title), env = ['Filter envelope','Amplifier'].indexOf(title);
    if (osc < 0 && env < 0) return;
    const c = document.createElement('canvas'); c.width = 148; c.height = 48;
    c.setAttribute('aria-hidden','true');
    const draw = () => {
      const ctx = c.getContext('2d'), v = getValues();
      ctx.clearRect(0,0,148,48); ctx.strokeStyle = '#394339'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(0,24); ctx.lineTo(148,24); ctx.stroke();
      ctx.strokeStyle = '#dfc27c'; ctx.lineWidth = 2; ctx.beginPath();
      if (osc >= 0) {
        const wave = v[osc];
        for (let i=0; i<148; i++) {
          const p = i % 68 / 68;
          const amp = [Math.sin(p*2*Math.PI),p*2-1,p < Math.max(.01,v[8]/254) ? -1 : 1,1-4*Math.abs(p-.5),Math.sin(i*209)*Math.cos(i*37)][wave];
          ctx.lineTo(i,24-amp*16);
        }
      } else {
        const id = env ? 25 : 15, a = 3+v[id]*.28, d = a+4+v[id+1]*.25, level = 43-v[id+2]*.28;
        ctx.moveTo(1,44); ctx.lineTo(a,5); ctx.lineTo(d,level); ctx.lineTo(143-v[id+3]*.4,level); ctx.lineTo(146,44);
      }
      ctx.stroke();
    };
    refreshers.push(draw); draw(); return c;
  }
  function render() {
    refreshers = []; layout.replaceChildren();
    for (const tab of tabs.children) tab.setAttribute('aria-pressed',tab.textContent === current);
    for (const [title,params] of sections[current]) {
      const module = document.createElement('section'), header = document.createElement('header');
      const h = document.createElement('h2'), grid = document.createElement('div');
      module.className = 'module'; h.textContent = title; grid.className = 'controls';
      header.append(h); const visual = graph(title); if (visual) header.append(visual);
      for (const [id,label] of params) grid.append(makeControl(id,label,title));
      module.append(header,grid); layout.append(module);
    }
  }
  for (const section of Object.keys(sections)) {
    const b = document.createElement('button'); b.textContent = section;
    b.onclick = () => { current = section; render(); }; tabs.append(b);
  }
  render();
  return {render, update: () => refreshers.forEach(fn => fn())};
}
