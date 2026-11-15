export function* walk(node, key) {
  if (Array.isArray(node)) {
    for (const item of node) yield* walk(item, key);
  } else if (node && typeof node === 'object') {
    for (const [k, v] of Object.entries(node)) {
      if (k === key) yield v;
      yield* walk(v, key);
    }
  }
}

export function first(node, key) {
  for (const value of walk(node, key)) return value;
  return undefined;
}
