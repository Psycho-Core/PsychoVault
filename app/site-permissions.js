'use strict'
// Origin-scoped decisions. No wildcard, domain suffix or third-party inheritance.
function originOf(value) {
  try {
    const u = new URL(String(value || ''))
    return (u.protocol === 'http:' || u.protocol === 'https:') && !u.username && !u.password ? u.origin : ''
  } catch { return '' }
}
function sitePermission(settings, rawOrigin, permission, details) {
  const deny = String(settings && settings.denyPerms || '').split(',').map(x => x.trim().toLowerCase())
  if (!['media', 'geolocation', 'notifications'].includes(permission)) return !deny.includes(String(permission).toLowerCase())
  const origin = originOf(rawOrigin)
  if (!origin) return false
  const values = settings && settings.sitePermissions && settings.sitePermissions[origin]
  let keys
  if (permission === 'media') {
    const types = (details && details.mediaTypes) || (details && details.mediaType ? [details.mediaType] : [])
    if (!Array.isArray(types) || !types.length) return !deny.includes('media')
    keys = types.map(x => x === 'video' ? 'camera' : x === 'audio' ? 'microphone' : '')
    if (keys.includes('')) return false
  } else if (permission === 'geolocation' || permission === 'notifications') keys = [permission]
  else return !deny.includes(String(permission).toLowerCase())
  return keys.every(key => {
    const rule = values && values[key]
    if (rule === 'allow') return true
    if (rule === 'deny') return false
    return !deny.includes(permission)
  })
}
module.exports = { originOf, sitePermission }
