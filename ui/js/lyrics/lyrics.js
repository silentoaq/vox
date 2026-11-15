import { toSimplified, toTraditional } from '../chinese.js';
import { sourceOf } from '../sources.js';
import { alignWords, evenChars } from './align.js';
import { kugouLines } from './kugou.js';

const CONVERTERS = { hant: toTraditional, hans: toSimplified };
const KANA = /[\u3040-\u30ff]/;

export function inScript(lines, script) {
  const convert = CONVERTERS[script];
  if (!convert || lines.some((line) => KANA.test(line.text))) return lines;
  return lines.map((line) => {
    const text = convert(line.text);
    return [...text].length === [...line.text].length ? { ...line, text } : line;
  });
}

const withEvenChars = (lines) =>
  lines.filter((line) => line.text).map((line) => ({ ...line, chars: line.chars ?? evenChars(line) }));

export async function lyricsFor(track) {
  const source = sourceOf(track.source);
  let kugouError = null;
  const [lines, krc] = await Promise.all([
    source.timedLines(track),
    kugouLines(track).catch((e) => {
      kugouError = e;
      console.error('酷狗歌詞失敗', e);
      return [];
    })
  ]);
  const note = kugouError ? `，酷狗失敗：${kugouError.message}` : '';
  if (lines.length && krc.length) {
    const merged = alignWords(lines, krc);
    const worded = merged.filter((line) => line.chars).length;
    const total = merged.filter((line) => line.text).length;
    return { label: `${source.name} 行時間＋酷狗逐字（${worded}/${total} 行）`, lines: withEvenChars(merged) };
  }
  if (lines.length) return { label: `${source.name} 行時間${note}`, lines: withEvenChars(lines) };
  if (krc.length) return { label: '酷狗逐字', lines: krc };
  return { label: `沒有歌詞${note}`, lines: [] };
}
