import t from './tables.js';

// Rows follow the controls' positions on the original front panel. Each ID is a .sy1 parameter.
export const modules = [
  {area:'osc', name:'Oscillators', groups:[
    ['osc-one','1', [[0,'wave'],[76,'det'],[45,'FM']]],
    ['sub','sub', [[96,'wave'],[95,'level'],[97,'oct']]],
    ['osc-two','2', [[1,'wave'],[7,'ring'],[6,'sync'],[4,'track'],[2,'pitch'],[3,'fine']]],
    ['mix','', [[9,'key shift'],[5,'mix'],[8,'p/w'],[91,'phase'],[72,'tune']]],
    ['mod-env','', [[10,'m.env'],[12,'A'],[13,'D'],[11,'amt'],[71,'dest.']]],
  ]},
  {area:'amp', name:'Amplifier', groups:[['','',[[25,'A'],[26,'D'],[27,'S'],[28,'R'],[29,'gain'],[30,'vel']]]]},
  {area:'filter', name:'Filter', groups:[
    ['filter-knobs','',[[15,'A'],[16,'D'],[17,'S'],[18,'R'],[21,'amt'],[19,'frq'],[20,'res'],[23,'sat'],[22,'trk'],[24,'vel']]],
    ['filter-type','',[[14,'type']]],
  ]},
  {area:'lfo', name:'LFO', groups:[
    ['lfo-one','',[[57,'1'],[42,'wave'],[41,'dest'],[43,'spd'],[44,'amt'],[67,'tempo'],[68,'key']]],
    ['lfo-two','',[[58,'2'],[47,'wave'],[46,'dest'],[48,'spd'],[49,'amt'],[69,'tempo'],[70,'key']]],
  ]},
  {area:'arp', name:'Arpeggiator', groups:[['','',[[59,'on'],[31,'type'],[32,'range'],[33,'beat'],[34,'gate']]]]},
  {area:'wheel', name:'Wheel / MIDI', groups:[
    ['bend','',[[40,'p.b range']]],
    ['route','src 1',[[86,'source'],[50,'sens'],[87,'destination']]],
    ['route','src 2',[[88,'source'],[51,'sens'],[89,'destination']]],
  ]},
  {area:'fx', name:'Effect', groups:[['','',[[77,'on'],[78,'type'],[79,'ctl 1'],[80,'ctl 2'],[81,'level']]]]},
  {area:'eq', name:'Equalizer / Pan', groups:[['','',[[61,'freq'],[62,'level'],[63,'Q'],[60,'tone'],[90,'L–R']]]]},
  {area:'delay', name:'Tempo Delay', groups:[['','',[[65,'on'],[82,'type'],[35,'time'],[83,'sprd'],[36,'fdbk'],[98,'tone'],[37,'d/w']]]]},
  {area:'chorus', name:'Chorus / Flanger', groups:[['','',[[66,'on'],[64,'type'],[52,'time'],[53,'depth'],[54,'rate'],[55,'fdbk'],[56,'level']]]]},
  {area:'voice', name:'Voice', groups:[
    ['voice-mode','',[[38,'mode'],[94,'poly']]],
    ['glide','portamento',[[39,'time'],[74,'auto']]],
    ['unison','',[[73,'unison'],[93,'num'],[75,'det'],[92,'phase'],[84,'sprd'],[85,'pitch']]],
  ]},
];

const waves = ['Saw','Triangle','Square','Sample & hold','Random','Sine'];
const targets = ['Osc 2','Osc 1 + 2','Filter','Amp','Pulse width','FM','Pan'];
const beats = ['4','7/2','3','2','7/4','3/2','4/3','1','7/8','3/4','2/3','1/2','3/8','1/3','1/4','1/6','1/8','1/12','1/24'];
const lfoBeats = ['64','48','36','32','24','18','16','12','9','8','6',...beats];
const choices = {
  0:['Sine','Saw','Pulse','Triangle'], 1:['Saw','Pulse','Triangle','Noise'],
  14:['Low-pass 12','Low-pass 24','High-pass 12','Band-pass 12','LPDL'],
  31:['Up / down','Up','Down','Random'], 32:['1','2','3','4'], 33:beats,
  35:['Minimum','1/24','1/12','1/8','1/6','1/4','1/3','3/8','1/2','2/3','3/4','7/8','1','4/3','3/2','7/4','2','3','7/2','4'],
  38:['Poly','Mono','Legato'], 41:targets, 42:waves, 46:targets, 47:waves,
  64:['Single','Stereo 1','Stereo 2','Quad'], 71:['Osc 2','FM','Pulse width'],
  78:['Analog dist. 1','Analog dist. 2','Digital dist.','Decimator','Ring mod','Compressor','Phaser 1','Phaser 2','Phaser 3','Phaser 4'],
  82:['Stereo','Cross','Ping-pong'], 96:['Sine','Triangle','Saw','Square'], 97:['Same','−1 octave'],
};

export function options(id) {
  if (choices[id]) return choices[id].map((label,i) => [t.param_min[id] + i, label]);
  if (id === 86 || id === 88) {
    return [[-1,'Off'], [0xe000,'Pitch bend'], [0xd000,'Aftertouch'],
      ...Array.from({length:128}, (_,i) => [0xb000 + i, i === 1 ? 'CC 1 · Mod wheel' : 'CC ' + i])];
  }
  if (id === 87 || id === 89) {
    return [[-1,'Off'], ...t.param_name.flatMap((name,i) => t.param_min[i] === 0 && t.param_max[i] === 127 ? [[i, name]] : [])];
  }
}

export function readout(id, values) {
  const n = values[id];
  const seconds = [12,15,25].includes(id) ? t.t_attack_s[n] : [13,16,18,26,28].includes(id) ? t.t_env_tau_s[n] : null;
  if (seconds !== null) return seconds >= 1 ? Number(seconds.toFixed(2)) + ' s' : Number((seconds * 1000).toFixed(1)) + ' ms';
  if (id === 43 || id === 48) return values[id === 43 ? 67 : 69] ? lfoBeats[Math.floor(n * 30 / 128)] + ' beat' : Number((2 ** (n / 12.0367) / 12).toFixed(2)) + ' Hz';
  if (id === 2) return t.t_osc2_semis[n] + ' st';
  if ([9,40,85].includes(id)) return n + ' st';
  if (id === 3 || id === 72) return t.t_fine_cents[n] + ' ct';
  if (id === 75) return Number(t.t_unison_cents[n].toFixed(1)) + ' ct';
  if (id === 27) return Math.round(t.t_amp_sustain[n] * 100) + '%';
  if ([17,34].includes(id)) return Math.round(n / 127 * 100) + '%';
  if (id === 61) return Math.round(50 * 320 ** (n / 127)) + ' Hz';
  if (id === 62) return Number(((n - 64) * 50 / 127).toFixed(1)) + ' dB';
  return String(n);
}
