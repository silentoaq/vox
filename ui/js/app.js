import { accounts } from './accounts.js';
import { EQ_PRESETS, REVERB_PRESETS, effectSettings } from './effects.js';
import { PITCH_LATENCY_FRAMES } from './effectsGraph.js';
import { call, on } from './host.js';
import { escapeHtml } from './html.js';
import { LANGUAGES, SYSTEM_LANGUAGE, language, t } from './i18n.js';
import { createKaraoke } from './karaoke.js';
import { library, loadLibrary } from './library.js';
import { createNowPlaying } from './nowPlaying.js';
import { RequestError } from './requestError.js';
import { createLyricStyle } from './lyricStyle.js';
import { inScript, lyricsFor } from './lyrics/lyrics.js';
import { createPlayback } from './playback.js';
import { parseSongQuery, rankForQuery, songSearchText } from './songQuery.js';
import { sourceOf, sources } from './sources.js';
import { createStageBand } from './stageBand.js';
import { saveFlag, savedFlag, savedText, saveText } from './storage.js';
import { TrackNotFound } from './trackNotFound.js';

const SEEK_STEPS = 1000;
const WIPE_FRAME_MS = 33;
const TOAST_MS = 2200;
const SKIP_AFTER_ERROR_MS = 3000;
const OFFSET_STEP_MS = 500;
const COMMAND_SETTLE_MS = 5000;
const COMMAND_POLL_MS = 100;
const CLOCK_DRIFT_MS = 50;
const EFFECTS_KEY = 'effects';
const NO_EFFECTS = { eq: 'flat', reverb: 'off' };
const RENAME_DELAY_MS = 800;
const KARAOKE_KEY = 'karaoke';
const AUTOPLAY_KEY = 'autoplay';
const NEXT_LEAD_SECONDS = 45;
const KARAOKE_TICK_MS = 250;
const KARAOKE_PHASES = {
  installing: { from: 0, to: 0.1, seconds: 5 },
  preparing: { from: 0.1, to: 0.6, seconds: 10 },
  downloading: { from: 0.6, to: 0.7 },
  separating: { from: 0.7, to: 0.98, seconds: 3 }
};
const KEY_SHIFT_KEY = 'keyShift';
const VOLUME_KEY = 'volume';
const VOCAL_KEY = 'vocal';
const SOURCE_KEY = 'searchSource';
const QUEUE_KEY = 'queueOpen';
const RECOMMEND_SEEDS = 5;
const REPEAT_KEY = 'repeat';
const REPEAT_MODES = ['off', 'all', 'one'];
const QUEUE_LABEL = { key: 'tab.queue' };
const ASSUMED_DEVICE_RATE = 48000;
const FRAME_PITCH_LATENCY_MS = (PITCH_LATENCY_FRAMES / ASSUMED_DEVICE_RATE) * 1000;
const POPOVERS = ['fx', 'stylePanel', 'settings'];
const PLAY_ICON = '<svg viewBox="0 0 24 24"><path d="M8 5.5v13a1 1 0 0 0 1.5.86l10.4-6.5a1 1 0 0 0 0-1.72L9.5 4.64A1 1 0 0 0 8 5.5z"/></svg>';
const PAUSE_ICON = '<svg viewBox="0 0 24 24"><rect x="6" y="5" width="4.5" height="14" rx="1"/><rect x="13.5" y="5" width="4.5" height="14" rx="1"/></svg>';
const HEART =
  '<svg viewBox="0 0 24 24"><path d="M12 21s-7.5-4.6-9.6-9.2C.9 8.3 3 4.5 6.7 4.5c2.1 0 3.5 1.2 4.3 2.4h2c.8-1.2 2.2-2.4 4.3-2.4 3.7 0 5.8 3.8 4.3 7.3C19.5 16.4 12 21 12 21z"/></svg>';
const PERSON = '<svg viewBox="0 0 24 24"><circle cx="12" cy="8" r="4"/><path d="M4 20c0-4 3.6-6 8-6s8 2 8 6"/></svg>';
const CHEVRON = '<svg viewBox="0 0 24 24"><path d="M6 9l6 6 6-6"/></svg>';

const $ = (id) => document.getElementById(id);
const clockText = (sec) => {
  const s = Math.max(0, Math.floor(sec));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};
const sameTrack = (a, b) => a && b && a.source === b.source && a.id === b.id;
const trackKey = (track) => `${track.source}:${track.id}`;

let searchSource = sourceOf(savedText(SOURCE_KEY) ?? 'ytm');
let view = 'pick';
let results = null;
const discover = { charts: [], chart: null, state: 'loading', tracks: [], recs: [], recsFor: null };
const played = new Set();
let confirmAction = null;
const queue = [];
let current = null;
let sourceLines = [];
let lyricLines = [];
let lyricsState = 'idle';
let playError = null;
let offsetMs = 0;
let desktop = { visible: false, locked: false };
let sentClock = null;
let sentPlayback = null;
let publishedOffset = 0;
let seeking = false;
let timer = 0;
let toastTimer = 0;
let openPlaylist = null;
let menu = null;
let renameTimer = 0;
let openAccount = null;
let languageOpen = false;
let upNext = null;
let currentAuto = false;
let radioFor = null;
let preparedNext = null;
let keyShift = 0;
let karaokeTicker = 0;
const karaokePhase = new Map();
let autoplay = savedFlag(AUTOPLAY_KEY, true);
let repeatMode = REPEAT_MODES.includes(savedText(REPEAT_KEY)) ? savedText(REPEAT_KEY) : 'off';
let cycle = [];
let cycleLabel = QUEUE_LABEL;
const accountLists = new Map();
const accountLoads = new Map();
let karaokeOn = savedFlag(KARAOKE_KEY, false);
let embedMuted = false;
let holding = false;
const karaokeStatus = new Map();
const karaoke = createKaraoke();
const shown = new Map();
const band = createStageBand($('band'));
const lyricStyle = createLyricStyle({
  band,
  stage: document.querySelector('.stage'),
  toast,
  refresh: () => {
    band.measureAll();
    frame();
  }
});

const playback = createPlayback($('player'), {
  onChange: () => {
    updateKeys();
    frame();
  },
  onEnded: () => songEnded(),
  onError: (code) => {
    if (!current) return;
    playError = code === 100 ? 'error.removed' : code === 101 || code === 150 ? 'error.embedBlocked' : 'error.playback';
    frame();
    const failed = current;
    if (nextTrack()) setTimeout(() => sameTrack(current, failed) && playNext(), SKIP_AFTER_ERROR_MS);
  }
});
const nowPlaying = createNowPlaying();

function show(id, prop, value) {
  const key = `${id}.${prop}`;
  if (shown.get(key) === value) return;
  shown.set(key, value);
  $(id)[prop] = value;
}

function setRange(el, value, max) {
  el.value = String(value);
  el.style.setProperty('--v', `${(value / max) * 100}%`);
}

function toast(text) {
  clearTimeout(toastTimer);
  $('toast').textContent = text;
  $('toast').hidden = false;
  toastTimer = setTimeout(() => ($('toast').hidden = true), TOAST_MS);
}

const coverHtml = (thumb) => (thumb ? `<img src="${escapeHtml(thumb)}" alt="">` : '<img alt="">');
const emptyHtml = (...lines) => `<div class="empty">${lines.map(escapeHtml).join('<br>')}</div>`;
const songCount = (n) => t('count.songs', { n });
const labelText = (label) => (label.key ? t(label.key) : label.text);

function rowHtml(track, i, kind) {
  const isNow = sameTrack(track, current);
  const more = `<button class="btn reveal" data-act="more">${t('row.more')}</button>`;
  const remove = `<button class="btn reveal" data-act="remove">${t('row.remove')}</button>`;
  const acts =
    kind === 'queue'
      ? `<button class="btn reveal" data-act="top">${t('row.top')}</button>${remove}`
      : kind === 'next'
        ? remove + more
        : kind === 'now'
          ? ''
          : `<button class="btn reveal" data-act="insert">${t('row.insert')}</button>${more}<button class="btn pick" data-act="pick">${t('row.pick')}</button>`;
  const mark = library.isFavorite(track) ? `<span class="mark">${HEART}</span>` : '';
  const badge = track.source === 'spotify' ? 'Spotify' : track.kind === 'video' ? t('badge.video') : '';
  return `<div class="row${isNow && kind !== 'queue' && kind !== 'next' ? ' now' : ''}" data-kind="${kind}" data-i="${i}">
    ${coverHtml(track.thumb)}
    <div class="text"><div class="title"><span class="name">${escapeHtml(track.title)}</span>${mark}</div>
      <div class="meta">${badge ? `<span class="badge">${badge}</span>` : ''}<span class="artist">${escapeHtml(track.artist)}</span>${track.duration ? `<span class="time">${clockText(track.duration)}</span>` : ''}</div></div>
    <div class="acts">${acts}</div></div>`;
}

