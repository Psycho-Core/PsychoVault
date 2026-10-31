'use strict'
function cleanUrl(raw) {
  try { const u = new URL(String(raw)); if (!['https:', 'http:'].includes(u.protocol)) return ''; u.username = ''; u.password = ''; return u.href } catch { return '' }
}
function captureWindows(windows) {
  return [...windows].filter(w => !w.private).map(w => {
    const tabs = (w.order || []).map(id => w.tabs.get(id)).filter(Boolean).map(t => ({ url: cleanUrl(t.discarded ? t.discardUrl : t.url), title: String(t.title || '').slice(0, 250), pinned: !!t.pinned, muted: !!t.muted, container: typeof t.container === 'string' ? t.container.slice(0, 100) : null })).filter(t => t.url)
    return { tabs }
  }).filter(w => w.tabs.length)
}
function selectWindows(snapshot, selections) {
  if (!Array.isArray(snapshot) || !Array.isArray(selections)) return []
  return snapshot.map((win, wi) => {
    const picked = new Set(Array.isArray(selections[wi]) ? selections[wi].filter(Number.isInteger) : [])
    return { tabs: (Array.isArray(win.tabs) ? win.tabs : []).filter((_, i) => picked.has(i)).map(t => ({ ...t, url: cleanUrl(t.url) })).filter(t => t.url) }
  }).filter(w => w.tabs.length)
}
module.exports = { cleanUrl, captureWindows, selectWindows }
