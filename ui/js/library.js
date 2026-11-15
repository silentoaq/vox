import { call } from './host.js';
import { t } from './i18n.js';

const EMPTY = { favorites: [], playlists: [], history: [] };

let data = { ...EMPTY };

const keyOf = (track) => `${track.source}:${track.id}`;
const stored = ({ source, kind, id, title, artist, album, duration, thumb }) => ({
  source,
  kind,
  id,
  title,
  artist,
  album,
  duration,
  thumb
});
const save = () => call('library.save', { text: JSON.stringify(data) });

function playlist(id) {
  const found = data.playlists.find((item) => item.id === id);
  if (!found) throw new Error(`沒有這個歌單：${id}`);
  return found;
}

export async function loadLibrary() {
  const { text } = await call('library.load');
  if (text) data = { ...EMPTY, ...JSON.parse(text) };
}

export const library = {
  favorites: () => data.favorites,
  playlists: () => data.playlists,
  playlist,
  isFavorite: (track) => data.favorites.some((item) => keyOf(item) === keyOf(track)),
  toggleFavorite(track) {
    const favorite = library.isFavorite(track);
    data.favorites = favorite
      ? data.favorites.filter((item) => keyOf(item) !== keyOf(track))
      : [stored(track), ...data.favorites];
    save();
    return !favorite;
  },
  createPlaylist() {
    const created = { id: crypto.randomUUID(), name: t('playlist.defaultName', { n: data.playlists.length + 1 }), tracks: [] };
    data.playlists.push(created);
    save();
    return created;
  },
  renamePlaylist(id, name) {
    playlist(id).name = name;
    save();
  },
  deletePlaylist(id) {
    data.playlists = data.playlists.filter((item) => item.id !== id);
    save();
  },
  addToPlaylist(id, track) {
    const target = playlist(id);
    if (target.tracks.some((item) => keyOf(item) === keyOf(track))) return false;
    target.tracks.push(stored(track));
    save();
    return true;
  },
  removeFromPlaylist(id, index) {
    playlist(id).tracks.splice(index, 1);
    save();
  },
  history: () => data.history,
  addHistory(track) {
    data.history = [{ ...stored(track), playedAt: Date.now() }, ...data.history.filter((item) => keyOf(item) !== keyOf(track))];
    save();
  },
  removeHistory(index) {
    data.history.splice(index, 1);
    save();
  },
  clearHistory() {
    data.history = [];
    save();
  }
};