function discoverHtml() {
  const recs = discover.recs.length
    ? `<div class="section">${t('discover.recommended')}</div>${discover.recs.map((track, i) => rowHtml(track, i, 'rec')).join('')}`
    : '';
  const tabs = discover.charts
    .map((chart) => `<button class="seg${chart.id === discover.chart ? ' on' : ''}" data-act="chart" data-id="${chart.id}">${escapeHtml(chart.name)}</button>`)
    .join('');
  const body =
    discover.state === 'loading'
      ? emptyHtml(t('common.loading'))
      : discover.state === 'error'
        ? emptyHtml(t('discover.failed'))
        : discover.tracks.map((track, i) => rowHtml(track, i, 'chart')).join('');
  return `${recs}<div class="section">${t('discover.charts')}</div><div class="chart-tabs">${tabs}</div>${body}`;
}

async function loadChart(id) {
  discover.chart = id;
  discover.state = 'loading';
  renderPanel();
  try {
    const tracks = await sourceOf('ytm').playlistTracks(id);
    if (discover.chart !== id) return;
    discover.tracks = tracks;
    discover.state = 'ready';
  } catch (e) {
    console.error('排行榜讀不到', id, e);
    if (discover.chart !== id) return;
    discover.state = 'error';
  }
  renderPanel();
}

async function loadCharts() {
  try {
    discover.charts = await sourceOf('ytm').charts();
  } catch (e) {
    console.error('排行榜清單讀不到', e);
    discover.state = 'error';
    renderPanel();
    return;
  }
  if (!discover.charts.length) {
    discover.state = 'error';
    renderPanel();
    return;
  }
  loadChart(discover.charts[0].id);
}

const recommendSeeds = () =>
  accounts.signedIn('ytm') ? [] : library.history().filter((track) => track.source === 'ytm').slice(0, RECOMMEND_SEEDS);

function interleave(lists) {
  const mixed = [];
  for (let i = 0; i < Math.max(0, ...lists.map((list) => list.length)); i++) {
    for (const list of lists) if (i < list.length) mixed.push(list[i]);
  }
  return mixed;
}

async function loadRecommended() {
  const seeds = recommendSeeds();
  const key = seeds.length ? seeds.map(trackKey).join('|') : `home:${accounts.signedIn('ytm')}`;
  if (discover.recsFor === key) return;
  discover.recsFor = key;
  const ytm = sourceOf('ytm');
  let found;
  try {
    found = seeds.length ? interleave(await Promise.all(seeds.map((seed) => ytm.radio(seed)))) : await ytm.recommended();
  } catch (e) {
    console.error('推薦讀不到', e);
    found = [];
  }
  if (discover.recsFor !== key) return;
  const sung = new Set(library.history().map(trackKey));
  const seen = new Set();
  discover.recs = found.filter((track) => !sung.has(trackKey(track)) && !seen.has(trackKey(track)) && seen.add(trackKey(track)));
  renderPanel();
}

const pageHeadHtml = (title, sub, acts = '') =>
  `<div class="page-head"><div class="page-text">${title}<div class="page-sub">${escapeHtml(sub)}</div></div><div class="head-acts">${acts}</div></div>`;
const pickAllHtml = (from, count) =>
  `<button class="btn primary" data-act="pickAll" data-from="${from}"${count ? '' : ' disabled'}>${t('list.pickAll')}</button>`;

function favoritesHtml() {
  const favorites = library.favorites();
  const head = pageHeadHtml(`<h1>${t('tab.favorites')}</h1>`, songCount(favorites.length), pickAllHtml('favorites', favorites.length));
  if (!favorites.length) return head + emptyHtml(t('favorites.empty1'), t('favorites.empty2'));
  return head + favorites.map((track, i) => rowHtml(track, i, 'fav')).join('');
}

function accountListHtml() {
  const { name, state, tracks } = openAccount;
  const head = pageHeadHtml(`<h1>${escapeHtml(name)}</h1>`, songCount(tracks.length), pickAllHtml('account', tracks.length));
  if (state === 'loading') return head + emptyHtml(t('common.loading'));
  if (state === 'error') return head + emptyHtml(t('accountList.error'));
  if (!tracks.length) return head + emptyHtml(t('accountList.empty'));
  return head + tracks.map((track, i) => rowHtml(track, i, 'alist')).join('');
}

function playlistHtml() {
  if (openAccount) return accountListHtml();
  if (!openPlaylist) return '';
  const list = library.playlist(openPlaylist);
  const head = pageHeadHtml(
    `<input class="title-input" id="plName" value="${escapeHtml(list.name)}" autocomplete="off">`,
    songCount(list.tracks.length),
    `${pickAllHtml('playlist', list.tracks.length)}<button class="btn danger" data-act="deletePlaylist">${t('playlist.delete')}</button>`
  );
  if (!list.tracks.length) return head + emptyHtml(t('playlist.empty1'), t('playlist.empty2'));
  return head + list.tracks.map((track, i) => rowHtml(track, i, 'plist')).join('');
}

const sideItemHtml = (attrs, on, thumb, name, detail) =>
  `<button class="side-item${on ? ' on' : ''}" ${attrs}>${coverHtml(thumb)}<span class="side-text"><b>${escapeHtml(name)}</b><span>${escapeHtml(detail)}</span></span></button>`;

function sideAccountHtml(source) {
  if (!accounts.signedIn(source.id)) return '';
  const head = `<div class="side-group">${escapeHtml(t('playlists.account', { source: source.name }))}</div>`;
  const entry = accountLists.get(source.id);
  if (!entry || entry.state === 'loading') return `${head}<div class="side-note">${t('common.loading')}</div>`;
  if (entry.state === 'error') return `${head}<div class="side-note">${t('accountLists.error')}</div>`;
  if (!entry.lists.length) return `${head}<div class="side-note">${t('accountLists.empty')}</div>`;
  const openId = openAccount?.source === source.id ? openAccount.id : null;
  return (
    head +
    entry.lists
      .map((list, i) =>
        sideItemHtml(`data-act="openAccount" data-source="${source.id}" data-i="${i}"`, view === 'playlist' && list.id === openId, list.thumb, list.name, list.detail)
      )
      .join('')
  );
}

function sideListsHtml() {
  const local = library
    .playlists()
    .map((list) =>
      sideItemHtml(`data-act="open" data-id="${list.id}"`, view === 'playlist' && list.id === openPlaylist, list.tracks[0]?.thumb, list.name, songCount(list.tracks.length))
    )
    .join('');
  return `${local}<button class="side-add" data-act="newPlaylist">${t('playlists.new')}</button>${sources.map(sideAccountHtml).join('')}`;
}

function upNextHtml() {
  const nowRow = current ? `<div class="section">${t('row.nowPlaying')}</div>${rowHtml(current, -1, 'now')}` : '';
  const queued = queue.length
    ? `<div class="section">${t('queue.upNext')}</div>${queue.map((track, i) => rowHtml(track, i, 'queue')).join('')}`
    : '';
  const following = upNext?.tracks.length
    ? `<div class="section">${escapeHtml(
        upNext.auto ? t('queue.autoplayTitle') : t('queue.playingFrom', { name: labelText(upNext.label) })
      )}</div>${upNext.tracks.map((track, i) => rowHtml(track, i, 'next')).join('')}`
    : '';
  const empty = nextTrack() ? '' : emptyHtml(t('queue.empty1'), t('queue.empty2'));
  return nowRow + queued + following + empty;
}

function renderPanel() {
  $('app').dataset.view = view;
  document.querySelectorAll('#nav [data-view]').forEach((el) => el.classList.toggle('on', el.dataset.view === view));
  $('qcount').textContent = queue.length;
  $('autoplayToggle').classList.toggle('on', autoplay);
  $('sideLists').innerHTML = sideListsHtml();
  if (view === 'favorites') $('favs').innerHTML = favoritesHtml();
  if (view === 'playlist') $('plist').innerHTML = playlistHtml();
  if (view === 'history') $('hist').innerHTML = historyHtml();

  if (!results) $('results').innerHTML = discoverHtml();
  else if (!results.length) $('results').innerHTML = emptyHtml(t('search.none'));
  else $('results').innerHTML = results.map((track, i) => rowHtml(track, i, 'pick')).join('');

  $('queued').innerHTML = upNextHtml();
}

