import t from './tables.js';

// IDs are the numbers stored in a Synth1 patch.
export const sections = {
  Synthesis: [
    ['Oscillator 1', [[0,'Waveform'], [45,'FM amount'], [76,'Detune'], [8,'Pulse width']]],
    ['Oscillator 2', [[1,'Waveform'], [2,'Pitch'], [3,'Fine'], [4,'Key track'], [6,'Sync'], [7,'Ring mod']]],
    ['Mixer & sub', [[5,'Osc mix'], [95,'Sub level'], [96,'Sub wave'], [97,'Sub octave'], [9,'Transpose'], [72,'Fine tune'], [91,'Phase']]],
    ['Filter', [[14,'Type'], [19,'Cutoff'], [20,'Resonance'], [23,'Saturation'], [22,'Key track'], [21,'Env amount'], [24,'Velocity']]],
    ['Filter envelope', [[15,'Attack'], [16,'Decay'], [17,'Sustain'], [18,'Release']]],
    ['Amplifier', [[25,'Attack'], [26,'Decay'], [27,'Sustain'], [28,'Release'], [29,'Gain'], [30,'Velocity'], [90,'Pan']]],
  ],
  Modulation: [
    ['LFO 1', [[57,'Enabled'], [42,'Waveform'], [41,'Destination'], [43,'Speed'], [44,'Depth'], [67,'Tempo sync'], [68,'Key sync']]],
    ['LFO 2', [[58,'Enabled'], [47,'Waveform'], [46,'Destination'], [48,'Speed'], [49,'Depth'], [69,'Tempo sync'], [70,'Key sync']]],
    ['Mod envelope', [[10,'Enabled'], [71,'Destination'], [11,'Amount'], [12,'Attack'], [13,'Decay']]],
    ['Controller 1', [[86,'Source'], [87,'Destination'], [50,'Sensitivity']]],
    ['Controller 2', [[88,'Source'], [89,'Destination'], [51,'Sensitivity']]],
  ],
  Effects: [
    ['Effect', [[77,'Enabled'], [78,'Type'], [79,'Control 1'], [80,'Control 2'], [81,'Mix']]],
    ['Equalizer', [[60,'Tone'], [61,'Frequency'], [62,'Level'], [63,'Q']]],
    ['Tempo delay', [[65,'Enabled'], [82,'Type'], [35,'Time'], [36,'Feedback'], [37,'Mix'], [83,'Spread'], [98,'Tone']]],
    ['Chorus', [[66,'Enabled'], [64,'Type'], [52,'Time'], [53,'Depth'], [54,'Rate'], [55,'Feedback'], [56,'Level']]],
  ],
  Performance: [
    ['Voice', [[38,'Play mode'], [94,'Polyphony'], [40,'Bend range'], [39,'Glide time'], [74,'Auto glide']]],
    ['Unison', [[73,'Enabled'], [93,'Voices'], [75,'Detune'], [84,'Pan spread'], [85,'Pitch'], [92,'Phase']]],
    ['Arpeggiator', [[59,'Enabled'], [31,'Pattern'], [32,'Octaves'], [33,'Beat'], [34,'Gate']]],
  ],
};

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
