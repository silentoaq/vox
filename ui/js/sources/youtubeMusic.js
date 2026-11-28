import { fetchJson } from '../host.js';
import { language } from '../i18n.js';
import { urlOf } from '../link.js';
import { TrackNotFound } from '../trackNotFound.js';
import { first, walk } from '../walk.js';

const WEB_CLIENT = { clientName: 'WEB_REMIX', clientVersion: '1.20250101.01.00' };
const TIMED_CLIENT = { clientName: '26', clientVersion: '7.01.05' };
const VIDEO_CLIENT = { clientName: 'WEB', clientVersion: '2.20250101.00.00' };
const MUSIC_HOST = 'music.youtube.com';
const VIDEO_HOST = 'www.youtube.com';
const SEARCH_PARAMS = {
  song: 'EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D',
  video: 'EgWKAQIQAWoKEAkQChAFEAMQBA%3D%3D'
};
const VIDEO_SEARCH_PARAMS = 'EgIQAQ%3D%3D';
const RADIO_PREFIX = 'RDAMVM';
const LIBRARY_PLAYLISTS = 'FEmusic_liked_playlists';
const CHARTS = 'FEmusic_charts';
const HOME = 'FEmusic_home';
const PLAYLIST_BROWSE_PREFIX = 'VL';
const SONG_VIDEO_TYPE = 'MUSIC_VIDEO_TYPE_ATV';
const UPLOAD_VIDEO_TYPE = 'MUSIC_VIDEO_TYPE_UGC';
const NOT_ORIGINAL = /伴奏|翻唱|instrumental|karaoke|\bcover\b/i;
const LYRICS_PAGE = 'MUSIC_PAGE_TYPE_TRACK_LYRICS';
const ALBUM_PAGE = 'MUSIC_PAGE_TYPE_ALBUM';
const PLAYING = 1;
const ENDED = 0;
const SEEK_SETTLE_SECONDS = 1;
const LINK_HOSTS = new Set(['youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com']);
const SHORT_LINK_HOST = 'youtu.be';
const LINK_PATH = /^\/(?:shorts|live|embed)\/([^/]+)/;
const VIDEO_ID = /^[A-Za-z0-9_-]{11}$/;

function innertube(path, body, client, host = MUSIC_HOST) {
  return fetchJson(`https://${host}/youtubei/v1/${path}?prettyPrint=false`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ...body, context: { client: client === TIMED_CLIENT ? client : { ...client, hl: language.id() } } }),
    anonymous: client === TIMED_CLIENT
  });
}

const runsText = (node) => (node?.runs ?? []).map((run) => run.text).join('');
const columnText = (column) => runsText(column.musicResponsiveListItemFlexColumnRenderer?.text);
const pageTypeOf = (run) =>
  run.navigationEndpoint?.browseEndpoint?.browseEndpointContextSupportedConfigs?.browseEndpointContextMusicConfig
    ?.pageType;

function seconds(clock) {
  return clock ? clock.split(':').reduce((total, piece) => total * 60 + Number(piece), 0) : 0;
}

function idOfLink(text) {
  const url = urlOf(text);
  if (!url) return null;
  const id =
    url.hostname === SHORT_LINK_HOST
      ? url.pathname.slice(1)
      : !LINK_HOSTS.has(url.hostname)
        ? null
        : url.pathname === '/watch'
          ? url.searchParams.get('v')
          : LINK_PATH.exec(url.pathname)?.[1];
  return id && VIDEO_ID.test(id) ? id : null;
}

async function search(query) {
  const lists = await Promise.all([searchKind(query, 'song'), searchKind(query, 'video'), searchVideos(query)]);
  const seen = new Set();
  return lists.flat().filter((track) => !seen.has(track.id) && seen.add(track.id));
}

async function searchVideos(query) {
  const res = await innertube('search', { query, params: VIDEO_SEARCH_PARAMS }, VIDEO_CLIENT, VIDEO_HOST);
  const tracks = [];
  for (const item of walk(res, 'videoRenderer')) {
    const clock = item.lengthText?.simpleText;
    if (!item.videoId || !clock) continue;
    tracks.push({
      source: 'ytm',
      kind: 'video',
      id: item.videoId,
      title: runsText(item.title),
      artist: runsText(item.ownerText),
      album: '',
      duration: seconds(clock),
      thumb: item.thumbnail?.thumbnails?.at(-1)?.url ?? ''
    });
  }
  return tracks;
}

