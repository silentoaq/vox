export function urlOf(text) {
  const candidate = /^https?:\/\//i.test(text) ? text : `https://${text}`;
  return URL.canParse(candidate) ? new URL(candidate) : null;
}