function dayLabel(at) {
  const day = new Date(at);
  const today = new Date();
  const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1);
  if (day.toDateString() === today.toDateString()) return t('history.today');
  if (day.toDateString() === yesterday.toDateString()) return t('history.yesterday');
  const year = day.getFullYear() === today.getFullYear() ? {} : { year: 'numeric' };
  return new Intl.DateTimeFormat(language.id(), { month: 'long', day: 'numeric', ...year }).format(day);
}

function setView(next) {
  if (view === 'playlist' && next !== 'playlist') savePlaylistName();
  view = next;
  renderPanel();
  if (view === 'pick') loadRecommended();
}

function historyHtml() {
  const items = library.history();
  const head = pageHeadHtml(
    `<h1>${t('tab.history')}</h1>`,
    songCount(items.length),
    items.length ? `<button class="btn danger" data-act="clearHistory">${t('history.clear')}</button>` : ''
  );
  if (!items.length) return head + emptyHtml(t('history.empty1'), t('history.empty2'));
  let shownDay = '';
  const rows = items
    .map((track, i) => {
      const day = dayLabel(track.playedAt);
      const section = day === shownDay ? '' : `<div class="section">${day}</div>`;
      shownDay = day;
      return section + rowHtml(track, i, 'hist');
    })
    .join('');
  return head + rows;
}

function ask(text, okLabel, action) {
  $('confirmText').textContent = text;
  $('confirmOk').textContent = okLabel;
  confirmAction = action;
  $('confirm').hidden = false;
}

const previewOnly = (sourceId) => sourceId === 'spotify' && !accounts.signedIn('spotify');

function loginButtonHtml(source) {
  const key = accounts.state(source.id)?.state === 'expired' ? 'account.signInAgainTo' : 'account.signInTo';
  return `<button class="btn" data-act="login" data-source="${source.id}">${escapeHtml(t(key, { source: source.name }))}</button>`;
}

function accountRowHtml(source) {
  const account = accounts.state(source.id);
  const button = (act, label, primary) =>
    `<button class="btn${primary ? ' primary' : ''}" data-act="${act}" data-source="${source.id}">${t(label)}</button>`;
  const [detail, action] = !account
    ? [t('account.checking'), '']
    : account.state === 'active'
      ? [account.name || t('account.signedIn'), button('logout', 'account.signOut', false)]
      : account.state === 'expired'
        ? [t('account.expired'), button('login', 'account.signInAgain', true)]
        : [t(`account.benefit.${source.id}`), button('login', 'account.signIn', false)];
  const face = account?.avatar ? `<img src="${escapeHtml(account.avatar)}" alt="">` : PERSON;
  return `<div class="acct-row${account?.state === 'expired' ? ' expired' : ''}"><span class="acct-face">${face}</span>
    <div class="acct-text"><div class="acct-source">${source.name}</div><div class="acct-detail">${escapeHtml(detail)}</div></div>${action}</div>`;
}

function renderAccount() {
  const states = sources.map((source) => accounts.state(source.id));
  const active = states.filter((account) => account?.state === 'active');
  const faces = active
    .filter((account) => account.avatar)
    .map((account) => `<img src="${escapeHtml(account.avatar)}" alt="">`)
    .join('');
  const expired = states.some((account) => account?.state === 'expired');
  const signedIn = sources.filter((source) => accounts.signedIn(source.id)).map((source) => source.name);
  const status = expired
    ? t('account.expired')
    : signedIn.length
      ? t('me.signedIn', { sources: new Intl.ListFormat(language.id()).format(signedIn) })
      : t('me.signedOut');
  $('settingsToggle').innerHTML = `${faces ? `<span class="me-faces">${faces}</span>` : ''}<span class="me-text"><b>${t('settings.label')}</b><span>${escapeHtml(status)}</span></span>`;
  $('settingsToggle').classList.toggle('expired', expired);
  $('accountRows').innerHTML = sources.map(accountRowHtml).join('');
  renderLanguagePicker();
  const preview = previewOnly(searchSource.id);
  $('pickHint').hidden = !preview;
  if (preview)
    $('pickHint').innerHTML = `<span>${escapeHtml(t('preview.notice', { source: searchSource.name }))}</span>${loginButtonHtml(searchSource)}`;
}

function renderLanguagePicker() {
  const followsSystem = language.setting() === SYSTEM_LANGUAGE;
  const active = LANGUAGES.find((option) => option.id === language.id()).name;
  $('languageCurrent').innerHTML = `<span class="select-name">${followsSystem ? t('language.system') : active}</span>
    <span class="select-note">${followsSystem ? active : ''}</span>${CHEVRON}`;
  $('languageCurrent').classList.toggle('open', languageOpen);
  $('languageList').hidden = !languageOpen;
  $('languageList').innerHTML = [{ id: SYSTEM_LANGUAGE, name: t('language.system') }, ...LANGUAGES]
    .map(
      (option) =>
        `<button class="select-option${option.id === language.setting() ? ' on' : ''}" data-language="${option.id}">${option.name}</button>`
    )
    .join('');
}

function renderLanguage() {
  lyricLines = inScript(sourceLines, language.script());
  band.reset();
  announceSong();
  sources.forEach(loadAccountLists);
  renderAccount();
  renderPanel();
  renderNowPlaying();
  renderKeyShift();
  renderDesktopKeys();
  renderEffects();
  lyricStyle.languageChanged();
  frame();
}

function prepareKaraoke(track, from, urgent) {
  if (karaokeOn && track?.source === 'ytm') call('karaoke.prepare', { id: track.id, from, urgent });
}

function setEmbedMuted(muted) {
  if (muted === embedMuted) return;
  embedMuted = muted;
  playback.setMuted(muted);
}

function phaseOf(status) {
  switch (status?.state) {
    case 'installing':
      return 'installing';
    case 'downloading':
      return status.progress > 0 ? 'downloading' : 'preparing';
    case 'separating':
    case 'segment':
    case 'complete':
      return 'separating';
    default:
      return 'preparing';
  }
}

function notePhase(id, status) {
  const name = phaseOf(status);
  if (karaokePhase.get(id)?.name !== name) karaokePhase.set(id, { name, since: performance.now() });
}

function karaokeLoading() {
  if (!karaokeOn || !current || current.source !== 'ytm' || embedMuted) return null;
  const status = karaokeStatus.get(current.id);
  if (status?.state === 'error') return null;
  const name = phaseOf(status);
  const phase = KARAOKE_PHASES[name];
  const elapsed = (performance.now() - (karaokePhase.get(current.id)?.since ?? performance.now())) / 1000;
  const share = name === 'downloading' ? status.progress : Math.min(1, elapsed / phase.seconds);
  const step =
    name === 'downloading'
      ? t('karaoke.phase.downloading', { percent: Math.round(status.progress * 100) })
      : t(`karaoke.phase.${name}`);
  return { step, progress: phase.from + (phase.to - phase.from) * share };
}

function syncKaraokeTicker() {
  if (holding && !karaokeTicker) karaokeTicker = setInterval(renderKaraoke, KARAOKE_TICK_MS);
  if (!holding && karaokeTicker) {
    clearInterval(karaokeTicker);
    karaokeTicker = 0;
  }
}

function karaokeNote() {
  if (!karaokeOn || !current) return null;
  if (current.source !== 'ytm') return t('karaoke.unsupported', { source: sourceOf(current.source).name });
  const status = karaokeStatus.get(current.id);
  if (status?.state === 'error') return t('karaoke.failed');
  const loading = karaokeLoading();
  return loading ? t('karaoke.preparing', { step: loading.step }) : null;
}

function renderKaraoke() {
  const loading = karaokeLoading();
  $('kMask').hidden = !holding;
  if (holding) {
    $('kMaskTitle').textContent = t('karaoke.maskTitle', { title: current.title });
    $('kMaskBar').style.width = `${Math.round((loading?.progress ?? 0) * 100)}%`;
    $('kMaskStep').textContent = loading?.step ?? t('karaoke.preparingShort');
  }
  $('kToggle').classList.toggle('on', karaokeOn);
  $('kToggle').classList.toggle('loading', Boolean(loading));
  $('kToggle').style.setProperty('--p', `${Math.round((loading?.progress ?? 0) * 100)}%`);
  $('vocal').disabled = !karaokeOn;
  $('vocalRow').classList.toggle('off', !karaokeOn);
  syncKaraokeTicker();
}

