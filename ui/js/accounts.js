import { call, on } from './host.js';
import { sources } from './sources.js';

const CHECK_MS = 10 * 60_000;
const SIGNED_OUT = { state: 'none', name: '', avatar: '' };

const states = new Map();
const listeners = [];

async function check(source) {
  const { signedIn } = await call('account.state', { site: source.site });
  let next = SIGNED_OUT;
  if (signedIn) {
    let who;
    try {
      who = await source.whoAmI();
    } catch (e) {
      console.error(`${source.name} 查帳號失敗`, e);
    }
    if (who === undefined) next = { ...(states.get(source.id) ?? SIGNED_OUT), state: 'active' };
    else if (who === null) next = { state: 'expired', name: '', avatar: '' };
    else next = { state: 'active', ...who };
  }
  states.set(source.id, next);
  for (const listener of listeners) listener(source.id);
}

const checkAll = () => Promise.all(sources.map(check));

on('account', async ({ site }) => {
  const source = sources.find((candidate) => candidate.site === site);
  if (!source) throw new Error(`收到不認得的帳號種類：${site}`);
  source.accountChanged();
  await check(source);
});

export const accounts = {
  start() {
    setInterval(checkAll, CHECK_MS);
    return checkAll();
  },
  state: (sourceId) => states.get(sourceId) ?? null,
  signedIn: (sourceId) => states.get(sourceId)?.state === 'active',
  onChange: (listener) => listeners.push(listener),
  login: (source) => call('account.login', { site: source.site }),
  async logout(source) {
    await call('account.logout', { site: source.site });
    source.accountChanged();
    await check(source);
  }
};
