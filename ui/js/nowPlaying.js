import { call } from './host.js';

export function createNowPlaying() {
  const sent = new Map();
  return {
    send(kind, data) {
      const text = JSON.stringify(data);
      if (sent.get(kind) === text) return;
      sent.set(kind, text);
      call(`nowPlaying.${kind}`, data);
    }
  };
}
