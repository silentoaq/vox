import { hexToHsv, hsvToHex, isHex } from './color.js';
import { FONT_STYLES, SYSTEM_FONT, cssFamilyOf, fontOf, fontsForLanguage, nativeFontOf, webCssOf, weightsOf } from './fonts.js';
import { call } from './host.js';
import { escapeHtml } from './html.js';
import { t } from './i18n.js';
import { savedJson, savedText, saveText } from './storage.js';

const FONT_KEY = 'lyricFont';
const STAGE_KEY = 'stageLyrics';
const SIZE_STEP = 4;
const DEFAULT_LOOK = {
  sung: '#3AD38A',
  unsung: '#FFFFFF',
  outline: '#000000',
  outlineWidth: 0.12,
  weight: 700,
  backgroundOn: false,
  background: '#101012'
};
const DEFAULT_STAGE = { size: 40, lineCount: 2, direction: 'horizontal', align: 'stagger', zoom: false };
const COLOR_PRESETS = [
  '#3AD38A', '#7BED9F', '#00D2D3', '#3BA7FF', '#5F7CFF', '#A66CFF', '#E056FD', '#FF6FA8', '#FF5A5F',
  '#FF9F43', '#FFD24A', '#FFF3B0', '#F8C291', '#FFFFFF', '#D9D9DE', '#9A9AA6', '#5C5C66', '#000000'
];
const OUTLINE_WIDTHS = [
  ['none', 0],
  ['thin', 0.06],
  ['medium', 0.12],
  ['thick', 0.18]
];
const LINE_COUNTS = [1, 2, 3, 5];
const DIRECTIONS = ['horizontal', 'vertical'];
const ALIGNS = ['stagger', 'start', 'center', 'end'];

const $ = (id) => document.getElementById(id);
const segs = (options, chosen, attr) =>
  options
    .map(([value, label]) => `<button class="seg${value === chosen ? ' on' : ''}" data-${attr}="${value}">${label}</button>`)
    .join('');
const named = (group, ids, chosen, attr) =>
  segs(
    ids.map((id) => [id, t(`${group}.${id}`)]),
    chosen,
    attr
  );
const faceHtml = (font) =>
  font.id === SYSTEM_FONT
    ? `<span class="font-system" style="font-family: ${escapeHtml(cssFamilyOf(font))}">${t('font.system')}</span>`
    : `<img src="fonts/${font.id}.webp" alt="${escapeHtml(font.label)}">`;
const switchSegs = (chosen, attr) =>
  segs(
    [false, true].map((on) => [on, t(on ? 'switch.on' : 'switch.off')]),
    chosen,
    attr
  );

