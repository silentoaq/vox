import { escapeHtml } from './html.js';
import { lineIndexAt, singingAt } from './lyrics/ktv.js';

const BLANK = { key: 'empty' };

export function createStageBand(element) {
  let layout = { lineCount: 2, direction: 'horizontal', align: 'stagger', zoom: false };
  let slots = [];

  function build() {
    element.replaceChildren();
    slots = Array.from({ length: layout.lineCount }, (_, i) => {
      const el = document.createElement('div');
      el.className = 'slot';
      if (layout.align === 'stagger' && layout.lineCount === 2) el.classList.add(i === 0 ? 'start' : 'end');
      element.append(el);
      return { el, key: null, line: null, fill: null, dim: false, current: false, starts: [], sizes: [], total: 0, shown: -1 };
    });
    element.style.setProperty('--lines', String(layout.lineCount));
    element.dataset.align = layout.align;
    element.classList.toggle('vertical', layout.direction === 'vertical');
    element.classList.toggle('zoom', layout.zoom);
  }

  function placeIdle(card, notice) {
    const contents = slots.map(() => BLANK);
    if (slots.length === 1) {
      contents[0] = notice ?? card ?? BLANK;
      return contents;
    }
    const center = slots.length === 2 ? 0 : Math.floor(slots.length / 2);
    contents[center] = card ?? notice ?? BLANK;
    if (card && notice) contents[center + 1] = notice;
    return contents;
  }

  function lineContent(lines, index, current, card) {
    if (index === -1) return card;
    if (index < -1 || index >= lines.length) return BLANK;
    return { key: `line:${index}`, line: lines[index], current: index === current };
  }

  function placeLines(lines, ms, card) {
    const current = lineIndexAt(lines, ms);
    if (slots.length === 2) {
      if (current < 0) return [card, lineContent(lines, 0, current, card)];
      const contents = [BLANK, BLANK];
      contents[current % 2] = lineContent(lines, current, current, card);
      contents[(current + 1) % 2] = lineContent(lines, current + 1, current, card);
      return contents;
    }
    const center = Math.floor(slots.length / 2);
    return slots.map((_, i) => lineContent(lines, current - center + i, current, card));
  }

  function paint(slot, content) {
    const dim = Boolean(content.line) && !content.current && slots.length > 2;
    const current = Boolean(content.current);
    const restyled = dim !== slot.dim || current !== slot.current;
    slot.dim = dim;
    slot.current = current;
    slot.el.classList.toggle('dim', dim);
    slot.el.classList.toggle('current', current);
    slot.el.classList.toggle('line', Boolean(content.line));
    if (slot.key === content.key) {
      if (restyled) measure(slot);
      return;
    }
    slot.key = content.key;
    slot.line = content.line ?? null;
    slot.fill = null;
    slot.el.replaceChildren();
    if (content.notice) {
      const el = document.createElement('div');
      el.className = content.error ? 'notice error' : 'notice';
      el.textContent = content.notice;
      slot.el.append(el);
      return;
    }
    if (content.card) {
      const el = document.createElement('div');
      el.className = 'card';
      el.innerHTML = `<div class="card-title">${escapeHtml(content.title)}</div><div class="card-artist">${escapeHtml(content.artist)}</div>`;
      slot.el.append(el);
      return;
    }
    if (!content.line) return;
    const lyric = document.createElement('div');
    lyric.className = 'lyric';
    const base = document.createElement('div');
    base.className = 'base';
    for (const ch of content.line.text) {
      const span = document.createElement('span');
      span.textContent = ch;
      base.append(span);
    }
    const fill = document.createElement('div');
    fill.className = 'fill';
    fill.textContent = content.line.text;
    lyric.append(base, fill);
    slot.el.append(lyric);
    slot.fill = fill;
    measure(slot);
  }

  function measure(slot) {
    const lyric = slot.el.firstElementChild;
    if (!lyric?.classList.contains('lyric')) return;
    const vertical = layout.direction === 'vertical';
    lyric.style.removeProperty('--size');
    const room = vertical ? slot.el.clientHeight : slot.el.clientWidth;
    const extent = vertical ? lyric.offsetHeight : lyric.offsetWidth;
    if (extent > room) lyric.style.setProperty('--size', `${(parseFloat(getComputedStyle(lyric).fontSize) * room) / extent}px`);
    const spans = [...lyric.firstElementChild.children];
    slot.starts = spans.map((span) => (vertical ? span.offsetTop : span.offsetLeft));
    slot.sizes = spans.map((span) => (vertical ? span.offsetHeight : span.offsetWidth));
    slot.total = vertical ? lyric.offsetHeight : lyric.offsetWidth;
    slot.shown = -1;
  }

  function wipe(slot, ms) {
    if (!slot.fill) return false;
    const state = singingAt(slot.line, ms);
    const reach = state.done ? slot.total : state.char ? slot.starts[state.char.i] + slot.sizes[state.char.i] * state.progress : 0;
    const rounded = Math.round(reach * 2) / 2;
    if (rounded !== slot.shown) {
      slot.shown = rounded;
      const rest = Math.max(0, slot.total - rounded);
      slot.fill.style.clipPath = layout.direction === 'vertical' ? `inset(0 0 ${rest}px 0)` : `inset(0 ${rest}px 0 0)`;
    }
    return Boolean(state.char) && state.progress < 1;
  }

  build();

  return {
    setLayout(next) {
      layout = { ...layout, ...next };
      build();
    },
    render({ ms, card, notice, lines }) {
      const contents = lines ? placeLines(lines, ms, card) : placeIdle(card, notice);
      let wiping = false;
      slots.forEach((slot, i) => {
        paint(slot, contents[i]);
        if (wipe(slot, ms)) wiping = true;
      });
      return wiping;
    },
    reset: build,
    measureAll() {
      for (const slot of slots) measure(slot);
    },
    nextChangeAt(ms) {
      const candidates = [Infinity];
      for (const slot of slots) {
        const next = slot.line?.chars?.find((c) => c.t > ms);
        if (next) candidates.push(next.t);
        if (slot.line?.chars && slot.line.chars.at(-1).e > ms) candidates.push(slot.line.chars.at(-1).e);
      }
      return Math.min(...candidates);
    }
  };
}
