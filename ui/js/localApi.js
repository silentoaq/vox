import { call, on } from './host.js';

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function parseRequest(body) {
  let request;
  try {
    request = JSON.parse(body);
  } catch {
    throw new ApiError(400, '內容要是 JSON');
  }
  if (typeof request !== 'object' || request === null || Array.isArray(request)) throw new ApiError(400, '內容要是 JSON 物件');
  return request;
}

export function createLocalApi({ ready, command, enqueue }) {
  const sent = new Map();

  on('api', async ({ id, kind, body }) => {
    let status = 200;
    let result;
    try {
      await ready;
      const request = parseRequest(body);
      switch (kind) {
        case 'command':
          result = await command(request);
          break;
        case 'enqueue':
          result = await enqueue(request);
          break;
        default:
          throw new Error(`本機介面不認得的請求：${kind}`);
      }
    } catch (e) {
      if (!(e instanceof ApiError)) console.error('本機介面處理失敗', kind, e);
      status = e instanceof ApiError ? e.status : 500;
      result = { error: e.message };
    }
    call('api.reply', { id, status, body: JSON.stringify(result) });
  });

  return {
    publish(event, data) {
      const text = JSON.stringify(data);
      if (sent.get(event) === text) return;
      sent.set(event, text);
      call('api.publish', { event, data: text });
    }
  };
}
