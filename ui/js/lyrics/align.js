import { toSimplified } from '../chinese.js';

const WORD_LINE_RATIO = 0.8;
const isSpace = (ch) => /\s/.test(ch);
const keys = new Map();

function keyOf(ch) {
  if (!keys.has(ch)) keys.set(ch, toSimplified(ch).toLowerCase());
  return keys.get(ch);
}

function longestMatch(a, b, positions, alo, ahi, blo, bhi) {
  let best = { i: alo, j: blo, size: 0 };
  let lengths = new Map();
  for (let i = alo; i < ahi; i++) {
    const next = new Map();
    for (const j of positions.get(a[i]) ?? []) {
      if (j < blo) continue;
      if (j >= bhi) break;
      const size = (lengths.get(j - 1) ?? 0) + 1;
      next.set(j, size);
      if (size > best.size) best = { i: i - size + 1, j: j - size + 1, size };
    }
    lengths = next;
  }
  return best;
}

function matchingBlocks(a, b) {
  const positions = new Map();
  b.forEach((item, j) => {
    if (!positions.has(item)) positions.set(item, []);
    positions.get(item).push(j);
  });
  const blocks = [];
  const queue = [[0, a.length, 0, b.length]];
  while (queue.length) {
    const [alo, ahi, blo, bhi] = queue.pop();
    const block = longestMatch(a, b, positions, alo, ahi, blo, bhi);
    if (!block.size) continue;
    blocks.push(block);
    if (alo < block.i && blo < block.j) queue.push([alo, block.i, blo, block.j]);
    if (block.i + block.size < ahi && block.j + block.size < bhi)
      queue.push([block.i + block.size, ahi, block.j + block.size, bhi]);
  }
  return blocks;
}

export function evenChars(line) {
  const indexes = [...line.text].flatMap((ch, ci) => (isSpace(ch) ? [] : [ci]));
  const step = (line.end - line.start) / indexes.length;
  return indexes.map((i, n) => ({ t: line.start + step * n, e: line.start + step * (n + 1), i }));
}

export function alignWords(lines, krc) {
  const ours = [];
  lines.forEach((line, li) =>
    [...line.text].forEach((ch, ci) => {
      if (!isSpace(ch)) ours.push({ li, ci, key: keyOf(ch) });
    })
  );
  const theirs = krc.flatMap((line) => {
    const text = [...line.text];
    return line.chars.map((c) => ({ c, key: keyOf(text[c.i]) }));
  });

  const timed = new Map();
  for (const block of matchingBlocks(
    ours.map((x) => x.key),
    theirs.map((x) => x.key)
  )) {
    for (let n = 0; n < block.size; n++) {
      const { li, ci } = ours[block.i + n];
      timed.set(`${li}:${ci}`, theirs[block.j + n].c);
    }
  }

  return lines.map((line, li) => {
    const indexes = [...line.text].flatMap((ch, ci) => (isSpace(ch) ? [] : [ci]));
    const hits = indexes.filter((ci) => timed.has(`${li}:${ci}`));
    if (!indexes.length || hits.length < WORD_LINE_RATIO * indexes.length) return { ...line, chars: null };
    const chars = [];
    for (const ci of indexes) {
      const c = timed.get(`${li}:${ci}`);
      if (c) chars.push({ t: c.t, e: c.e, i: ci });
      else {
        const prev = chars.length ? chars.at(-1).e : line.start;
        chars.push({ t: prev, e: prev, i: ci });
      }
    }
    for (let n = 1; n < chars.length; n++) {
      if (chars[n].t < chars[n - 1].t) chars[n].t = chars[n - 1].t;
      chars[n].e = Math.max(chars[n].e, chars[n].t);
    }
    return { ...line, chars };
  });
}