async function searchKind(query, kind) {
  const res = await innertube('search', { query, params: SEARCH_PARAMS[kind] }, WEB_CLIENT);
  const tracks = [];
  for (const item of walk(res, 'musicResponsiveListItemRenderer')) {
    const id = item.playlistItemData?.videoId ?? first(item, 'videoId');
    const columns = item.flexColumns ?? [];
    if (!id || columns.length < 2) continue;
    const parts = columnText(columns[1]).split(' • ');
    const clock = [...parts].reverse().find((part) => /^\d+:\d{2}(:\d{2})?$/.test(part.trim()));
    const thumbs = first(item.thumbnail ?? {}, 'thumbnails') ?? [];
    tracks.push({
      source: 'ytm',
      kind,
      id,
      title: columnText(columns[0]),
      artist: parts[0] ?? '',
      album: kind === 'song' && parts.length > 2 ? parts[1] : '',
      duration: seconds(clock?.trim()),
      thumb: thumbs.at(-1)?.url ?? ''
    });
  }
  return tracks;
}

function panelTrack(panel) {
  const byline = panel.longBylineText?.runs ?? [];
  return {
    source: 'ytm',
    kind: first(panel.navigationEndpoint ?? {}, 'musicVideoType') === SONG_VIDEO_TYPE ? 'song' : 'video',
    id: panel.videoId,
    title: runsText(panel.title),
    artist: runsText(panel.longBylineText).split(' • ')[0],
    album: byline.find((run) => pageTypeOf(run) === ALBUM_PAGE)?.text ?? '',
    duration: seconds(runsText(panel.lengthText)),
    thumb: panel.thumbnail?.thumbnails?.at(-1)?.url ?? ''
  };
}

async function track(id) {
  const res = await innertube('next', { videoId: id, isAudioOnly: true }, WEB_CLIENT);
  const panel = [...walk(res, 'playlistPanelVideoRenderer')].find((item) => item.videoId === id);
  if (!panel) throw new TrackNotFound(`YouTube Music 查不到 ${id}`);
  return panelTrack(panel);
}

async function radio({ id }) {
  const res = await innertube('next', { videoId: id, playlistId: RADIO_PREFIX + id, isAudioOnly: true }, WEB_CLIENT);
  return (first(res, 'playlistPanelRenderer')?.contents ?? [])
    .map((item) => item.playlistPanelVideoRenderer ?? item.playlistPanelVideoWrapperRenderer?.primaryRenderer?.playlistPanelVideoRenderer)
    .filter((panel) => panel?.videoId && panel.videoId !== id)
    .filter((panel) => first(panel.navigationEndpoint ?? {}, 'musicVideoType') !== UPLOAD_VIDEO_TYPE)
    .map(panelTrack)
    .filter((track) => !NOT_ORIGINAL.test(track.title));
}

const continued = (token) => innertube('browse', { continuation: token }, WEB_CLIENT);

async function playlists() {
  let res = await innertube('browse', { browseId: LIBRARY_PLAYLISTS }, WEB_CLIENT);
  const lists = [];
  for (;;) {
    for (const item of walk(res, 'musicTwoRowItemRenderer')) {
      const browseId = item.navigationEndpoint?.browseEndpoint?.browseId;
      if (!browseId) continue;
      lists.push({
        id: browseId,
        name: runsText(item.title),
        detail: runsText(item.subtitle),
        thumb: first(item.thumbnailRenderer ?? {}, 'thumbnails')?.at(-1)?.url ?? ''
      });
    }
    const token = first(res, 'continuationCommand')?.token;
    if (!token) return lists;
    res = await continued(token);
  }
}

function playlistItemTrack(item) {
  const columns = item.flexColumns;
  return {
    source: 'ytm',
    kind: first(item.overlay ?? {}, 'musicVideoType') === SONG_VIDEO_TYPE ? 'song' : 'video',
    id: item.playlistItemData.videoId,
    title: columnText(columns[0]),
    artist: columnText(columns[1]),
    album: columns[2] ? columnText(columns[2]) : '',
    duration: seconds(runsText(item.fixedColumns?.[0]?.musicResponsiveListItemFixedColumnRenderer?.text)),
    thumb: first(item.thumbnail ?? {}, 'thumbnails')?.at(-1)?.url ?? ''
  };
}

async function playlistTracks(browseId) {
  const shelf = first(await innertube('browse', { browseId }, WEB_CLIENT), 'musicPlaylistShelfRenderer');
  if (!shelf) throw new Error(`YouTube Music 歌單 ${browseId} 沒有歌曲清單`);
  let items = shelf.contents ?? [];
  const tracks = [];
  for (;;) {
    for (const entry of items) {
      const item = entry.musicResponsiveListItemRenderer;
      if (item?.playlistItemData?.videoId) tracks.push(playlistItemTrack(item));
    }
    const token = items.at(-1)?.continuationItemRenderer?.continuationEndpoint?.continuationCommand?.token;
    if (!token) return tracks;
    items = first(await continued(token), 'continuationItems') ?? [];
  }
}

async function charts() {
  const res = await innertube('browse', { browseId: CHARTS }, WEB_CLIENT);
  const lists = [];
  for (const shelf of walk(res, 'musicCarouselShelfRenderer')) {
    for (const item of shelf.contents ?? []) {
      const card = item.musicTwoRowItemRenderer;
      const browseId = card?.navigationEndpoint?.browseEndpoint?.browseId;
      if (browseId?.startsWith(PLAYLIST_BROWSE_PREFIX)) lists.push({ id: browseId, name: runsText(card.title) });
    }
  }
  return lists;
}

