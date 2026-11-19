import { sourceOf, sources } from './sources.js';

export function createPlayback(element, handlers) {
  let active = null;
  let wanted = false;
  const mounted = new Map();
  const mount = (source) => {
    if (!mounted.has(source)) mounted.set(source, source.player.mount(element, handlers));
    return mounted.get(source);
  };
  const ready = mount(sources[0]);

  return {
    ready,
    async load(track, autoplay) {
      wanted = autoplay;
      const next = sourceOf(track.source);
      if (active && active !== next) active.player.stop();
      active = next;
      element.dataset.active = track.source;
      await mount(next);
      if (active === next) next.player.load(track, autoplay);
    },
    toggle() {
      if (!active) return;
      wanted = !active.player.isPlaying();
      if (wanted) active.player.play();
      else active.player.pause();
    },
    play() {
      wanted = true;
      active?.player.play();
    },
    pause() {
      wanted = false;
      active?.player.pause();
    },
    stop() {
      wanted = false;
      active?.player.stop();
      active = null;
      delete element.dataset.active;
    },
    seek: (sec) => active?.player.seek(sec),
    setVolume(volume) {
      for (const source of sources) source.player.setVolume(volume);
    },
    setMuted: (muted) => active?.player.setMuted(muted),
    setEffects(settings) {
      for (const source of sources) source.player.setEffects?.(settings);
    },
    setPitch(semitones) {
      for (const source of sources) source.player.setPitch(semitones);
    },
    time: () => (active ? active.player.currentTime() : 0),
    duration: () => (active ? active.player.duration() : 0),
    isPlaying: () => (active ? active.player.isPlaying() : false),
    wantsToPlay: () => wanted
  };
}
