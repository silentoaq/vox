const webview = window.chrome.webview;
const pending = new Map();
const listeners = new Map();
let nextId = 1;

webview.addEventListener('message', ({ data }) => {
  if ('event' in data) {
    for (const listener of listeners.get(data.event) ?? []) listener(data.params);
    return;
  }
  const call = pending.get(data.id);
  if (!call) throw new Error(`收到不認得的回覆 ${data.id}`);
  pending.delete(data.id);
  if ('error' in data) call.reject(new Error(data.error));
  else call.resolve(data.result);
});

export function call(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    webview.postMessage({ id, method, params });
  });
}

export function on(event, listener) {
  if (!listeners.has(event)) listeners.set(event, []);
  listeners.get(event).push(listener);
}

export async function fetchText(url, { method = 'GET', headers = {}, body = '', anonymous = false } = {}) {
  const res = await call('http', { url, method, headers, body, anonymous });
  if (res.status < 200 || res.status >= 300) throw new Error(`${new URL(url).host} 回 ${res.status}`);
  return res.body;
}

export const fetchJson = async (url, options) => JSON.parse(await fetchText(url, options));