function checkHold() {
  if (!holding || !current) return;
  const failed = karaokeStatus.get(current.id)?.state === 'error';
  if (failed || karaoke.sync(playback.time(), false)) {
    holding = false;
    setEmbedMuted(!failed);
    playback.play();
    if (!failed) toast(t('karaoke.ready'));
  }
  renderNowPlaying();
  frame();
}

function renderNowPlaying() {
  $('app').toggleAttribute('data-idle', !current);
  $('npTitle').textContent = current ? current.title : t('np.idleTitle');
  $('npArtist').textContent = current ? current.artist : t('np.idleHint');
  $('npNext').textContent =
    karaokeNote() ??
    (current && previewOnly(current.source) ? t('preview.notice', { source: sourceOf(current.source).name }) : null) ??
    (nextTrack() ? t('np.next', { title: nextTrack().title }) : t('np.nextEmpty'));
  renderKaraoke();
  if (current) $('cover').src = current.thumb;
  else $('cover').removeAttribute('src');
  $('favToggle').hidden = !current;
  $('favToggle').classList.toggle('on', Boolean(current) && library.isFavorite(current));
  updateKeys();
  nowPlaying.send('track', {
    title: current?.title ?? '',
    artist: current?.artist ?? '',
    album: current?.album ?? '',
    cover: current?.thumb ?? ''
  });
}

function toggleFavorite(track) {
  const favorite = library.toggleFavorite(track);
  toast(t(favorite ? 'toast.favorited' : 'toast.unfavorited', { title: track.title }));
  renderNowPlaying();
  renderPanel();
}

function pickAll(tracks) {
  if (!tracks.length) return;
  const [first, ...rest] = tracks;
  if (!current) playSong(first);
  else queue.push(first);
  queue.push(...rest);
  tracks.forEach(dropFromAutoplay);
  renderNowPlaying();
  renderPanel();
  toast(t('toast.pickedAll', { n: tracks.length }));
}

function closeMenu() {
  $('menu').hidden = true;
  menu = null;
}

function openMenu(button, row) {
  const track = trackOfRow(row);
  menu = { track, kind: row.dataset.kind, i: Number(row.dataset.i) };
  const lists = library.playlists()
    .map((list) => `<button data-menu="add" data-id="${list.id}">${escapeHtml(list.name)}</button>`)
    .join('');
  const removal = { plist: 'removeFromPlaylist', hist: 'removeFromHistory' }[menu.kind];
  $('menu').innerHTML = `<button data-menu="insert">${t('row.insert')}</button>
    <button data-menu="favorite">${t(library.isFavorite(track) ? 'action.unfavorite' : 'action.favorite')}</button>
    <div class="menu-title">${t('menu.addTo')}</div>${lists}<button data-menu="addNew">${t('menu.addNew')}</button>
    ${removal ? `<div class="menu-sep"></div><button data-menu="${removal}">${t(`menu.${removal}`)}</button>` : ''}`;
  $('menu').hidden = false;
  const anchor = button.getBoundingClientRect();
  const box = $('menu').getBoundingClientRect();
  const below = anchor.bottom + 4 + box.height <= window.innerHeight;
  $('menu').style.left = `${Math.max(8, anchor.right - box.width)}px`;
  $('menu').style.top = `${below ? anchor.bottom + 4 : anchor.top - box.height - 4}px`;
}

function addToPlaylist(id, track) {
  const list = library.playlist(id);
  toast(t(library.addToPlaylist(id, track) ? 'toast.addedTo' : 'toast.alreadyIn', { name: list.name }));
  renderPanel();
}

function savePlaylistName() {
  clearTimeout(renameTimer);
  const input = $('plName');
  const name = input?.value.trim();
  if (openPlaylist && name) library.renamePlaylist(openPlaylist, name);
}

function updateKeys() {
  const playing = playback.isPlaying();
  show('toggle', 'innerHTML', playing ? PAUSE_ICON : PLAY_ICON);
  $('toggle').setAttribute('aria-label', t(playing ? 'key.pause' : 'key.play'));
  show('toggle', 'disabled', !current);
  show('restart', 'disabled', !current);
  show('skip', 'disabled', !current);
  show('repeatToggle', 'textContent', t(`repeat.${repeatMode}`));
  $('repeatToggle').classList.toggle('on', repeatMode !== 'off');
}

function linkedTrack(text) {
  for (const source of sources) {
    const id = source.idOfLink(text);
    if (id) return { source, id };
  }
  return null;
}

async function searchSongs(source, text) {
  const query = parseSongQuery(text);
  return rankForQuery(query, await source.search(songSearchText(query)));
}

async function doSearch(text) {
  setView('pick');
  $('results').innerHTML = emptyHtml(t('search.searching'));
  const link = linkedTrack(text);
  try {
    results = link ? [await link.source.track(link.id)] : await searchSongs(searchSource, text);
  } catch (e) {
    console.error('搜尋失敗', e);
    $('results').innerHTML = emptyHtml(t(link ? 'search.linkFailed' : 'search.failed'));
    return;
  }
  $('results').scrollTop = 0;
  renderPanel();
}

function utf16Chars(line) {
  const positions = [];
  let at = 0;
  for (const ch of line.text) {
    positions.push([at, ch.length]);
    at += ch.length;
  }
  return line.chars.map((c) => ({ t: c.t, e: c.e, pos: positions[c.i][0], len: positions[c.i][1] }));
}

function announceSong() {
  call('desktopLyrics.song', {
    title: current?.title ?? '',
    artist: current?.artist ?? '',
    lines: lyricsState === 'ready' ? lyricLines.map((line) => ({ start: line.start, text: line.text, chars: utf16Chars(line) })) : []
  });
  sentClock = null;
  publishLyrics();
}

function sharedLyricsState() {
  switch (lyricsState) {
    case 'ready':
    case 'loading':
    case 'error':
      return lyricsState;
    case 'idle':
    case 'mv':
    case 'preview':
      return 'none';
    default:
      throw new Error(`歌詞狀態對不上歌詞檔：${lyricsState}`);
  }
}

function publishLyrics() {
  const offset = Math.round(lyricOffsetMs());
  publishedOffset = offset;
  const lines = lyricsState === 'ready' ? lyricLines : [];
  nowPlaying.send('lyrics', {
    text: JSON.stringify({
      title: current?.title ?? '',
      artist: current?.artist ?? '',
      source: current?.source ?? '',
      id: current?.id ?? '',
      ...(current?.requestedAs === undefined ? {} : { requestedAs: current.requestedAs }),
      ...(current?.thumb?.startsWith('https://') ? { cover: current.thumb } : {}),
      state: sharedLyricsState(),
      lines: lines.map((line) => {
        const text = [...line.text];
        return {
          start: line.start - offset,
          text: line.text,
          chars: line.chars.map((c) => ({ start: c.t - offset, end: c.e - offset, text: text[c.i] }))
        };
      })
    })
  });
}

function publishPlayback() {
  const at = Date.now();
  const duration = current ? Math.round(playback.duration() * 1000) : 0;
  const info = {
    state: !current
      ? 'idle'
      : holding
        ? 'loading'
        : playback.isPlaying()
          ? 'playing'
          : playback.wantsToPlay() && !playError
            ? 'loading'
            : 'paused',
    duration
  };
  const position = current ? Math.round(playback.time() * 1000) : 0;
  if (sentPlayback && info.state === sentPlayback.state && info.duration === sentPlayback.duration) {
    const predicted = sentPlayback.position + (info.state === 'playing' ? at - sentPlayback.at : 0);
    if (Math.abs(predicted - position) < CLOCK_DRIFT_MS) return;
  }
  sentPlayback = { ...info, position, at };
  nowPlaying.send('playback', { state: info.state, position, duration: info.duration });
}


function sendClock(ms, playing) {
  const at = performance.now();
  if (sentClock && sentClock.playing === playing) {
    const predicted = sentClock.ms + (playing ? at - sentClock.at : 0);
    if (Math.abs(predicted - ms) < CLOCK_DRIFT_MS) return;
  }
  sentClock = { ms, at, playing };
  call('desktopLyrics.clock', { ms, playing });
}

