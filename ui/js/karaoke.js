import { PITCH_WORKLET_PATH, createEffectsGraph } from './effectsGraph.js';

const SAMPLE_RATE = 44100;
const LOOKAHEAD_SEGMENTS = 2;
const DRIFT_SECONDS = 0.05;
const START_LEAD_SECONDS = 0.05;

const segmentName = (stem, index) => `${stem}_${String(index).padStart(4, '0')}.opus`;

export function createKaraoke() {
  let stems = null;
  let ctx = null;
  let master = null;
  let accompaniment = null;
  let vocals = null;
  let effects = null;
  let volume = 0.6;
  let vocal = 0;
  let effectSettings = null;
  let semitones = 0;
  let song = null;
  let playing = null;
  const known = new Map();
  const loadedListeners = [];

  function ensureContext() {
    if (ctx) return;
    ctx = new AudioContext();
    master = ctx.createGain();
    accompaniment = ctx.createGain();
    vocals = ctx.createGain();
    effects = createEffectsGraph(ctx, ctx.destination, location.origin + PITCH_WORKLET_PATH);
    accompaniment.connect(master);
    vocals.connect(master);
    master.connect(effects.input);
    master.gain.value = volume;
    vocals.gain.value = vocal;
    if (effectSettings) effects.apply(effectSettings);
    effects.setPitch(semitones);
  }

  const segmentAt = (seconds) => Math.floor((seconds * SAMPLE_RATE) / song.stride);
  const segmentStart = (index) => (index * song.stride) / SAMPLE_RATE;
  const position = () => playing.anchorPosition + (ctx.currentTime - playing.anchorTime);
  const heard = () => position() - effects.latency();

  async function load(index) {
    if (!song.ready.has(index) || song.buffers.has(index) || song.loading.has(index)) return;
    const current = song;
    current.loading.add(index);
    const responses = await Promise.all(['acc', 'voc'].map((stem) => fetch(`${stems}/${current.id}/${segmentName(stem, index)}`)));
    for (const res of responses) if (!res.ok) throw new Error(`伴唱第 ${index} 段讀不到：${res.status}`);
    const [acc, voc] = await Promise.all(responses.map(async (res) => ctx.decodeAudioData(await res.arrayBuffer())));
    current.loading.delete(index);
    current.buffers.set(index, { acc, voc });
    if (song !== current) return;
    schedule();
    for (const listener of loadedListeners) listener();
  }

  function schedule() {
    if (!playing) return;
    const now = ctx.currentTime;
    for (const [index, buffers] of song.buffers) {
      if (playing.scheduled.has(index)) continue;
      const ideal = playing.anchorTime + (segmentStart(index) - playing.anchorPosition);
      if (ideal + buffers.acc.duration <= now) continue;
      const at = Math.max(ideal, now + START_LEAD_SECONDS);
      const offset = at - ideal;
      for (const [buffer, target] of [
        [buffers.acc, accompaniment],
        [buffers.voc, vocals]
      ]) {
        const source = ctx.createBufferSource();
        source.buffer = buffer;
        source.connect(target);
        source.start(at, offset);
        playing.sources.push(source);
      }
      playing.scheduled.add(index);
    }
  }

  function stop() {
    if (!playing) return;
    for (const source of playing.sources) source.stop();
    playing = null;
  }

  function start(seconds) {
    stop();
    playing = {
      anchorTime: ctx.currentTime + START_LEAD_SECONDS,
      anchorPosition: seconds + START_LEAD_SECONDS + effects.latency(),
      sources: [],
      scheduled: new Set()
    };
    schedule();
  }

  return {
    setStems(url) {
      stems = url;
    },
    setSong(id) {
      stop();
      const seen = known.get(id);
      song = id
        ? { id, stride: seen?.stride ?? 0, total: seen?.total ?? 0, ready: new Set(seen?.ready), buffers: new Map(), loading: new Set() }
        : null;
    },
    async onStatus(status) {
      if (!known.has(status.id)) known.set(status.id, { stride: 0, total: 0, ready: new Set() });
      const seen = known.get(status.id);
      if (status.state === 'separating') {
        seen.stride = status.stride;
        seen.total = status.total;
      } else if (status.state === 'segment') {
        seen.ready.add(status.segment);
      }
      if (!song || status.id !== song.id) return;
      if (status.state === 'separating') {
        song.stride = status.stride;
        song.total = status.total;
      } else if (status.state === 'segment') {
        song.ready.add(status.segment);
      } else if (status.state === 'complete') {
        const current = song;
        const meta = await (await fetch(`${stems}/${current.id}/segments.json`)).json();
        current.stride = meta.stride;
        current.total = meta.segments;
        for (let i = 0; i < meta.segments; i++) current.ready.add(i);
      }
    },
    progress: () => (song && song.total ? { ready: song.ready.size, total: song.total } : null),
    onLoaded: (listener) => loadedListeners.push(listener),
    sync(seconds, isPlaying) {
      if (!song || !song.stride) return false;
      ensureContext();
      if (ctx.state === 'suspended') ctx.resume();
      const index = segmentAt(seconds);
      for (let i = index; i <= index + LOOKAHEAD_SEGMENTS; i++) load(i);
      if (!song.buffers.has(index)) {
        stop();
        return false;
      }
      if (!isPlaying) {
        stop();
        return true;
      }
      if (!playing || Math.abs(heard() - seconds) > DRIFT_SECONDS) start(seconds);
      else schedule();
      return true;
    },
    setVolume(value) {
      volume = value;
      if (master) master.gain.value = value;
    },
    setVocal(value) {
      vocal = value;
      if (vocals) vocals.gain.value = value;
    },
    setEffects(settings) {
      effectSettings = settings;
      if (effects) effects.apply(settings);
    },
    setPitch(value) {
      semitones = value;
      if (!effects) return;
      const at = playing ? heard() : null;
      effects.setPitch(value);
      if (at !== null) start(at);
    }
  };
}
