// Browser data logic. The Chromium host must own one instance and call recordVisit
// only for a real committed regular-profile navigation. Never expose storage to pages.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoLibrary = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  const copy = value => JSON.parse(JSON.stringify(value));
  function webUrl(raw) {
    if (typeof raw !== 'string' || raw.length > 8192) throw new TypeError('Invalid URL');
    let url;
    try { url = new URL(raw); } catch { throw new TypeError('Invalid URL'); }
    if (!['http:', 'https:'].includes(url.protocol)) throw new TypeError('Only HTTP and HTTPS URLs can be stored');
    url.username = ''; url.password = '';
    return url.href;
  }
  function entry(input, now) {
    if (!input || typeof input !== 'object') throw new TypeError('Invalid entry');
    const url = webUrl(input.url);
    return {url, title: typeof input.title === 'string' && input.title ? input.title.slice(0, 500) : url, at: now()};
  }
  function create(storage, options = {}) {
    if (!storage || typeof storage.read !== 'function' || typeof storage.write !== 'function') throw new TypeError('Persistent storage is required');
    const now = options.now || Date.now;
    let state;
    let queue = Promise.resolve();
    function validate(saved) {
      if (saved === null) return {version: 1, bookmarks: [], history: []};
      if (!saved || saved.version !== 1 || !Array.isArray(saved.bookmarks) || !Array.isArray(saved.history)) throw new Error('Unsupported or corrupt library data; refusing to overwrite it');
      for (const item of [...saved.bookmarks, ...saved.history]) {
        if (!item || typeof item.url !== 'string' || typeof item.title !== 'string' || !Number.isFinite(item.at)) throw new Error('Corrupt library data; refusing to overwrite it');
        if (webUrl(item.url) !== item.url) throw new Error('Unsafe URL in library data; refusing to overwrite it');
      }
      return copy(saved);
    }
    async function load() { state = validate(await storage.read()); }
    function run(work) {
      const result = queue.then(async () => { await load(); return work(); });
      queue = result.catch(() => {});
      return result;
    }
    async function change(fn) {
      if (typeof storage.update === 'function') {
        let result;
        const saved = await storage.update(raw => {
          const next = validate(raw);
          result = fn(next);
          return copy(next);
        });
        state = saved === null ? state : copy(saved);
        return copy(result === undefined ? state : result);
      }
      const next = copy(state);
      const result = fn(next);
      await storage.write(copy(next));
      state = next;
      return copy(result === undefined ? next : result);
    }
    return Object.freeze({
      listBookmarks: () => run(() => copy(state.bookmarks)),
      addBookmark: data => run(() => change(next => {
        const item = entry(data, now);
        if (!next.bookmarks.some(b => b.url === item.url)) next.bookmarks.unshift(item);
        return next.bookmarks;
      })),
      removeBookmark: raw => run(() => change(next => {
        const url = webUrl(raw);
        next.bookmarks = next.bookmarks.filter(b => b.url !== url);
        return next.bookmarks;
      })),
      isBookmarked: raw => run(() => state.bookmarks.some(b => b.url === webUrl(raw))),
      listHistory: () => run(() => copy(state.history.slice(0, 300))),
      clearHistory: () => run(() => change(next => { next.history = []; return []; })),
      recordVisit: (data, context) => run(async () => {
        // An absent profile or remember value is not enough to authorize recording.
        if (!context || context.profile !== 'regular' || context.remember !== true) return false;
        await change(next => {
          const item = entry(data, now);
          next.history = [item, ...next.history.filter(h => h.url !== item.url)].slice(0, 2000);
        });
        return true;
      }),
      search: query => run(() => {
        if (typeof query !== 'string' || !query) return [];
        const q = query.toLowerCase();
        return [...state.bookmarks.map(b => ({...b, type:'bookmark'})), ...state.history.map(h => ({...h, type:'history'}))]
          .filter(item => item.url.toLowerCase().includes(q) || item.title.toLowerCase().includes(q)).slice(0, 8);
      })
    });
  }
  return Object.freeze({create, webUrl});
});
