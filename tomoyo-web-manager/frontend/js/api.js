/* Thin fetch() wrapper: JSON in, JSON out, throws Error(message) on
 * any non-2xx response so callers can just try/catch. */
window.TWM = (function () {
  async function api(path, opts) {
    opts = opts || {};
    const fetchOpts = {
      method: opts.method || 'GET',
      credentials: 'same-origin',
      headers: {},
    };
    if (opts.body !== undefined) {
      fetchOpts.headers['Content-Type'] = 'application/json';
      fetchOpts.body = JSON.stringify(opts.body);
    }

    const res = await fetch(path, fetchOpts);
    const contentType = res.headers.get('content-type') || '';
    const isJson = contentType.indexOf('application/json') !== -1;
    const data = isJson ? await res.json().catch(() => ({})) : await res.text();

    if (!res.ok) {
      const message = isJson && data && data.error ? data.error : ('Erreur HTTP ' + res.status);
      const err = new Error(message);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  return { api, escapeHtml };
})();