async function playSong(track, auto = false) {
  if (!current) view = 'stage';
  current = track;
  currentAuto = auto;
  if (!auto && !cycle.some((item) => sameTrack(item, track))) cycle.push(track);
  playError = null;
  offsetMs = 0;
  played.add(trackKey(track));
  preparedNext = null;
  applyKeyShift(savedKeyShifts()[trackKey(track)] ?? 0);
  library.addHistory(track);
  karaoke.setSong(track.source === 'ytm' ? track.id : null);
  setEmbedMuted(false);
  holding = karaokeOn && track.source === 'ytm';
  if (holding) notePhase(track.id, karaokeStatus.get(track.id));
  playback.load(track, !holding);
  prepareKaraoke(track, 0, true);
  sourceLines = [];
  lyricLines = [];
  lyricsState = track.kind === 'video' ? 'mv' : previewOnly(track.source) ? 'preview' : 'loading';
  renderStage();
  announceSong();
  frame();
  renderNowPlaying();
  renderPanel();
  refreshAutoplay();
  if (lyricsState === 'mv' || lyricsState === 'preview') return;

  let lyrics;
  try {
    lyrics = await lyricsFor(track);
  } catch (e) {
    console.error('歌詞失敗', e);
    if (!sameTrack(current, track)) return;
    lyricsState = 'error';
    announceSong();
    frame();
    return;
  }
  if (!sameTrack(current, track)) return;
  console.info('歌詞', track.title, lyrics.label);
  sourceLines = lyrics.lines;
  lyricLines = inScript(sourceLines, language.script());
  lyricsState = lyricLines.length ? 'ready' : 'mv';
  renderStage();
  announceSong();
  frame();
}

function renderStage() {
  document.querySelector('.stage').classList.toggle('mv', lyricsState === 'mv');
}

function stopAll() {
  playback.stop();
  karaoke.setSong(null);
  setEmbedMuted(false);
  holding = false;
  current = null;
  cycle = [];
  applyKeyShift(0);
  sourceLines = [];
  lyricLines = [];
  lyricsState = 'idle';
  playError = null;
  renderStage();
  renderNowPlaying();
  renderPanel();
  announceSong();
  frame();
}

function restart() {
  if (!current) return;
  playback.seek(0);
  frame();
}

function shiftLyrics(delta) {
  if (!current) return;
  offsetMs += delta;
  const seconds = Math.abs(offsetMs) / 1000;
  toast(offsetMs === 0 ? t('toast.lyricsReset') : t(offsetMs > 0 ? 'toast.lyricsEarlier' : 'toast.lyricsLater', { seconds }));
  frame();
}

function savedEffects() {
  try {
    const saved = JSON.parse(localStorage.getItem(EFFECTS_KEY));
    effectSettings(saved.eq, saved.reverb);
    return saved;
  } catch {
    return { ...NO_EFFECTS };
  }
}

let effects = savedEffects();

function renderEffects() {
  const segment = (preset, kind, chosen) =>
    `<button class="seg${preset.id === chosen ? ' on' : ''}" data-${kind}="${preset.id}">${t(`${kind}.${preset.id}`)}</button>`;
  $('fxEq').innerHTML = EQ_PRESETS.map((preset) => segment(preset, 'eq', effects.eq)).join('');
  $('fxReverb').innerHTML = REVERB_PRESETS.map((preset) => segment(preset, 'reverb', effects.reverb)).join('');
  $('fxToggle').classList.toggle('on', effects.eq !== NO_EFFECTS.eq || effects.reverb !== NO_EFFECTS.reverb);
}

function applyEffects() {
  const settings = effectSettings(effects.eq, effects.reverb);
  playback.setEffects(settings);
  karaoke.setEffects(settings);
  try {
    localStorage.setItem(EFFECTS_KEY, JSON.stringify(effects));
  } catch (e) {
    console.error('音效設定存不起來', e);
  }
}

function savedKeyShifts() {
  try {
    return JSON.parse(savedText(KEY_SHIFT_KEY)) ?? {};
  } catch {
    return {};
  }
}

function applyKeyShift(value) {
  keyShift = value;
  playback.setPitch(value);
  karaoke.setPitch(value);
  renderKeyShift();
}

function changeKeyShift(delta) {
  if (!current) return;
  const shifts = savedKeyShifts();
  const value = keyShift + delta;
  if (value) shifts[trackKey(current)] = value;
  else delete shifts[trackKey(current)];
  saveText(KEY_SHIFT_KEY, JSON.stringify(shifts));
  applyKeyShift(value);
  frame();
}

function renderKeyShift() {
  const usable = Boolean(current) && !previewOnly(current.source);
  show('keyDown', 'disabled', !usable);
  show('keyUp', 'disabled', !usable);
  show('keyValue', 'textContent', keyShift === 0 ? t('key.original') : keyShift > 0 ? `+${keyShift}` : `−${-keyShift}`);
}

function renderDesktopKeys() {
  $('dlToggle').classList.toggle('on', desktop.visible);
  $('dlToggle').textContent = t(desktop.visible && desktop.locked ? 'key.unlockLyrics' : 'key.desktopLyrics');
}

const nextTrack = () => queue[0] ?? upNext?.tracks[0] ?? (repeatMode === 'all' && current ? cycle[0] : null) ?? null;

function prepareNextWhenClose(now, duration) {
  if (!karaokeOn || !current || !duration || duration - now > NEXT_LEAD_SECONDS) return;
  const next = nextTrack();
  if (!next || next.source !== 'ytm' || preparedNext === trackKey(next)) return;
  preparedNext = trackKey(next);
  prepareKaraoke(next, 0, false);
}

function playNext() {
  if (queue.length) playSong(queue.shift());
  else if (upNext?.tracks.length) playSong(upNext.tracks.shift(), upNext.auto);
  else if (repeatMode === 'all' && cycle.length) {
    const [first, ...rest] = cycle;
    upNext = rest.length ? { label: cycleLabel, tracks: [...rest], auto: false } : null;
    playSong(first);
  } else stopAll();
}

function songEnded() {
  if (repeatMode !== 'one' || !current) {
    playNext();
    return;
  }
  playback.seek(0);
  playback.play();
  frame();
}

function playFrom(track, label = null, rest = []) {
  upNext = rest.length ? { label, tracks: [...rest], auto: false } : null;
  cycle = [track, ...rest];
  cycleLabel = label ?? QUEUE_LABEL;
  playSong(track);
}

const autoplayOwnsNext = () => !upNext || upNext.auto;

function dropFromAutoplay(track) {
  if (upNext?.auto) upNext.tracks = upNext.tracks.filter((item) => !sameTrack(item, track));
}

async function refreshAutoplay() {
  if (!autoplay || repeatMode === 'all' || !current || !autoplayOwnsNext()) return;
  if (currentAuto && upNext?.tracks.length) return;
  const seed = current;
  if (radioFor === trackKey(seed)) return;
  radioFor = trackKey(seed);
  let found;
  try {
    found = await sourceOf(seed.source).radio(seed);
  } catch (e) {
    console.error('自動播放找不到相似的歌', seed.title, e);
    return;
  } finally {
    if (radioFor === trackKey(seed)) radioFor = null;
  }
  if (!autoplay || repeatMode === 'all' || !sameTrack(current, seed) || !autoplayOwnsNext()) return;
  upNext = {
    label: null,
    tracks: found.filter((track) => !played.has(trackKey(track)) && !queue.some((item) => sameTrack(item, track))),
    auto: true
  };
  renderNowPlaying();
  renderPanel();
}

function pick(track, notify = true) {
  if (!current) {
    playFrom(track);
    if (notify) toast(t(holding ? 'toast.holding' : 'toast.playing', { title: track.title }));
    return;
  }
  queue.push(track);
  dropFromAutoplay(track);
  renderNowPlaying();
  renderPanel();
  if (notify) toast(t('toast.picked', { title: track.title, position: queue.length }));
}

function insert(track, notify = true) {
  if (!current) {
    pick(track, notify);
    return;
  }
  queue.unshift(track);
  dropFromAutoplay(track);
  renderNowPlaying();
  renderPanel();
  if (notify) toast(t('toast.inserted', { title: track.title }));
}

const noticeOf = (key, params) => ({ key: `notice:${key}:${language.id()}`, notice: t(key, params) });

