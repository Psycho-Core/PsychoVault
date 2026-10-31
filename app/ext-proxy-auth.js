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
module.exports = { matchesProxyChallenge }
