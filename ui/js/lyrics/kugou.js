import { toSimplified } from '../chinese.js';
import { fetchJson } from '../host.js';

const KRC_KEY = [0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69];
const DURATION_TOLERANCE = 4;

const query = (params) => new URLSearchParams(params).toString();
const isSpace = (ch) => /\s/.test(ch);

async function decodeKrc(base64) {
  const raw = Uint8Array.from(atob(base64), (ch) => ch.charCodeAt(0)).subarray(4);
  const plain = raw.map((byte, i) => byte ^ KRC_KEY[i % KRC_KEY.length]);
  const stream = new Blob([plain]).stream().pipeThrough(new DecompressionStream('deflate'));
  return new Response(stream).text();
}

function parseKrc(text) {
  const lines = [];
  for (const line of text.replace(/\r/g, '').matchAll(/^\[(\d+),(\d+)\](.*)$/gm)) {
    const start = Number(line[1]);
    const words = [...line[3].matchAll(/<(\d+),(\d+),\d+>([^<]*)/g)].map((word) => ({
      t: start + Number(word[1]),
      d: Number(word[2]),
      text: word[3]
    }));
    const plain = words.map((word) => word.text).join('');
    if (!plain.trim() || /[:：]/.test(plain)) continue;
    const chars = [];
    let at = 0;
    for (const word of words) {
      const pieces = [...word.text];
      pieces.forEach((ch, k) => {
        if (!isSpace(ch))
          chars.push({
            t: word.t + Math.floor((word.d * k) / pieces.length),
            e: word.t + Math.floor((word.d * (k + 1)) / pieces.length),
            i: at
          });
        at++;
      });
    }
    lines.push({ start, end: start + Number(line[2]), text: plain, chars });
  }
  return lines;
}

export async function kugouLines(track) {
  const keyword = toSimplified(`${track.artist} ${track.title}`);
  const found = await fetchJson(
    `http://mobilecdn.kugou.com/api/v3/search/song?${query({ format: 'json', keyword, page: 1, pagesize: 8, showtype: 1 })}`
  );
  const songs = found.data.info;
  if (!songs.length) return [];
  const gap = (song) => Math.abs(song.duration - track.duration);
  const song = track.duration ? songs.reduce((best, song) => (gap(song) < gap(best) ? song : best)) : songs[0];
  if (track.duration && gap(song) > DURATION_TOLERANCE) return [];

  const { candidates = [] } = await fetchJson(
    `https://krcs.kugou.com/search?${query({ ver: 1, man: 'yes', client: 'mobi', keyword: '', duration: song.duration * 1000, hash: song.hash })}`
  );
  if (!candidates.length) return [];
  const [candidate] = candidates;
  const { content } = await fetchJson(
    `https://lyrics.kugou.com/download?${query({ ver: 1, client: 'pc', id: candidate.id, accesskey: candidate.accesskey, fmt: 'krc', charset: 'utf8' })}`
  );
  return parseKrc(await decodeKrc(content));
}