function bandState(ms) {
  if (!current) return { ms, card: null, notice: noticeOf('stage.idle'), lines: null };
  const card = { key: `title:${current.source}:${current.id}`, card: true, title: current.title, artist: current.artist };
  const notice = playError
    ? { ...noticeOf(playError), error: true }
    : lyricsState === 'preview'
      ? noticeOf('stage.preview', { source: sourceOf(current.source).name })
      : lyricsState === 'error'
        ? noticeOf('stage.lyricsError')
        : null;
  return { ms, card, notice, lines: !playError && lyricsState === 'ready' ? lyricLines : null };
}

const lyricOffsetMs = () => offsetMs - (keyShift && !embedMuted ? FRAME_PITCH_LATENCY_MS : 0);

function nextClockChangeAt(ms, durationMs) {
  const candidates = [(Math.floor(ms / 1000) + 1) * 1000];
  if (durationMs) {
    const step = durationMs / SEEK_STEPS;
    candidates.push((Math.floor(ms / step) + 1) * step);
  }
  return Math.min(...candidates);
}

function nextLyricChangeAt(ms) {
  const nextLine = lyricLines.find((line) => line.start > ms)?.start ?? Infinity;
  return Math.min(nextLine, band.nextChangeAt(ms));
}

function frame() {
  clearTimeout(timer);
  const now = playback.time();
  const duration = playback.duration();
  show('cur', 'textContent', clockText(current ? now : 0));
  show('dur', 'textContent', clockText(current ? duration : 0));
  if (!seeking) {
    const value = current && duration ? Math.round((now / duration) * SEEK_STEPS) : 0;
    if (shown.get('seek') !== value) {
      shown.set('seek', value);
      setRange($('seek'), value, SEEK_STEPS);
    }
  }
  const playerMs = now * 1000;
  const lyricMs = playerMs + lyricOffsetMs();
  const playing = Boolean(current) && playback.isPlaying();
  const wiping = band.render(bandState(lyricMs));
  sendClock(lyricMs, playing);
  publishPlayback();
  if (Math.round(lyricOffsetMs()) !== publishedOffset) publishLyrics();
  setEmbedMuted(karaokeOn && Boolean(current) && karaoke.sync(now, playing));
  prepareNextWhenClose(now, duration);
  if (!playing) return;
  const delay = wiping
    ? WIPE_FRAME_MS
    : Math.min(nextClockChangeAt(playerMs, duration * 1000) - playerMs, nextLyricChangeAt(lyricMs) - lyricMs);
  timer = setTimeout(frame, delay);
}

function trackOfRow(row) {
  const i = Number(row.dataset.i);
  switch (row.dataset.kind) {
    case 'pick':
      return results[i];
    case 'queue':
      return queue[i];
    case 'now':
      return current;
    case 'fav':
      return library.favorites()[i];
    case 'plist':
      return library.playlist(openPlaylist).tracks[i];
    case 'next':
      return upNext.tracks[i];
    case 'alist':
      return openAccount.tracks[i];
    case 'hist':
      return library.history()[i];
    case 'chart':
      return discover.tracks[i];
    case 'rec':
      return discover.recs[i];
    default:
      throw new Error(`不認得的清單：${row.dataset.kind}`);
  }
}

async function loadAccountLists(source) {
  if (!accounts.signedIn(source.id)) {
    accountLists.delete(source.id);
    return;
  }
  if (!accountLists.has(source.id)) accountLists.set(source.id, { state: 'loading', lists: [] });
  const serial = (accountLoads.get(source.id) ?? 0) + 1;
  accountLoads.set(source.id, serial);
  let loaded;
  try {
    loaded = { state: 'ready', lists: await source.playlists() };
  } catch (e) {
    console.error(`${source.name} 帳號歌單讀不到`, e);
    loaded = { state: 'error', lists: [] };
  }
  if (accountLoads.get(source.id) !== serial) return;
  accountLists.set(source.id, loaded);
  renderPanel();
}

async function openAccountList(source, list) {
  const opened = { source: source.id, id: list.id, name: list.name, state: 'loading', tracks: [] };
  savePlaylistName();
  openAccount = opened;
  openPlaylist = null;
  setView('playlist');
  try {
    opened.tracks = await source.playlistTracks(list.id);
    opened.state = 'ready';
  } catch (e) {
    console.error(`${source.name} 歌單「${list.name}」讀不到`, e);
    opened.state = 'error';
  }
  if (openAccount === opened) renderPanel();
}

function pickAllFrom(from) {
  switch (from) {
    case 'favorites':
      return pickAll(library.favorites());
    case 'playlist':
      return pickAll(library.playlist(openPlaylist).tracks);
    case 'account':
      return pickAll(openAccount.tracks);
    default:
      throw new Error(`不認得的全部點歌來源：${from}`);
  }
}

function playRow(row) {
  const i = Number(row.dataset.i);
  switch (row.dataset.kind) {
    case 'pick':
      return playFrom(trackOfRow(row));
    case 'fav': {
      const tracks = library.favorites();
      return playFrom(tracks[i], { key: 'tab.favorites' }, tracks.slice(i + 1));
    }
    case 'plist': {
      const list = library.playlist(openPlaylist);
      return playFrom(list.tracks[i], { text: list.name }, list.tracks.slice(i + 1));
    }
    case 'alist':
      return playFrom(openAccount.tracks[i], { text: openAccount.name }, openAccount.tracks.slice(i + 1));
    case 'hist': {
      const tracks = library.history();
      return playFrom(tracks[i], { key: 'tab.history' }, tracks.slice(i + 1));
    }
    case 'chart': {
      const name = discover.charts.find((chart) => chart.id === discover.chart)?.name ?? '';
      return playFrom(discover.tracks[i], { text: name }, discover.tracks.slice(i + 1));
    }
    case 'rec':
      return playFrom(discover.recs[i], { key: 'discover.recommended' }, discover.recs.slice(i + 1));
    case 'queue':
      return playSong(queue.splice(i, 1)[0]);
    case 'next':
      return playSong(upNext.tracks.splice(0, i + 1).at(-1), upNext.auto);
    case 'now':
      return undefined;
    default:
      throw new Error(`不認得的清單：${row.dataset.kind}`);
  }
}

const lists = [$('side'), $('main'), $('queuePanel')];

function onListAct(e) {
  const target = e.target.closest('[data-act]');
  if (!target) return;
  const row = target.closest('.row[data-kind]');
  switch (target.dataset.act) {
    case 'pick':
      pick(trackOfRow(row));
      break;
    case 'insert':
      insert(trackOfRow(row));
      break;
    case 'top':
    case 'remove': {
      const list = row.dataset.kind === 'next' ? upNext.tracks : queue;
      const [track] = list.splice(Number(row.dataset.i), 1);
      if (target.dataset.act === 'top') queue.unshift(track);
      renderNowPlaying();
      renderPanel();
      if (!nextTrack()) refreshAutoplay();
      break;
    }
    case 'more':
      openMenu(target, row);
      break;
    case 'pickAll':
      pickAllFrom(target.dataset.from);
      break;
    case 'open':
      savePlaylistName();
      openPlaylist = target.dataset.id;
      openAccount = null;
      setView('playlist');
      break;
    case 'openAccount': {
      const source = sourceOf(target.dataset.source);
      openAccountList(source, accountLists.get(source.id).lists[Number(target.dataset.i)]);
      break;
    }
    case 'newPlaylist':
      savePlaylistName();
      openPlaylist = library.createPlaylist().id;
      openAccount = null;
      setView('playlist');
      $('plName').focus();
      $('plName').select();
      break;
    case 'deletePlaylist': {
      const list = library.playlist(openPlaylist);
      ask(t('confirm.deletePlaylist', { name: list.name, n: list.tracks.length }), t('common.delete'), () => {
        library.deletePlaylist(openPlaylist);
        openPlaylist = null;
        setView('pick');
        toast(t('toast.playlistDeleted'));
      });
      break;
    }
    case 'chart':
      if (target.dataset.id !== discover.chart) loadChart(target.dataset.id);
      break;
    case 'clearHistory':
      ask(t('confirm.clearHistory', { n: library.history().length }), t('common.clear'), () => {
        library.clearHistory();
        renderPanel();
        toast(t('toast.historyCleared'));
      });
      break;
    case 'login':
      accounts.login(sourceOf(target.dataset.source));
      break;
    default:
      throw new Error(`不認得的操作：${target.dataset.act}`);
  }
}

