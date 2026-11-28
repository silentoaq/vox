class VoxPitch extends AudioWorkletProcessor {
  constructor({ processorOptions }) {
    super();
    this.shifter = new SoundTouchJS.SoundTouch();
    this.shifter.pitchSemitones = processorOptions.semitones;
    this.latencyFrames = processorOptions.latencyFrames;
    this.fed = 0;
    this.interleaved = new Float32Array(0);
    this.port.onmessage = (e) => {
      this.shifter.pitchSemitones = e.data;
    };
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const frames = output[0].length;
    if (this.interleaved.length !== frames * 2) this.interleaved = new Float32Array(frames * 2);
    const input = inputs[0];
    const left = input[0];
    const right = input[1] ?? left;
    for (let i = 0; i < frames; i++) {
      this.interleaved[2 * i] = left ? left[i] : 0;
      this.interleaved[2 * i + 1] = right ? right[i] : 0;
    }
    this.shifter.inputBuffer.putSamples(this.interleaved, 0, frames);
    this.shifter.process();
    this.fed += frames;
    if (this.fed <= this.latencyFrames) return true;
    const count = Math.min(this.shifter.outputBuffer.frameCount, frames);
    this.shifter.outputBuffer.receiveSamples(this.interleaved, count);
    for (let i = 0; i < count; i++) {
      output[0][i] = this.interleaved[2 * i];
      output[1][i] = this.interleaved[2 * i + 1];
    }
    return true;
  }
}

registerProcessor('vox-pitch', VoxPitch);
