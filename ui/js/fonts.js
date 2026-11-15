import catalog from './fonts.json' with { type: 'json' };
import { language } from './i18n.js';

const GOOGLE_CSS = 'https://fonts.googleapis.com/css2';
const LYRIC_WEIGHT = '700';
const REGULAR = '400';

export const SYSTEM_FONT = 'system';
export const FONT_STYLES = ['system', 'sans', 'round', 'serif', 'kai', 'hand', 'brush', 'display', 'pixel', 'other'];
export const FONTS = catalog;

export function fontOf(id) {
  return FONTS.find((font) => font.id === id) ?? FONTS[0];
}

const pickWeight = (weights, wanted) =>
  weights.includes(wanted) ? wanted : weights.includes(LYRIC_WEIGHT) ? LYRIC_WEIGHT : weights.includes(REGULAR) ? REGULAR : weights[0];

const systemFamily = () => (language.script() === 'hans' ? '"Microsoft YaHei"' : '"Microsoft JhengHei"');

export const cssFamilyOf = (font) =>
  font.id === SYSTEM_FONT ? `${systemFamily()}, sans-serif` : `${font.family}, ${systemFamily()}, sans-serif`;

export const fontsForLanguage = () =>
  language.script() === 'hans' ? FONTS.filter((font) => font.id === SYSTEM_FONT || font.simplified) : FONTS;

export function weightsOf(font) {
  return [...new Set([...font.weights, LYRIC_WEIGHT])].sort((a, b) => Number(a) - Number(b));
}

export function webCssOf(font, wanted) {
  if (font.gfont) {
    const weight = pickWeight(font.weights, wanted);
    return `${GOOGLE_CSS}?family=${font.gfont}${weight === REGULAR ? '' : `:wght@${weight}`}&display=swap`;
  }
  if (font.webFiles) {
    const weight = pickWeight(Object.keys(font.webFiles), wanted);
    const face = `@font-face{font-family:${font.family};src:url("${font.webFiles[weight]}");font-weight:${weight};font-display:swap}`;
    return `data:text/css,${encodeURIComponent(face)}`;
  }
  return font.css ?? null;
}

export function nativeFontOf(font, wanted) {
  if (font.id === SYSTEM_FONT) return {};
  if (font.gfont) return { id: font.id, weight: pickWeight(font.weights, wanted), gfont: font.gfont };
  if (!font.files) throw new Error(`字體 ${font.id} 沒有完整字檔`);
  const weight = pickWeight(Object.keys(font.files), wanted);
  return { id: font.id, weight, url: font.files[weight], ...(font.zip ? { zipPath: font.zip[weight] } : {}) };
}