export function createLyricStyle({ band, stage, toast, refresh }) {
  let look = { ...DEFAULT_LOOK };
  let fontId = fontOf(savedText(FONT_KEY)).id;
  let layout = savedJson(STAGE_KEY, DEFAULT_STAGE);
  let target = 'sung';
  let hsv = hexToHsv(look.sung);
  let fontsOpen = false;

  const panelOpen = () => !$('stylePanel').hidden;

  function activeFont() {
    const font = fontOf(fontId);
    return fontsForLanguage().includes(font) ? font : fontOf(SYSTEM_FONT);
  }

  function applyLookCss() {
    const root = document.documentElement.style;
    root.setProperty('--sung', look.sung);
    root.setProperty('--unsung', look.unsung);
    root.setProperty('--outline-color', look.outline);
    root.setProperty('--outline-width', String(look.outlineWidth));
    root.setProperty('--lyric-weight', String(look.weight));
    root.setProperty('--lyric-bg-color', look.background);
    root.setProperty('--lyric-bg', look.backgroundOn ? `color-mix(in srgb, ${look.background} 80%, transparent)` : 'transparent');
    markChanged();
  }

  function markChanged() {
    const changed =
      fontId !== SYSTEM_FONT ||
      Object.keys(DEFAULT_LOOK).some((key) => look[key] !== DEFAULT_LOOK[key]) ||
      Object.keys(DEFAULT_STAGE).some((key) => layout[key] !== DEFAULT_STAGE[key]);
    $('styleToggle').classList.toggle('on', changed);
  }

  const sendLook = (save) => call('desktopLyrics.look', { ...look, save });

  function applyStageFont() {
    const font = activeFont();
    $('lyricFontCss')?.remove();
    const css = webCssOf(font, String(look.weight));
    if (css) {
      const link = document.createElement('link');
      link.rel = 'stylesheet';
      link.href = css;
      link.id = 'lyricFontCss';
      document.head.append(link);
    }
    document.documentElement.style.setProperty('--lyric-font', cssFamilyOf(font));
  }

  async function applyDesktopFont() {
    const font = activeFont();
    try {
      await call('desktopLyrics.font', nativeFontOf(font, String(look.weight)));
    } catch (e) {
      console.error('桌面歌詞字體載入失敗', font.label, e);
      toast(t('style.fontFailed', { name: font.label }));
    }
  }

  function applyLayout() {
    band.setLayout({ lineCount: layout.lineCount, direction: layout.direction, align: layout.align, zoom: layout.zoom });
    document.documentElement.style.setProperty('--lyric-size', `${layout.size}px`);
    stage.classList.toggle('vertical', layout.direction === 'vertical');
    markChanged();
    refresh();
  }

  function setLayout(patch) {
    layout = { ...layout, ...patch };
    saveText(STAGE_KEY, JSON.stringify(layout));
    applyLayout();
    render();
  }

  function setLook(patch, save) {
    const weightChanged = patch.weight !== undefined && patch.weight !== look.weight;
    look = { ...look, ...patch };
    applyLookCss();
    sendLook(save);
    if (weightChanged) {
      applyStageFont();
      applyDesktopFont();
    }
    render();
  }

  function setColor(hex, save) {
    setLook({ [target]: hex.toUpperCase() }, save);
  }

  function chooseFont(id) {
    fontId = id;
    saveText(FONT_KEY, id);
    const weights = weightsOf(fontOf(id)).map(Number);
    if (!weights.includes(look.weight)) setLook({ weight: weights.includes(700) ? 700 : weights[0] }, true);
    applyStageFont();
    applyDesktopFont();
    markChanged();
    render();
  }

  function renderFonts() {
    $('fontList').hidden = !fontsOpen;
    if (!fontsOpen) return;
    const offered = fontsForLanguage();
    $('fontList').innerHTML = FONT_STYLES.map((style) => {
      const fonts = offered.filter((font) => font.style === style);
      if (!fonts.length) return '';
      const items = fonts
        .map(
          (font) =>
`<button class="font-item${font.id === activeFont().id ? ' on' : ''}" data-font="${font.id}">${faceHtml(font)}</button>`
        )
        .join('');
      return `<div class="font-group">${t(`fontStyle.${style}`)}</div><div class="font-grid">${items}</div>`;
    }).join('');
  }

  function render() {
    if (!panelOpen()) return;
    const font = activeFont();
    $('fontCurrent').innerHTML = `${faceHtml(font)}<span>${t(fontsOpen ? 'style.fontCollapse' : 'style.fontChange')}</span>`;
    renderFonts();
    $('weightRow').innerHTML = segs(
      weightsOf(font).map((w) => [Number(w), t(`weight.${w}`)]),
      look.weight,
      'weight'
    );
    document.querySelectorAll('#colorTargets .seg').forEach((el) => el.classList.toggle('on', el.dataset.target === target));
    $('sv').style.setProperty('--hue', String(Math.round(hsv.h)));
    $('svKnob').style.left = `${hsv.s * 100}%`;
    $('svKnob').style.top = `${(1 - hsv.v) * 100}%`;
    $('hue').value = String(Math.round(hsv.h));
    if (document.activeElement !== $('hex')) $('hex').value = look[target];
    $('swatches').innerHTML = COLOR_PRESETS.map(
      (preset) =>
        `<button class="swatch${preset === look[target] ? ' on' : ''}" data-color="${preset}" style="background:${preset}" aria-label="${preset}"></button>`
    ).join('');
    $('outlineWidths').innerHTML = segs(
      OUTLINE_WIDTHS.map(([name, value]) => [value, t(`outlineWidth.${name}`)]),
      look.outlineWidth,
      'width'
    );
    $('backgroundRow').innerHTML = switchSegs(look.backgroundOn, 'background');
    $('sizeValue').textContent = `${layout.size}px`;
    $('lineCountRow').innerHTML = segs(
      LINE_COUNTS.map((n) => [n, t('style.lineCount', { n })]),
      layout.lineCount,
      'lines'
    );
    $('directionRow').innerHTML = named('direction', DIRECTIONS, layout.direction, 'direction');
    $('alignRow').innerHTML = named('align', ALIGNS, layout.align, 'align');
    $('zoomRow').innerHTML = switchSegs(layout.zoom, 'zoom');
  }

  function pickTarget(name) {
    target = name;
    hsv = hexToHsv(look[name], hsv.h);
    render();
  }

  function dragColor(e, save) {
    const box = $('sv').getBoundingClientRect();
    hsv = {
      h: hsv.h,
      s: Math.min(1, Math.max(0, (e.clientX - box.left) / box.width)),
      v: 1 - Math.min(1, Math.max(0, (e.clientY - box.top) / box.height))
    };
    setColor(hsvToHex(hsv), save);
  }

  $('fontCurrent').addEventListener('click', () => {
    fontsOpen = !fontsOpen;
    render();
  });
  $('fontList').addEventListener('click', (e) => {
    const button = e.target.closest('[data-font]');
    if (button) chooseFont(button.dataset.font);
  });
  $('weightRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-weight]');
    if (button) setLook({ weight: Number(button.dataset.weight) }, true);
  });
  $('colorTargets').addEventListener('click', (e) => {
    const button = e.target.closest('[data-target]');
    if (button) pickTarget(button.dataset.target);
  });
  $('sv').addEventListener('pointerdown', (e) => {
    $('sv').setPointerCapture(e.pointerId);
    dragColor(e, false);
  });
  $('sv').addEventListener('pointermove', (e) => {
    if ($('sv').hasPointerCapture(e.pointerId)) dragColor(e, false);
  });
  $('sv').addEventListener('pointerup', (e) => dragColor(e, true));
  $('hue').addEventListener('input', () => {
    hsv = { ...hsv, h: Number($('hue').value) };
    setColor(hsvToHex(hsv), false);
  });
  $('hue').addEventListener('change', () => setColor(hsvToHex(hsv), true));
  $('hex').addEventListener('input', () => {
    const text = $('hex').value.trim();
    if (!isHex(text)) return;
    hsv = hexToHsv(text, hsv.h);
    setColor(text, true);
  });
  $('hex').addEventListener('blur', render);
  $('hex').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' || e.key === 'Escape') $('hex').blur();
  });
  $('swatches').addEventListener('click', (e) => {
    const swatch = e.target.closest('[data-color]');
    if (!swatch) return;
    hsv = hexToHsv(swatch.dataset.color, hsv.h);
    setColor(swatch.dataset.color, true);
  });
  $('outlineWidths').addEventListener('click', (e) => {
    const button = e.target.closest('[data-width]');
    if (button) setLook({ outlineWidth: Number(button.dataset.width) }, true);
  });
  $('backgroundRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-background]');
    if (button) setLook({ backgroundOn: button.dataset.background === 'true' }, true);
  });
  $('sizeDown').addEventListener('click', () => setLayout({ size: Math.max(SIZE_STEP, layout.size - SIZE_STEP) }));
  $('sizeUp').addEventListener('click', () => setLayout({ size: layout.size + SIZE_STEP }));
  $('lineCountRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-lines]');
    if (button) setLayout({ lineCount: Number(button.dataset.lines) });
  });
  $('directionRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-direction]');
    if (button) setLayout({ direction: button.dataset.direction });
  });
  $('alignRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-align]');
    if (button) setLayout({ align: button.dataset.align });
  });
  $('zoomRow').addEventListener('click', (e) => {
    const button = e.target.closest('[data-zoom]');
    if (button) setLayout({ zoom: button.dataset.zoom === 'true' });
  });

  return {
    start(state) {
      this.sync(state);
      applyStageFont();
      if (activeFont().id !== SYSTEM_FONT) applyDesktopFont();
      applyLayout();
    },
    sync(state) {
      look = {
        sung: state.sung,
        unsung: state.unsung,
        outline: state.outline,
        outlineWidth: Math.round(state.outlineWidth * 100) / 100,
        weight: state.weight,
        backgroundOn: state.backgroundOn,
        background: state.background
      };
      hsv = hexToHsv(look[target], hsv.h);
      applyLookCss();
      render();
    },
    opened() {
      fontsOpen = false;
      pickTarget(target);
    },
    languageChanged() {
      applyStageFont();
      applyDesktopFont();
      render();
    }
  };
}
