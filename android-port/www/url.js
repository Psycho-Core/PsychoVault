(function (root) {
  function destination(value, engine) {
    const text = String(value || '').trim();
    if (!text) return null;
    if (/^https?:\/\//i.test(text)) {
      try { const u = new URL(text); return u.hostname ? u.href : null; } catch { return null; }
    }
    if (!/\s/.test(text) && (/^[\w-]+(\.[\w-]+)+(?::\d+)?(?:[/?#].*)?$/.test(text) || /^localhost(?::\d+)?(?:[/?#].*)?$/.test(text))) {
      try { return new URL('https://' + text).href; } catch { return null; }
    }
    // Other schemes are not allowed, including javascript:, file: and intent:.
    if (/^[a-z][a-z\d+.-]*:/i.test(text)) return null;
    const base = { google: 'https://www.google.com/search?q=', duckduckgo: 'https://duckduckgo.com/?q=', bing: 'https://www.bing.com/search?q=' }[engine] || 'https://www.google.com/search?q=';
    return base + encodeURIComponent(text);
  }
  if (typeof module !== 'undefined') module.exports = { destination };
  else root.PsychoUrl = { destination };
})(typeof window === 'undefined' ? globalThis : window);