for (const list of lists) {
  list.addEventListener('click', onListAct);
  list.addEventListener('dblclick', (e) => {
    const row = e.target.closest('.row[data-kind]');
    if (row && !e.target.closest('button')) playRow(row);
  });
}
$('main').addEventListener('input', (e) => {
  if (e.target.id !== 'plName') return;
  clearTimeout(renameTimer);
  renameTimer = setTimeout(() => {
    savePlaylistName();
    $('sideLists').innerHTML = sideListsHtml();
  }, RENAME_DELAY_MS);
});
$('main').addEventListener('focusout', (e) => {
  if (e.target.id === 'plName') savePlaylistName();
});
$('main').addEventListener('keydown', (e) => {
  if (e.target.id === 'plName' && (e.key === 'Enter' || e.key === 'Escape')) e.target.blur();
});
$('nav').addEventListener('click', (e) => {
  const button = e.target.closest('[data-view]');
  if (button) setView(button.dataset.view);
});
$('miniOpen').addEventListener('click', () => setView('stage'));
$('queueToggle').addEventListener('click', () => {
  const open = $('app').classList.toggle('queue-closed') === false;
  saveFlag(QUEUE_KEY, open);
  $('queueToggle').classList.toggle('on', open);
});
$('menu').addEventListener('click', (e) => {
  const button = e.target.closest('[data-menu]');
  if (!button || !menu) return;
  const { track, i } = menu;
  closeMenu();
  switch (button.dataset.menu) {
    case 'insert':
      insert(track);
      break;
    case 'favorite':
      toggleFavorite(track);
      break;
    case 'add':
      addToPlaylist(button.dataset.id, track);
      break;
    case 'addNew':
      addToPlaylist(library.createPlaylist().id, track);
      break;
    case 'removeFromPlaylist':
      library.removeFromPlaylist(openPlaylist, i);
      renderPanel();
      break;
    case 'removeFromHistory':
      library.removeHistory(i);
      renderPanel();
      break;
    default:
      throw new Error(`不認得的選單項目：${button.dataset.menu}`);
  }
});
$('favToggle').addEventListener('click', () => {
  if (current) toggleFavorite(current);
});
$('confirmCancel').addEventListener('click', () => ($('confirm').hidden = true));
$('confirmOk').addEventListener('click', () => {
  $('confirm').hidden = true;
  confirmAction();
});
$('searchForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const text = $('q').value.trim();
  if (text) doSearch(text);
  else {
    results = null;
    renderPanel();
  }
});
$('srcTabs').addEventListener('click', (e) => {
  const button = e.target.closest('[data-source]');
  if (!button || button.classList.contains('on')) return;
  searchSource = sourceOf(button.dataset.source);
  saveText(SOURCE_KEY, searchSource.id);
  document.querySelectorAll('#srcTabs .src-tab').forEach((el) => el.classList.toggle('on', el === button));
  renderAccount();
  if (!results) renderPanel();
  const text = $('q').value.trim();
  if (text) doSearch(text);
});
function skipHold() {
  holding = false;
  setEmbedMuted(false);
  playback.play();
  renderNowPlaying();
  frame();
}

$('toggle').addEventListener('click', () => {
  if (holding) skipHold();
  else playback.toggle();
});
$('kMaskSkip').addEventListener('click', skipHold);
$('restart').addEventListener('click', restart);
function requireCurrent() {
  if (!current) throw new RequestError(409, '現在沒有在播的歌');
}

function pauseNow() {
  holding = false;
  playback.pause();
  renderNowPlaying();
  frame();
}

function playNow() {
  if (holding) skipHold();
  else playback.play();
}

async function settle(playing) {
  const deadline = performance.now() + COMMAND_SETTLE_MS;
  while (playback.isPlaying() !== playing) {
    if (performance.now() > deadline) throw new RequestError(504, playing ? '播放器沒有開始播' : '播放器沒有停下來');
    await new Promise((resolve) => setTimeout(resolve, COMMAND_POLL_MS));
  }
}

async function runCommand(request) {
  switch (request.action) {
    case 'play':
      requireCurrent();
      playNow();
      await settle(true);
      break;
    case 'pause':
      requireCurrent();
      pauseNow();
      await settle(false);
      break;
    case 'next':
      requireCurrent();
      playNext();
      break;
    default:
      throw new RequestError(400, `不認得的 action：${request.action}`);
  }
}

function apiSource(id) {
  if (id === undefined) return searchSource;
  if (!sources.some((source) => source.id === id))
    throw new RequestError(400, `沒有這個來源：${id}，可以用 ${sources.map((source) => source.id).join('、')}`);
  return sourceOf(id);
}

function apiTrackSource(request) {
  if (request.source === undefined) throw new RequestError(400, '要給 source');
  const source = apiSource(request.source);
  if (typeof request.id !== 'string' || !request.id) throw new RequestError(400, 'id 要是字串');
  return source;
}

async function lookupTrack(request) {
  if (request.query !== undefined) {
    if (request.id !== undefined) throw new RequestError(400, 'query 和 id 只能給一個');
    return (await findTracks(request))[0];
  }
  if (request.source === undefined) throw new RequestError(400, '要給 query，或是 source 加 id');
  const source = apiTrackSource(request);
  try {
    return await source.track(request.id);
  } catch (e) {
    if (e instanceof TrackNotFound) throw new RequestError(404, e.message);
    throw new RequestError(502, `${source.name} 查歌失敗：${e.message}`);
  }
}

async function enqueueRequest(request) {
  if (request.insert !== undefined && typeof request.insert !== 'boolean') throw new RequestError(400, 'insert 要是 true 或 false');
  const found = await lookupTrack(request);
  const track = request.query === undefined ? found : { ...found, requestedAs: request.query };
  if (request.insert) insert(track, false);
  else pick(track, false);
}

async function findTracks(request) {
  const source = apiSource(request.source);
  if (typeof request.query !== 'string' || !request.query.trim()) throw new RequestError(400, '要給 query');
  let found;
  try {
    found = await searchSongs(source, request.query);
  } catch (e) {
    throw new RequestError(502, `${source.name} 搜尋失敗：${e.message}`);
  }
  if (!found.length) throw new RequestError(404, `${source.name} 搜不到「${request.query.trim()}」`);
  return found;
}

function queueIndexOf(request) {
  if (request.query === undefined) {
    apiTrackSource(request);
    return queue.findIndex((track) => track.source === request.source && track.id === request.id);
  }
  if (request.source !== undefined || request.id !== undefined) throw new RequestError(400, 'query 和 source、id 只能擇一');
  if (typeof request.query !== 'string' || !request.query.trim()) throw new RequestError(400, '要給 query');
  return queue.findIndex((track) => track.requestedAs === request.query);
}

function dequeueRequest(request) {
  const i = queueIndexOf(request);
  if (i < 0) throw new RequestError(404, '待播清單裡沒有這首');
  queue.splice(i, 1);
  renderNowPlaying();
  renderPanel();
  if (!nextTrack()) refreshAutoplay();
}

function togglePopover(id) {
  const opening = $(id).hidden;
  for (const other of POPOVERS) $(other).hidden = true;
  $(id).hidden = !opening;
  return opening;
}

