const DOWNLOADS = {
  '/latest/VoxSetup.exe': { key: 'latest/VoxSetup.exe', name: 'VoxSetup.exe', type: 'application/octet-stream' },
  '/latest/Vox.dmg': { key: 'latest/Vox.dmg', name: 'Vox.dmg', type: 'application/x-apple-diskimage' },
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const download = DOWNLOADS[url.pathname];
    if (!download) return env.ASSETS.fetch(request);
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return new Response(null, { status: 405, headers: { allow: 'GET, HEAD' } });
    }
    const object = request.method === 'HEAD' ? await env.RELEASES.head(download.key) : await env.RELEASES.get(download.key);
    if (!object) return new Response(null, { status: 503 });
    return new Response(request.method === 'HEAD' ? null : object.body, {
      headers: {
        'content-type': download.type,
        'content-length': String(object.size),
        'content-disposition': `attachment; filename="${download.name}"`,
        'cache-control': 'no-store',
      },
    });
  },
};
