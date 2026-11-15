const BANDS = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
const BAND_Q = 1.1;
const RAMP_SECONDS = 0.04;
const SWAP_MS = 60;
const PITCH_FRAMES = 4096;
export const PITCH_LATENCY_FRAMES = PITCH_FRAMES * 4.5;

function createPitchStage(ctx, destination) {
  const input = ctx.createGain();
  const processor = ctx.createScriptProcessor(PITCH_FRAMES, 2, 2);
  const interleaved = new Float32Array(PITCH_FRAMES * 2);
  const shiftedFrames = new Float32Array(PITCH_FRAMES * 2);
  let shifter = null;
  let primed = false;
  let semitones = 0;
  processor.onaudioprocess = (e) => {
    const left = e.inputBuffer.getChannelData(0);
    const right = e.inputBuffer.getChannelData(1);
    for (let i = 0; i < PITCH_FRAMES; i++) {
      interleaved[2 * i] = left[i];
      interleaved[2 * i + 1] = right[i];
    }
    shifter.inputBuffer.putSamples(interleaved, 0, PITCH_FRAMES);
    shifter.process();
    primed ||= shifter.outputBuffer.frameCount >= PITCH_FRAMES * 2;
    const count = primed ? Math.min(shifter.outputBuffer.frameCount, PITCH_FRAMES) : 0;
    shifter.outputBuffer.receiveSamples(shiftedFrames, count);
    const outLeft = e.outputBuffer.getChannelData(0);
    const outRight = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < PITCH_FRAMES; i++) {
      outLeft[i] = i < count ? shiftedFrames[2 * i] : 0;
      outRight[i] = i < count ? shiftedFrames[2 * i + 1] : 0;
    }
  };
  input.connect(destination);
  return {
    input,
    latency: () => (semitones ? PITCH_LATENCY_FRAMES / ctx.sampleRate : 0),
    set(value) {
      const engaged = semitones !== 0;
      semitones = value;
      if (shifter) shifter.pitchSemitones = value;
      if (engaged === (value !== 0)) return;
      input.disconnect();
      if (value === 0) {
        processor.disconnect();
        input.connect(destination);
        return;
      }
      shifter = new SoundTouchJS.SoundTouch();
      shifter.pitchSemitones = value;
      primed = false;
      input.connect(processor);
      processor.connect(destination);
    }
  };
}

function impulse(ctx, seconds, decay) {
  const length = Math.round(ctx.sampleRate * seconds);
  const buffer = ctx.createBuffer(2, length, ctx.sampleRate);
  for (let channel = 0; channel < 2; channel++) {
    const data = buffer.getChannelData(channel);
    for (let i = 0; i < length; i++) data[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / length, decay);
  }
  return buffer;
}

export function createEffectsGraph(ctx, destination) {
  const preamp = ctx.createGain();
  const pitch = createPitchStage(ctx, preamp);
  const bands = BANDS.map((frequency) => {
    const filter = ctx.createBiquadFilter();
    filter.type = 'peaking';
    filter.frequency.value = frequency;
    filter.Q.value = BAND_Q;
    filter.gain.value = 0;
    return filter;
  });
  const limiter = ctx.createDynamicsCompressor();
  limiter.threshold.value = -1;
  limiter.knee.value = 0;
  limiter.ratio.value = 20;
  limiter.attack.value = 0.003;
  limiter.release.value = 0.1;
  const wet = ctx.createGain();
  wet.gain.value = 0;
  const convolver = ctx.createConvolver();
  bands.reduce((prev, band) => (prev.connect(band), band), preamp);
  bands.at(-1).connect(limiter);
  limiter.connect(destination);
  limiter.connect(convolver).connect(wet).connect(destination);
  let reverbKey = null;

  return {
    input: pitch.input,
    setPitch: pitch.set,
    latency: pitch.latency,
    apply(settings) {
      const now = ctx.currentTime;
      const boost = Math.max(0, ...settings.eq);
      preamp.gain.setTargetAtTime(Math.pow(10, -boost / 40), now, RAMP_SECONDS);
      bands.forEach((band, i) => band.gain.setTargetAtTime(settings.eq[i], now, RAMP_SECONDS));
      const reverb = settings.reverb;
      if (!reverb) {
        wet.gain.setTargetAtTime(0, now, RAMP_SECONDS);
        return;
      }
      const key = `${reverb.seconds}:${reverb.decay}`;
      if (key === reverbKey) {
        wet.gain.setTargetAtTime(reverb.wet, now, RAMP_SECONDS);
        return;
      }
      wet.gain.setTargetAtTime(0, now, RAMP_SECONDS / 2);
      setTimeout(() => {
        convolver.buffer = impulse(ctx, reverb.seconds, reverb.decay);
        reverbKey = key;
        wet.gain.setTargetAtTime(reverb.wet, ctx.currentTime, RAMP_SECONDS);
      }, SWAP_MS);
    }
  };
}
