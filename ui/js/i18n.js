import en from '../locales/en.json' with { type: 'json' };
import zhCN from '../locales/zh-CN.json' with { type: 'json' };
import zhTW from '../locales/zh-TW.json' with { type: 'json' };
import { call } from './host.js';

export const SYSTEM_LANGUAGE = 'system';
export const LANGUAGES = [
  { id: 'zh-TW', name: '繁體中文', tag: 'zh-Hant', script: 'hant', messages: zhTW },
  { id: 'zh-CN', name: '简体中文', tag: 'zh-Hans', script: 'hans', messages: zhCN },
  { id: 'en', name: 'English', tag: 'en', script: null, messages: en }
];

const listeners = [];
let setting;
let active;
let plural;

function apply(state) {
  const found = LANGUAGES.find((candidate) => candidate.id === state.active);
  if (!found) throw new Error(`沒有這個語言：${state.active}`);
  setting = state.setting;
  active = found;
  plural = new Intl.PluralRules(found.id);
  document.documentElement.lang = found.tag;
  for (const el of document.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
  for (const el of document.querySelectorAll('[data-i18n-placeholder]')) el.placeholder = t(el.dataset.i18nPlaceholder);
  for (const el of document.querySelectorAll('[data-i18n-label]')) el.setAttribute('aria-label', t(el.dataset.i18nLabel));
}

export function t(key, params = {}) {
  let message = active.messages[key];
  if (message === undefined) throw new Error(`沒有這句文字：${key}`);
  if (typeof message !== 'string') message = message[plural.select(params.n)] ?? message.other;
  return message.replace(/\{(\w+)\}/g, (_, name) => {
    if (!(name in params)) throw new Error(`文字 ${key} 少了 ${name}`);
    return String(params[name]);
  });
}

export const language = {
  id: () => active.id,
  script: () => active.script,
  setting: () => setting,
  onChange: (listener) => listeners.push(listener),
  async choose(next) {
    apply(await call('language.set', { setting: next }));
    for (const listener of listeners) listener();
  }
};

apply(await call('language.get'));
