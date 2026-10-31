'use strict'
// Only an extension that controls a fixed proxy may answer challenges for that exact
// proxy endpoint. Never pass website authentication challenges to an extension here.
function matchesProxyChallenge(value, authInfo) {
  if (!authInfo || authInfo.isProxy !== true || !value || value.mode !== 'fixed_servers') return false
  const rules = value.rules || {}
  const servers = [rules.singleProxy, rules.proxyForHttp, rules.proxyForHttps, rules.proxyForFtp, rules.fallbackProxy].filter(Boolean)
  const host = String(authInfo.host || '').toLowerCase().replace(/\.$/, '')
  const port = Number(authInfo.port)
  return !!host && servers.some(s => String(s.host || '').toLowerCase().replace(/\.$/, '') === host && Number(s.port) === port)
}
// A bridge that forwards arbitrary browsing URLs requires the extension to have asked
// for the web request auth permission and access to both HTTP and HTTPS hosts.
function allowsProxyAuth(manifest) {
  const permissions = manifest && manifest.permissions || []
  const hosts = manifest && manifest.host_permissions || []
  if (!permissions.includes('webRequest') || !permissions.includes('webRequestAuthProvider')) return false
  const all = hosts.includes('<all_urls>') || hosts.includes('*://*/*')
  const http = hosts.includes('http://*/') || hosts.includes('http://*/*')
  const https = hosts.includes('https://*/') || hosts.includes('https://*/*')
  return all || (http && https)
}
module.exports = { matchesProxyChallenge, allowsProxyAuth }
