import { spotify } from './sources/spotify.js';
import { youtubeMusic } from './sources/youtubeMusic.js';

export const sources = [youtubeMusic, spotify];

export function sourceOf(id) {
  const source = sources.find((candidate) => candidate.id === id);
  if (!source) throw new Error(`沒有這個來源：${id}`);
  return source;
}
