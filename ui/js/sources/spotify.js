import { fetchJson, fetchText } from '../host.js';
import { language } from '../i18n.js';
import { urlOf } from '../link.js';

const WEB = 'https://open.spotify.com';
const BUILD = 'https://open.spotifycdn.com/cdn/build/web-player/';
const PATHFINDER = 'https://api-partner.spotify.com/pathfinder/v2/query';
const PLACEHOLDER_TRACK = '4uLU6hMCjMI75M1A2tKUQC';
const OPERATIONS = {
  searchTracks: 'xpui-routes-search',
  internalLinkRecommenderTrack: 'xpui-routes-track-v2',
  profileAttributes: null,
  libraryV3: null,
  fetchPlaylist: null,
  fetchLibraryTracks: null
};
const HASH_KEY = 'spotify.hashes';
const SEARCH_LIMIT = 30;
const RADIO_SIZE = 50;
const PAGE_SIZE = 100;
const LIKED_SONGS = 'spotify:collection:tracks';
const TOKEN_MARGIN_MS = 60_000;
const END_MARGIN_MS = 400;
const TRACK_URI = /^spotify:track:([A-Za-z0-9]{22})$/;
const TRACK_PATH = /^\/(?:intl-[a-z-]+\/)?track\/([A-Za-z0-9]{22})$/;

const uriOf = (id) => `spotify:track:${id}`;
const largest = (images = []) => [...images].sort((a, b) => (b.width ?? b.maxWidth) - (a.width ?? a.maxWidth))[0]?.url ?? '';
const playable = (data) => data?.__typename === 'Track' && data.playability?.playable;

function toTrack(data, uri) {
  return {
    source: 'spotify',
    kind: 'song',
    id: uri.split(':').at(-1),
    title: data.name,
    artist: data.artists.items.map((artist) => artist.profile.name).join(', '),
    album: data.albumOfTrack?.name ?? '',
    duration: Math.round((data.duration ?? data.trackDuration).totalMilliseconds / 1000),
    thumb: largest(data.albumOfTrack?.coverArt?.sources)
  };
}

async function embedState(id) {
  const html = await fetchText(`${WEB}/embed/track/${id}`, { headers: { 'Accept-Language': language.id() } });
  const json = html.match(/<script id="__NEXT_DATA__" type="application\/json">(.*?)<\/script>/s)?.[1];
  if (!json) throw new Error('Spotify 內嵌頁沒有 __NEXT_DATA__');
  return JSON.parse(json).props.pageProps.state;
}

let session = null;

async function currentSession() {
  if (session && session.expires - Date.now() > TOKEN_MARGIN_MS) return session;
  const settings = (await embedState(PLACEHOLDER_TRACK)).settings.session;
  session = {
    token: settings.accessToken,
    expires: settings.accessTokenExpirationTimestampMs,
    anonymous: settings.isAnonymous
  };
  return session;
}

function cachedHashes() {
  try {
    return JSON.parse(localStorage.getItem(HASH_KEY));
  } catch (e) {
    console.error('讀不到 Spotify 查詢雜湊快取', e);
    return null;
  }
}

function hashOf(code, operation) {
  const hash = code.match(new RegExp(`"${operation}","query","([0-9a-f]{64})"`))?.[1];
  if (!hash) throw new Error(`Spotify 程式裡找不到 ${operation}`);
  return hash;
}

let hashes = null;

function chunkCode(main, name) {
  const chunkId = main.match(new RegExp(`(\\d+):"${name}"`))?.[1];
  const chunkHashes = main.match(/\)\+"\."\+\(\{([^}]*)\}\)\[\w+\]\+"\.js"/)?.[1];
  const chunkHash = chunkHashes?.match(new RegExp(`(?:^|,)${chunkId}:"([0-9a-f]+)"`))?.[1];
  if (!chunkId || !chunkHash) throw new Error(`Spotify 主程式找不到分塊 ${name}`);
  return fetchText(`${BUILD}${name}.${chunkHash}.js`);
}

