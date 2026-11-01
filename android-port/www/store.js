(function (root) {
  'use strict';
  const KEY = 'psychovault.android.data.v1';
  const engines = ['google', 'duckduckgo', 'bing'];
  const themes = ['navy', 'graphite', 'forest', 'plum', 'ocean'];
  function empty() { return { version: 1, settings: { search: 'google', theme: 'navy', restoreSession: false, blockAds: true, blockUseBundled: true, blockCustomDomains: '', blockAllowSites: '' }, bookmarks: [], history: [], session: { tabs: [], active: 0 } }; }
  function validUrl(value) {
    try { const u = new URL(value); return ['https:', 'http:'].includes(u.protocol) && u.hostname && !u.username && !u.password ? u.href : null; } catch { return null; }
  }
  function entry(raw) {
    if (!raw || typeof raw !== 'object') return null;
    const url = validUrl(raw.url);
    return url ? { url, title: typeof raw.title === 'string' ? raw.title.slice(0, 512) : '', time: Number.isFinite(raw.time) ? raw.time : 0 } : null;
  }
  function cleanSession(raw) {
    if (!raw || !Array.isArray(raw.tabs)) return { tabs: [], active: 0 };
    const tabs = []; let active = 0;
    for (let i = 0; i < raw.tabs.length && tabs.length < 50; i++) {
      const url = validUrl(raw.tabs[i]);
      if (!url) continue;
      if (i === raw.active) active = tabs.length;
      tabs.push(url);
    }
    return { tabs, active };
  }
  function open(storage) {
    let data; let writable = true;
    try {
      const raw = storage.getItem(KEY);
      if (raw === null) data = empty();
      else {
        const parsed = JSON.parse(raw);
        if (!parsed || parsed.version !== 1 || !Array.isArray(parsed.bookmarks) || !Array.isArray(parsed.history) || !parsed.settings || typeof parsed.settings !== 'object') throw new Error('Invalid browser data');
        data = { version: 1, settings: { search: engines.includes(parsed.settings.search) ? parsed.settings.search : 'google', theme: themes.includes(parsed.settings.theme) ? parsed.settings.theme : 'navy', restoreSession: parsed.settings.restoreSession === true, blockAds: parsed.settings.blockAds !== false, blockUseBundled: parsed.settings.blockUseBundled !== false, blockCustomDomains: typeof parsed.settings.blockCustomDomains === 'string' ? parsed.settings.blockCustomDomains.slice(0, 50000) : '', blockAllowSites: typeof parsed.settings.blockAllowSites === 'string' ? parsed.settings.blockAllowSites.slice(0, 50000) : '' }, bookmarks: parsed.bookmarks.map(entry).filter(Boolean).slice(0, 1000), history: parsed.history.map(entry).filter(Boolean).slice(0, 1000), session: parsed.settings.restoreSession === true ? cleanSession(parsed.session) : { tabs: [], active: 0 } };
      }
    } catch { data = empty(); writable = false; }
    function save(next) {
      if (!writable) throw new Error('Browser data could not be read; changes are disabled to avoid overwriting it.');
      storage.setItem(KEY, JSON.stringify(next));
      data = next;
    }
    return {
      read: () => JSON.parse(JSON.stringify(data)),
      writable: () => writable,
      settings(patch) { const next = { ...data, settings: { ...data.settings } }; if (engines.includes(patch.search)) next.settings.search = patch.search; if (themes.includes(patch.theme)) next.settings.theme = patch.theme; for (const key of ['blockAds', 'blockUseBundled']) if (typeof patch[key] === 'boolean') next.settings[key] = patch[key]; for (const key of ['blockCustomDomains', 'blockAllowSites']) if (typeof patch[key] === 'string') next.settings[key] = patch[key].slice(0, 50000); if (typeof patch.restoreSession === 'boolean') { next.settings.restoreSession = patch.restoreSession; if (!patch.restoreSession) next.session = { tabs: [], active: 0 }; } save(next); },
      saveSession(session) { if (data.settings.restoreSession) save({ ...data, session: cleanSession(session) }); },
      toggleBookmark(url, title) { const e = entry({url, title, time: Date.now()}); if (!e) throw new Error('Only ordinary web addresses can be bookmarked.'); const found = data.bookmarks.some(b => b.url === e.url); save({ ...data, bookmarks: found ? data.bookmarks.filter(b => b.url !== e.url) : [e, ...data.bookmarks].slice(0, 1000) }); return !found; },
      visit(url, title) { const e = entry({url, title, time: Date.now()}); if (!e) return; save({ ...data, history: [e, ...data.history.filter(h => h.url !== e.url)].slice(0, 1000) }); },
      removeBookmark(url) { const normalized = validUrl(url); if (!normalized) return; save({ ...data, bookmarks: data.bookmarks.filter(item => item.url !== normalized) }); },
      removeHistory(url) { const normalized = validUrl(url); if (!normalized) return; save({ ...data, history: data.history.filter(item => item.url !== normalized) }); },
      clearHistory() { save({ ...data, history: [] }); }
    };
  }
  const api = { open, validUrl, engines, themes };
  if (typeof module !== 'undefined') module.exports = api; else root.PsychoStore = api;
})(typeof window === 'undefined' ? globalThis : window);
