const INSTALLER_PATH = '/latest/VoxSetup.exe';
const INSTALLER_KEY = 'latest/VoxSetup.exe';

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== INSTALLER_PATH) return env.ASSETS.fetch(request);
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return new Response(null, { status: 405, headers: { allow: 'GET, HEAD' } });
    }
    const object = request.method === 'HEAD' ? await env.RELEASES.head(INSTALLER_KEY) : await env.RELEASES.get(INSTALLER_KEY);
    if (!object) return new Response(null, { status: 503 });
    return new Response(request.method === 'HEAD' ? null : object.body, {
      headers: {
        'content-type': 'application/octet-stream',
        'content-length': String(object.size),
        'content-disposition': 'attachment; filename="VoxSetup.exe"',
        'cache-control': 'no-store',
      },
    });
  },
};