$('fxToggle').addEventListener('click', () => {
  togglePopover('fx');
  renderEffects();
});
$('styleToggle').addEventListener('click', () => {
  if (togglePopover('stylePanel')) lyricStyle.opened();
});
$('settingsToggle').addEventListener('click', () => {
  if (!togglePopover('settings')) return;
  languageOpen = false;
  renderAccount();
});
$('settings').addEventListener('click', (e) => {
  if (e.target.closest('#languageCurrent')) {
    languageOpen = !languageOpen;
    renderLanguagePicker();
    return;
  }
  const option = e.target.closest('[data-language]');
  if (option) {
    languageOpen = false;
    if (option.dataset.language === language.setting()) renderLanguagePicker();
    else language.choose(option.dataset.language);
    return;
  }
  const target = e.target.closest('[data-act]');
  if (!target) return;
  const source = sourceOf(target.dataset.source);
  switch (target.dataset.act) {
    case 'login':
      accounts.login(source);
      break;
    case 'logout':
      accounts.logout(source);
      break;
    default:
      throw new Error(`不認得的帳號操作：${target.dataset.act}`);
  }
});
language.onChange(renderLanguage);
document.fonts.addEventListener('loadingdone', () => {
  band.measureAll();
  frame();
});
$('fx').addEventListener('click', (e) => {
  const button = e.target.closest('.seg');
  if (!button) return;
  if (button.dataset.eq) effects.eq = button.dataset.eq;
  if (button.dataset.reverb) effects.reverb = button.dataset.reverb;
  renderEffects();
  applyEffects();
});
document.addEventListener('pointerdown', (e) => {
  if (!$('fx').hidden && !e.target.closest('#fx, #fxToggle')) $('fx').hidden = true;
  if (!$('stylePanel').hidden && !e.target.closest('#stylePanel, #styleToggle')) $('stylePanel').hidden = true;
  if (!$('settings').hidden && !e.target.closest('#settings, #settingsToggle')) $('settings').hidden = true;
  if (!$('menu').hidden && !e.target.closest('#menu, [data-act=more]')) closeMenu();
});
$('dlToggle').addEventListener('click', async () => {
  desktop = await call('desktopLyrics.set', desktop.visible && desktop.locked ? { locked: false } : { visible: !desktop.visible });
  renderDesktopKeys();
});
$('skip').addEventListener('click', playNext);
$('keyDown').addEventListener('click', () => changeKeyShift(-1));
$('keyUp').addEventListener('click', () => changeKeyShift(1));
$('seek').addEventListener('input', () => {
  seeking = true;
  setRange($('seek'), Number($('seek').value), SEEK_STEPS);
});
$('seek').addEventListener('change', () => {
  playback.seek((playback.duration() * Number($('seek').value)) / SEEK_STEPS);
  seeking = false;
  shown.delete('seek');
  frame();
});
function applyVolume(volume) {
  $('vol').value = String(volume);
  setRange($('vol'), volume, 100);
  $('volNum').textContent = volume;
  playback.setVolume(volume);
  karaoke.setVolume(volume / 100);
  publishPlayback();
}

function applyVocal(value) {
  $('vocal').value = String(value);
  setRange($('vocal'), value, 100);
  $('vocalNum').textContent = value;
  karaoke.setVocal(value / 100);
}

$('vol').addEventListener('input', () => applyVolume(Number($('vol').value)));
$('vol').addEventListener('change', () => saveText(VOLUME_KEY, $('vol').value));
$('vocal').addEventListener('input', () => applyVocal(Number($('vocal').value)));
$('vocal').addEventListener('change', () => saveText(VOCAL_KEY, $('vocal').value));
$('repeatToggle').addEventListener('click', () => {
  repeatMode = REPEAT_MODES[(REPEAT_MODES.indexOf(repeatMode) + 1) % REPEAT_MODES.length];
  saveText(REPEAT_KEY, repeatMode);
  if (repeatMode === 'all' && upNext?.auto) upNext = null;
  renderNowPlaying();
  renderPanel();
  refreshAutoplay();
});
$('autoplayToggle').addEventListener('click', () => {
  autoplay = !autoplay;
  saveFlag(AUTOPLAY_KEY, autoplay);
  if (!autoplay && upNext?.auto) upNext = null;
  renderNowPlaying();
  renderPanel();
  refreshAutoplay();
});
$('kToggle').addEventListener('click', () => {
  if (karaokeOn) setKaraoke(false);
  else ask(t('karaoke.warning'), t('karaoke.turnOn'), () => setKaraoke(true));
});

function setKaraoke(on) {
  karaokeOn = on;
  saveFlag(KARAOKE_KEY, karaokeOn);
  karaoke.setSong(current?.source === 'ytm' ? current.id : null);
  setEmbedMuted(false);
  if (karaokeOn && current?.source === 'ytm') {
    playback.pause();
    holding = true;
    notePhase(current.id, karaokeStatus.get(current.id));
    prepareKaraoke(current, playback.time(), true);
  } else if (holding) {
    holding = false;
    playback.play();
  }
  renderKaraoke();
  renderNowPlaying();
  frame();
}
document.addEventListener('keydown', (e) => {
  if (e.ctrlKey && e.key.toLowerCase() === 'f') {
    e.preventDefault();
    setView('pick');
    $('q').focus();
    $('q').select();
    return;
  }
  if (e.key === 'Escape') {
    closeMenu();
    $('confirm').hidden = true;
    for (const id of POPOVERS) $(id).hidden = true;
  }
  if (e.key === ' ' && e.target.tagName !== 'INPUT' && current) {
    e.preventDefault();
    $('toggle').click();
  }
});
new ResizeObserver(() => {
  band.measureAll();
  frame();
}).observe($('band'));

async function openLink(kind, request) {
  await playback.ready;
  try {
    switch (kind) {
      case 'queue':
        await enqueueRequest(request);
        break;
      case 'command':
        await runCommand(request);
        break;
      case 'dequeue':
        dequeueRequest(request);
        break;
      default:
        throw new Error(`不認得的連結：${kind}`);
    }
  } catch (e) {
    if (kind !== 'queue' && e instanceof RequestError && (e.status === 404 || e.status === 409)) {
      console.info('連結要的狀態已經是這樣了', kind, request, e.message);
      return;
    }
    console.error('連結失敗', kind, request, e);
    toast(t(kind === 'queue' ? 'toast.linkFailed' : 'toast.commandFailed'));
  }
}

let links = Promise.resolve();
on('link', ({ kind, request }) => {
  links = links.then(() => openLink(kind, request));
});

on('karaoke', async (status) => {
  karaokeStatus.set(status.id, status);
  notePhase(status.id, status);
  if (status.state === 'error') console.error('伴唱失敗', status.message);
  await karaoke.onStatus(status);
  checkHold();
  renderNowPlaying();
  frame();
});
karaoke.onLoaded(checkHold);

on('desktopLyricsState', (state) => {
  desktop = state;
  renderDesktopKeys();
  lyricStyle.sync(state);
});
on('control', ({ action }) => {
  switch (action) {
    case 'restart':
      restart();
      break;
    case 'toggle':
      if (current) playback.toggle();
      break;
    case 'play':
      if (current) playNow();
      break;
    case 'pause':
      if (current) pauseNow();
      break;
    case 'skip':
      if (current) playNext();
      break;
    case 'earlier':
      shiftLyrics(OFFSET_STEP_MS);
      break;
    case 'later':
      shiftLyrics(-OFFSET_STEP_MS);
      break;
    default:
      throw new Error(`原生視窗送來不認得的指令：${action}`);
  }
});

on('update', ({ state, version, message }) => {
  switch (state) {
    case 'ready':
      toast(t('update.ready', { version }));
      break;
    case 'rejected':
      toast(t('update.rejected', { version }));
      break;
    case 'error':
      console.error(message);
      toast(t('update.failed'));
      break;
    default:
      throw new Error(`看不懂的更新狀態：${state}`);
  }
});

function applyWindow(state) {
  $('player').toggleAttribute('data-window-hidden', !state.shown);
}

on('window', applyWindow);

const started = await call('ready');
desktop = started.desktopLyrics;
applyWindow(started.window);
await loadLibrary();
renderDesktopKeys();
lyricStyle.start(desktop);
renderKaraoke();
renderKeyShift();
$('vol').value = savedText(VOLUME_KEY) ?? $('vol').value;
$('volNum').textContent = $('vol').value;
setRange($('vol'), Number($('vol').value), 100);
karaoke.setVolume(Number($('vol').value) / 100);
applyVocal(Number(savedText(VOCAL_KEY) ?? $('vocal').value));
document.querySelectorAll('#srcTabs .src-tab').forEach((el) => el.classList.toggle('on', el.dataset.source === searchSource.id));
const queueOpen = savedFlag(QUEUE_KEY, true);
$('app').classList.toggle('queue-closed', !queueOpen);
$('queueToggle').classList.toggle('on', queueOpen);
renderAccount();
renderNowPlaying();
renderPanel();
announceSong();
frame();
renderEffects();
accounts.onChange((sourceId) => {
  if (openAccount?.source === sourceId && !accounts.signedIn(sourceId)) {
    openAccount = null;
    if (view === 'playlist' && !openPlaylist) view = 'pick';
  }
  renderAccount();
  renderPanel();
  renderNowPlaying();
  renderKeyShift();
  loadAccountLists(sourceOf(sourceId));
  if (sourceId === 'ytm') loadRecommended();
});
accounts.start();
loadCharts();
loadRecommended();
await playback.ready;
playback.setVolume(Number($('vol').value));
if (effects.eq !== NO_EFFECTS.eq || effects.reverb !== NO_EFFECTS.reverb) applyEffects();
