// Trusted UI address resolution. Native code still validates the resolved URL and profile.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoNavigationResolver = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  const ENGINES = Object.freeze({
    duckduckgo: 'https://duckduckgo.com/?q=',
    brave: 'https://search.brave.com/search?q=',
    startpage: 'https://www.startpage.com/sp/search?query=',
    wikipedia: 'https://en.wikipedia.org/w/index.php?search='
  });
  const PAGE_ORIGIN = 'chrome://psychovault-pages';
  function resolve(input, settings = {}, isPrivate = false) {
    const text = typeof input === 'string' ? input.trim() : '';
    if (text.length > 8192) throw new TypeError('Address is too long');
    if (text === 'psychovault://settings') return PAGE_ORIGIN + '/settings.html?private=' + (isPrivate ? '1' : '0');
    if (text === 'psychovault://recovery') return PAGE_ORIGIN + '/recovery.html?private=' + (isPrivate ? '1' : '0');
    if (!text || text === 'psychovault://newtab' || text === 'about:newtab' || text === 'psychovault://home') {
      return PAGE_ORIGIN + '/newtab.html?private=' + (isPrivate ? '1' : '0');
    }
    // Never turn a user-typed privileged URL into a native navigation.
    if (/^[a-z][a-z0-9+.-]*:/i.test(text)) {
      let url;
      try { url = new URL(text); } catch { throw new TypeError('Invalid address'); }
      if (!['http:', 'https:'].includes(url.protocol)) throw new TypeError('This page is not available on Android yet');
      return url.href;
    }
    const bareHost = /^(?:localhost|\d{1,3}(?:\.\d{1,3}){3}|[^\s/:?#]+\.[a-z]{2,})(?::\d+)?(?:[/?#].*)?$/i.test(text);
    if (bareHost) {
      const url = new URL('https://' + text);
      if (!['http:', 'https:'].includes(url.protocol)) throw new TypeError('Invalid address');
      return url.href;
    }
    const custom = Array.isArray(settings.customSearchEngines) ? settings.customSearchEngines : [];
    const match = /^(\S+)\s+([\s\S]+)$/.exec(text);
    const shortcut = match && custom.find(e => e && e.shortcut === match[1]);
    if (shortcut && typeof shortcut.url === 'string' && shortcut.url.includes('%s')) {
      const target = shortcut.url.replaceAll('%s', encodeURIComponent(match[2]));
      const url = new URL(target);
      if (url.protocol !== 'https:') throw new TypeError('Custom search engines must use HTTPS');
      return url.href;
    }
    const selected = typeof settings.search === 'string' && settings.search.startsWith('custom:')
      ? custom.find(e => e && e.shortcut === settings.search.slice(7)) : null;
    const base = selected && typeof selected.url === 'string' ? selected.url : (ENGINES[settings.search] || ENGINES.duckduckgo);
    const target = base.includes('%s') ? base.replaceAll('%s', encodeURIComponent(text)) : base + encodeURIComponent(text);
    const url = new URL(target);
    if (url.protocol !== 'https:') throw new TypeError('Search engines must use HTTPS');
    return url.href;
  }
  function display(url, isPrivate = false) {
    const expected = PAGE_ORIGIN + '/newtab.html?private=' + (isPrivate ? '1' : '0');
    const settingsUrl = PAGE_ORIGIN + '/settings.html?private=' + (isPrivate ? '1' : '0');
    const recoveryUrl = PAGE_ORIGIN + '/recovery.html?private=' + (isPrivate ? '1' : '0');
    if (url === settingsUrl) return 'psychovault://settings';
    if (url === recoveryUrl) return 'psychovault://recovery';
    return url === expected ? 'psychovault://newtab' : String(url || '');
  }
  return Object.freeze({resolve, display, PAGE_ORIGIN});
});