async function findHashes() {
  const html = await fetchText(`${WEB}/search`);
  const bundle = html.match(/web-player\/(web-player\.[0-9a-f]+\.js)/)?.[1];
  if (!bundle) throw new Error('Spotify 首頁找不到網頁播放器主程式');
  const cached = cachedHashes();
  if (cached?.bundle === bundle && Object.keys(OPERATIONS).every((operation) => cached.hashes[operation])) return cached.hashes;
  const main = await fetchText(BUILD + bundle);
  const codes = new Map([[null, main]]);
  for (const name of new Set(Object.values(OPERATIONS).filter(Boolean))) codes.set(name, await chunkCode(main, name));
  const found = Object.fromEntries(
    Object.entries(OPERATIONS).map(([operation, name]) => [operation, hashOf(codes.get(name), operation)])
  );
  try {
    localStorage.setItem(HASH_KEY, JSON.stringify({ bundle, hashes: found }));
  } catch (e) {
    console.error('Spotify 查詢雜湊存不起來', e);
  }
  return found;
}

async function pathfinder(operation, variables) {
  hashes ??= await findHashes();
  const res = await fetchJson(PATHFINDER, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${(await currentSession()).token}`,
      'Accept-Language': language.id(),
      'Content-Type': 'application/json;charset=UTF-8'
    },
    body: JSON.stringify({
      variables,
      operationName: operation,
      extensions: { persistedQuery: { version: 1, sha256Hash: hashes[operation] } }
    })
  });
  if (res.errors?.length) throw new Error(`Spotify ${operation} 失敗：${res.errors[0].message}`);
  return res.data;
}

async function search(query) {
  const data = await pathfinder('searchTracks', {
    searchTerm: query,
    offset: 0,
    limit: SEARCH_LIMIT,
    numberOfTopResults: SEARCH_LIMIT,
    includeAudiobooks: false,
    includePreReleases: false,
    includeAlbumPreReleases: false,
    includeAuthors: false,
    includeEpisodeContentRatingsV2: false
  });
  return data.searchV2.tracksV2.items
    .map(({ item }) => item.data)
    .filter(playable)
    .map((data) => toTrack(data, data.uri));
}

async function radio({ id }) {
  const data = await pathfinder('internalLinkRecommenderTrack', { uri: uriOf(id), limit: RADIO_SIZE });
  return data.seoRecommendedTrack.items
    .map((item) => item.data)
    .filter(playable)
    .map((data) => toTrack(data, data.uri));
}

async function playlists() {
  const lists = [];
  for (let offset = 0; ; offset += PAGE_SIZE) {
    const page = (
      await pathfinder('libraryV3', {
        filters: ['Playlists'],
        order: null,
        textFilter: null,
        features: ['LIKED_SONGS'],
        limit: PAGE_SIZE,
        offset,
        flatten: true,
        includeFoldersWhenFlattening: false
      })
    ).me.libraryV3;
    for (const { item } of page.items) {
      const data = item.data;
      if (data.__typename !== 'Playlist' && data.__typename !== 'PseudoPlaylist') continue;
      lists.push({
        id: data.uri,
        name: data.name,
        detail: data.ownerV2?.data?.name ?? '',
        thumb: largest(data.images?.items?.[0]?.sources ?? data.image?.sources)
      });
    }
    if (offset + PAGE_SIZE >= page.totalCount) return lists;
  }
}

async function likedSongs() {
  const tracks = [];
  for (let offset = 0; ; offset += PAGE_SIZE) {
    const page = (await pathfinder('fetchLibraryTracks', { offset, limit: PAGE_SIZE })).me.library.tracks;
    for (const { track } of page.items) if (playable(track.data)) tracks.push(toTrack(track.data, track._uri));
    if (offset + PAGE_SIZE >= page.totalCount) return tracks;
  }
}

async function playlistTracks(uri) {
  if (uri === LIKED_SONGS) return likedSongs();
  const tracks = [];
  for (let offset = 0; ; offset += PAGE_SIZE) {
    const page = (await pathfinder('fetchPlaylist', { uri, offset, limit: PAGE_SIZE, enableWatchFeedEntrypoint: false })).playlistV2
      .content;
    for (const { itemV2 } of page.items) if (playable(itemV2.data)) tracks.push(toTrack(itemV2.data, itemV2.data.uri));
    if (offset + PAGE_SIZE >= page.totalCount) return tracks;
  }
}

function idOfLink(text) {
  const uri = TRACK_URI.exec(text);
  if (uri) return uri[1];
  const url = urlOf(text);
  return url?.origin === WEB ? (TRACK_PATH.exec(url.pathname)?.[1] ?? null) : null;
}

async function track(id) {
  const { entity } = (await embedState(id)).data;
  return {
    source: 'spotify',
    kind: 'song',
    id,
    title: entity.name,
    artist: entity.artists.map((artist) => artist.name).join(', '),
    album: '',
    duration: Math.round(entity.duration / 1000),
    thumb: largest(entity.visualIdentity?.image)
  };
}

async function timedLines() {
  return [];
}

function loadIframeApi() {
  return new Promise((resolve) => {
    window.onSpotifyIframeApiReady = resolve;
    const tag = document.createElement('script');
    tag.src = `${WEB}/embed/iframe-api/v1`;
    document.head.append(tag);
  });
}

let controller = null;
let frame = null;
let art = null;
let state = { position: 0, duration: 0, paused: true, at: 0 };
let loadedId = null;
let playWhenReady = false;
let resumeAt = 0;
let endedReported = false;
let level = 1;
let muted = false;
let effects = null;
let semitones = 0;

function post(message) {
  frame?.contentWindow.postMessage(message, WEB);
}

function sendAudio() {
  post({ kind: 'app-volume', level });
  post({ kind: 'app-mute', muted });
  post({ kind: 'app-pitch', semitones });
  if (effects) post({ kind: 'app-effects', settings: effects });
}

function loadUri(id, autoplay, at) {
  state = { position: at * 1000, duration: 0, paused: true, at: performance.now() };
  endedReported = false;
  loadedId = id;
  playWhenReady = autoplay;
  resumeAt = at;
  controller.loadUri(uriOf(id));
}

const player = {
  async mount(element, { onChange, onEnded }) {
    const api = await loadIframeApi();
    const box = document.createElement('div');
    box.className = 'source';
    box.dataset.source = 'spotify';
    const embed = document.createElement('div');
    embed.className = 'embed';
    const slot = document.createElement('div');
    embed.append(slot);
    art = document.createElement('img');
    art.className = 'art';
    art.alt = '';
    box.append(embed, art);
    element.append(box);
    controller = await new Promise((resolve) =>
      api.createController(slot, { uri: uriOf(PLACEHOLDER_TRACK), width: 300, height: 80 }, resolve)
    );
    frame = embed.querySelector('iframe');
    controller.addListener('ready', () => {
      sendAudio();
      if (playWhenReady) controller.play();
      if (resumeAt > 0) controller.seek(resumeAt);
      playWhenReady = false;
      resumeAt = 0;
    });
    controller.addListener('playback_update', ({ data }) => {
      state = { position: data.position, duration: data.duration, paused: data.isPaused || data.isBuffering, at: performance.now() };
      if (!endedReported && data.duration > 0 && data.position >= data.duration - END_MARGIN_MS) {
        endedReported = true;
        onEnded();
      }
      onChange();
    });
  },
  load(track, autoplay) {
    art.src = track.thumb;
    loadUri(track.id, autoplay, 0);
  },
  play: () => controller.resume(),
  pause: () => controller.pause(),
  stop: () => controller.pause(),
  seek(sec) {
    endedReported = false;
    controller.seek(sec);
  },
  setVolume(volume) {
    level = volume / 100;
    post({ kind: 'app-volume', level });
  },
  setMuted(value) {
    muted = value;
    post({ kind: 'app-mute', muted });
  },
  currentTime() {
    const elapsed = state.paused ? 0 : performance.now() - state.at;
    return Math.min(state.position + elapsed, state.duration) / 1000;
  },
  duration: () => state.duration / 1000,
  isPlaying: () => !state.paused,
  setEffects(settings) {
    effects = settings;
    post({ kind: 'app-effects', settings });
  },
  setPitch(value) {
    semitones = value;
    post({ kind: 'app-pitch', semitones });
  }
};

async function whoAmI() {
  if ((await currentSession()).anonymous) return null;
  const profile = (await pathfinder('profileAttributes', {})).me?.profile;
  if (!profile) throw new Error('Spotify 帳號資料格式變了');
  return { name: profile.name, avatar: largest(profile.avatar?.sources) };
}

function accountChanged() {
  session = null;
  if (loadedId) loadUri(loadedId, !state.paused, player.currentTime());
}

export const spotify = {
  id: 'spotify',
  name: 'Spotify',
  site: 'spotify',
  webUrl: (id) => `${WEB}/track/${id}`,
  idOfLink,
  search,
  track,
  radio,
  playlists,
  playlistTracks,
  timedLines,
  whoAmI,
  accountChanged,
  player
};
