const ARTIST_MARK = /\s*[/／]\s*/g;
const ARTIST_SEPARATOR = /[、,，&＆/／]|\s+(?:x|feat\.?|ft\.?)\s+/i;
const BY_ARTIST = 0;
const BY_TITLE_NAME = 1;
const OTHERS = 2;
const TIERS = 3;

const normalize = (text) =>
  text
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[đĐ]/g, 'd')
    .replace(/[︎️‍]/g, '')
    .replace(/\s/gu, '')
    .replace(/[（）()【】[\]「」『』《》"'’·・．.,，!！?？~～\-—_|｜]/g, '')
    .toLowerCase();

export function parseSongQuery(text) {
  const trimmed = text.trim();
  const last = [...trimmed.matchAll(ARTIST_MARK)].at(-1);
  if (last) {
    const title = trimmed.slice(0, last.index).trim();
    const artist = trimmed.slice(last.index + last[0].length).trim();
    if (title && artist) return { title, artist };
  }
  return { title: trimmed, artist: null };
}

export const songSearchText = (query) => (query.artist ? `${query.title} ${query.artist}` : query.title);

function artistMatches(wanted, artist) {
  return artist
    .split(ARTIST_SEPARATOR)
    .map(normalize)
    .some((name) => name.length > 0 && (name.includes(wanted) || wanted.includes(name)));
}

export function rankForQuery(query, tracks) {
  const wanted = query.artist ? normalize(query.artist) : '';
  if (!wanted) return tracks;
  const title = normalize(query.title);
  const rank = (track) => {
    const name = normalize(track.title);
    const tier = artistMatches(wanted, track.artist) ? BY_ARTIST : name.includes(wanted) ? BY_TITLE_NAME : OTHERS;
    return tier * TIERS + (name === title ? 0 : name.includes(title) ? 1 : 2);
  };
  return tracks
    .map((track, order) => ({ track, order, rank: rank(track) }))
    .sort((a, b) => a.rank - b.rank || a.order - b.order)
    .map((entry) => entry.track);
}
