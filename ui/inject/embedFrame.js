const isEmbed = (origin) => location.origin === origin && location.pathname.startsWith('/embed/');
if (window.top === window || !(isEmbed('https://www.youtube.com') || isEmbed('https://open.spotify.com'))) return;

const graphs = new WeakMap();
let graph = null;
let media = null;
let effects = null;
let level = 1;
let muted = false;
let ownsVolume = false;
let semitones = 0;

function apply() {
  const volume = muted ? 0 : level;
  if (graph && graph.media === media) {
    if (effects) graph.chain.apply(effects);
    graph.chain.setPitch(semitones);
    graph.gain.gain.value = volume;
  } else if (media && ownsVolume) {
    media.volume = volume;
  }
}

const createSource = AudioContext.prototype.createMediaElementSource;
let ownSource = false;

function adopt(ctx, element, node) {
  const gain = ctx.createGain();
  const chain = createEffectsGraph(ctx, gain, pitchWorkletUrl);
  AudioNode.prototype.connect.call(node, chain.input);
  node.connect = (destination, ...rest) => {
    gain.connect(destination, ...rest);
    return destination;
  };
  graphs.set(element, { media: element, ctx, chain, gain });
}

AudioContext.prototype.createMediaElementSource = function (element) {
  const node = createSource.call(this, element);
  if (!ownSource) adopt(this, element, node);
  return node;
};

function connect(element) {
  media = element;
  if (!graphs.has(element) && (element.currentSrc || element.src).startsWith('blob:')) {
    const ctx = new AudioContext({ latencyHint: 'playback' });
    const gain = ctx.createGain();
    gain.connect(ctx.destination);
    const chain = createEffectsGraph(ctx, gain, pitchWorkletUrl);
    ownSource = true;
    try {
      ctx.createMediaElementSource(element).connect(chain.input);
    } finally {
      ownSource = false;
    }
    graphs.set(element, { media: element, ctx, chain, gain });
  }
  graph = graphs.get(element) ?? graph;
  apply();
}

const play = HTMLMediaElement.prototype.play;
HTMLMediaElement.prototype.play = function (...args) {
  connect(this);
  return play.apply(this, args);
};

window.addEventListener('message', (e) => {
  if (e.source !== window.parent) return;
  switch (e.data?.kind) {
    case 'app-effects':
      effects = e.data.settings;
      break;
    case 'app-volume':
      level = e.data.level;
      ownsVolume = true;
      break;
    case 'app-mute':
      muted = e.data.muted;
      ownsVolume = true;
      break;
    case 'app-pitch':
      semitones = e.data.semitones;
      break;
    default:
      return;
  }
  apply();
});
