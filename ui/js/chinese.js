import { Converter } from '../vendor/opencc/full.js';

export const toTraditional = Converter({ from: 'cn', to: 'tw' });
export const toSimplified = Converter({ from: 'tw', to: 'cn' });
