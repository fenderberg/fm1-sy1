class EditorSynth extends AudioWorkletProcessor {
  constructor({processorOptions}) {
    super();
    const api = new WebAssembly.Instance(processorOptions.module).exports;
    api.editor_init(sampleRate);
    this.api = api;
    this.left = new Float32Array(api.memory.buffer, api.editor_output(), 128);
    this.right = new Float32Array(api.memory.buffer, api.editor_output() + 512, 128);
    const patch = new Int32Array(api.memory.buffer, api.editor_patch(), 99);
    this.port.onmessage = ({data}) => {
      switch (data.type) {
        case 'patch': patch.set(data.values); api.editor_load(); break;
        case 'param': api.editor_param(data.id, data.value); break;
        case 'note': api.editor_key(data.note, data.down); break;
        case 'stop': api.editor_stop(); break;
      }
    };
  }

  process(inputs, [channels]) {
    const [left, right] = channels;
    for (let i = 0; i < left.length; i += 128) {
      const n = Math.min(128, left.length - i);
      this.api.editor_render(n);
      left.set(n === 128 ? this.left : this.left.subarray(0, n), i);
      right.set(n === 128 ? this.right : this.right.subarray(0, n), i);
    }
    return true;
  }
}
registerProcessor('sy1-editor', EditorSynth);
