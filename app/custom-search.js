'use strict'
function validEngine(engine) {
  if (!engine || typeof engine !== 'object') return false
  const { shortcut, name, url } = engine
  if (typeof shortcut !== 'string' || !/^[a-z][a-z0-9_-]{0,19}$/i.test(shortcut)) return false
  if (typeof name !== 'string' || !name.trim() || name.length > 80) return false
  if (typeof url !== 'string' || url.length > 2048 || !url.includes('%s')) return false
  try {
    const a = new URL(url.replaceAll('%s', 'sample'))
    const b = new URL(url.replaceAll('%s', 'different'))
    return a.protocol === 'https:' && !a.username && !a.password && a.origin === b.origin
  } catch { return false }
}
function shortcutSearch(input, engines) {
  const match = String(input || '').match(/^([a-z][a-z0-9_-]{0,19})\s+(.+)$/i)
  if (!match || !Array.isArray(engines)) return null
  const engine = engines.find(e => validEngine(e) && e.shortcut.toLowerCase() === match[1].toLowerCase())
  return engine ? engine.url.replaceAll('%s', encodeURIComponent(match[2])) : null
}
module.exports = { validEngine, shortcutSearch }
