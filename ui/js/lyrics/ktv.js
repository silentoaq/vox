export function lineIndexAt(lines, ms) {
  let index = -1;
  for (let i = 0; i < lines.length; i++) if (lines[i].start <= ms) index = i;
  return index;
}

export function singingAt(line, ms) {
  const last = line.chars.at(-1);
  if (ms >= last.e) return { done: true, char: null, progress: 0 };
  let char = null;
  for (const c of line.chars) if (c.t <= ms) char = c;
  if (!char) return { done: false, char: null, progress: 0 };
  const span = char.e - char.t;
  return { done: false, char, progress: span > 0 ? Math.min(1, (ms - char.t) / span) : 1 };
}