async function recommended() {
  const res = await innertube('browse', { browseId: HOME }, WEB_CLIENT);
  const tracks = [];
  const seen = new Set();
  for (const item of walk(res, 'musicResponsiveListItemRenderer')) {
    const id = item.playlistItemData?.videoId;
    const columns = item.flexColumns ?? [];
    if (!id || columns.length < 2 || seen.has(id)) continue;
    seen.add(id);
    const parts = columnText(columns[1]).split(' • ');
    tracks.push({
      source: 'ytm',
      kind: first(item.overlay ?? {}, 'musicVideoType') === SONG_VIDEO_TYPE ? 'song' : 'video',
      id,
      title: columnText(columns[0]),
      artist: parts[0] ?? '',
      album: parts.length > 2 ? parts[1] : '',
      duration: 0,
      thumb: first(item.thumbnail ?? {}, 'thumbnails')?.at(-1)?.url ?? ''
    });
  }
  return tracks;
}

async function timedLines(song) {
  const next = await innertube('next', { videoId: song.id }, TIMED_CLIENT);
  let browseId = null;
  for (const endpoint of walk(next, 'browseEndpoint')) {
    if (endpoint.browseEndpointContextSupportedConfigs?.browseEndpointContextMusicConfig?.pageType === LYRICS_PAGE)
      browseId = endpoint.browseId;
  }
  if (!browseId) return [];
  const data = first(await innertube('browse', { browseId }, TIMED_CLIENT), 'timedLyricsData') ?? [];
  return data
    .filter((entry) => entry.cueRange)
    .map((entry) => {
      const text = (entry.lyricLine ?? '').trim();
      return {
        start: Number(entry.cueRange.startTimeMilliseconds),
        end: Number(entry.cueRange.endTimeMilliseconds),
        text: text === '♪' ? '' : text
      };
    });
}

function loadIframeApi() {
  return new Promise((resolve) => {
    window.onYouTubeIframeAPIReady = resolve;
    const tag = document.createElement('script');
    tag.src = 'https://www.youtube.com/iframe_api';
    document.head.append(tag);
  });
}

let yt = null;
let seekTarget = null;

const player = {
  async mount(element, { onChange, onEnded, onError }) {
    await loadIframeApi();
    const box = document.createElement('div');
    box.className = 'source';
    box.dataset.source = 'ytm';
    const slot = document.createElement('div');
    box.append(slot);
    element.append(box);
    await new Promise((resolve) => {
      yt = new window.YT.Player(slot, {
        width: '100%',
        height: '100%',
        playerVars: { playsinline: 1, controls: 0, disablekb: 1, rel: 0, iv_load_policy: 3, origin: location.origin },
        events: {
          onReady: resolve,
          onStateChange: (e) => {
            if (e.data === ENDED) onEnded();
            onChange();
          },
          onError: (e) => onError(e.data)
        }
      });
    });
  },
  load({ id }, autoplay) {
    seekTarget = null;
    if (autoplay) yt.loadVideoById(id);
    else yt.cueVideoById(id);
  },
  play: () => yt.playVideo(),
  pause: () => yt.pauseVideo(),
  stop: () => yt.stopVideo(),
  seek(sec) {
    seekTarget = sec;
    yt.seekTo(sec, true);
  },
  setVolume: (volume) => yt.setVolume(volume),
  setMuted: (muted) => (muted ? yt.mute() : yt.unMute()),
  currentTime() {
    const reported = yt.getCurrentTime() ?? 0;
    if (seekTarget !== null && Math.abs(reported - seekTarget) > SEEK_SETTLE_SECONDS) return seekTarget;
    seekTarget = null;
    return reported;
  },
  duration: () => yt.getDuration() ?? 0,
  isPlaying: () => yt.getPlayerState() === PLAYING,
  setEffects: (settings) => yt.getIframe().contentWindow.postMessage({ kind: 'app-effects', settings }, 'https://www.youtube.com'),
  setPitch: (semitones) => yt.getIframe().contentWindow.postMessage({ kind: 'app-pitch', semitones }, 'https://www.youtube.com')
};

async function whoAmI() {
  const header = first(await innertube('account/account_menu', {}, WEB_CLIENT), 'activeAccountHeaderRenderer');
  if (!header) return null;
  return { name: runsText(header.accountName), avatar: header.accountPhoto?.thumbnails?.at(-1)?.url ?? '' };
}

export const youtubeMusic = {
  id: 'ytm',
  name: 'YouTube',
  site: 'youtube',
  webUrl: (id) => `https://music.youtube.com/watch?v=${id}`,
  idOfLink,
  search,
  track,
  radio,
  playlists,
  playlistTracks,
  charts,
  recommended,
  timedLines,
  whoAmI,
  accountChanged() {},
  player
};
