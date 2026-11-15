export function savedText(key) {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export function saveText(key, value) {
  try {
    localStorage.setItem(key, value);
  } catch (e) {
    console.error(`設定 ${key} 存不起來`, e);
  }
}

export function savedFlag(key, fallback) {
  const value = savedText(key);
  return value === null ? fallback : value === 'on';
}

export const saveFlag = (key, value) => saveText(key, value ? 'on' : 'off');

export function savedJson(key, fallback) {
  try {
    return { ...fallback, ...JSON.parse(savedText(key)) };
  } catch {
    return { ...fallback };
  }
}
