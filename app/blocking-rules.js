'use strict'
// Domain lists, not general-purpose uBlock/ABP filter syntax.
function hostname(value) {
  try {
    const text = String(value || '').trim().toLowerCase()
    if (!text || /[\s@*]/.test(text)) return ''
    const url = new URL(text.includes('://') ? text : 'https://' + text)
    if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password) return ''
    return url.hostname.replace(/\.$/, '')
  } catch { return '' }
}
function parseHosts(value) {
  return [...new Set(String(value || '').split(/[\n,]+/).map(hostname).filter(Boolean))]
}
function siteExcluded(value, pageUrl) {
  const page = hostname(pageUrl)
  return !!page && parseHosts(value).includes(page)
}
function isDomainBlocked(domains, requestUrl) {
  const h = hostname(requestUrl)
  if (!h) return false
  const parts = h.split('.')
  for (let i = 0; i < parts.length - 1; i++) if (domains.has(parts.slice(i).join('.'))) return true
  return domains.has(h)
}
module.exports = { hostname, parseHosts, siteExcluded, isDomainBlocked }
