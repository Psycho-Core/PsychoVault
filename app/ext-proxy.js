'use strict'
// Translation between the chrome.proxy API that extensions speak and the session.setProxy
// config that Electron understands. Kept free of Electron and of the DOM so it can be
// unit-tested directly (tools/ext-proxy.test.cjs) - this is the part that decides whether
// a VPN extension's tunnel is applied to the right traffic, so it is tested rather than
// trusted.
//
// chrome.proxy.settings.set() takes { value, scope } where value is one of:
//   { mode: 'system' }
//   { mode: 'direct' }
//   { mode: 'auto_detect' }
//   { mode: 'pac_script', pacScript: { data } | { url } }
//   { mode: 'fixed_servers', rules: { singleProxy | proxyForHttp/ForHttps/ForFtp/fallbackProxy,
//                                    bypassList } }
// Electron's session.setProxy() takes a ProxyConfig with mode, proxyRules, proxyBypassRules
// and pacScript. The names differ; the semantics do not.

const ELECTRON_MODES = new Set(['direct', 'auto_detect', 'pac_script', 'fixed_servers', 'system'])

function proxyServerToRule(server) {
  if (!server || typeof server !== 'object') return ''
  const host = String(server.host || '').trim()
  if (!host) throw new Error('proxy server has no usable host: ' + JSON.stringify(server))
  const port = Number(server.port)
  if (!Number.isFinite(port) || port <= 0 || port > 65535) {
    throw new Error('proxy server has no usable port: ' + JSON.stringify(server))
  }
  // Electron infers the scheme from the prefix. Chrome's scheme defaults to http.
  const scheme = String(server.scheme || 'http').toLowerCase()
  const prefix = scheme === 'socks4' || scheme === 'socks5' || scheme === 'socks' ? 'socks' : scheme
  return prefix + '://' + host + ':' + port
}

// chrome.proxy ProxyConfig -> Electron ProxyConfig. Throws on anything malformed rather
// than silently falling back to DIRECT, because a VPN extension that thinks it is
// tunnelling while traffic goes direct is worse than one that reports a failure.
function toElectronProxy(value) {
  const v = value && typeof value === 'object' ? value : {}
  const mode = String(v.mode || 'system')

  if (!ELECTRON_MODES.has(mode)) throw new Error('unsupported proxy mode: ' + mode)

  if (mode === 'system' || mode === 'direct' || mode === 'auto_detect') return { mode }

  if (mode === 'pac_script') {
    const pac = v.pacScript || {}
    if (typeof pac.data === 'string' && pac.data.length) return { mode, pacScript: pac.data }
    if (typeof pac.url === 'string' && pac.url.length) return { mode, pacScript: pac.url }
    throw new Error('pac_script mode without pacScript.data or pacScript.url')
  }

  // fixed_servers
  const rules = v.rules && typeof v.rules === 'object' ? v.rules : {}
  const parts = []
  let specific = false
  if (rules.singleProxy) {
    // No scheme prefix: a bare rule is the one Chromium applies to every scheme. Prefixing it
    // with http= would route http through the tunnel and send https direct, which is the exact
    // failure a VPN must never have.
    parts.push(proxyServerToRule(rules.singleProxy))
    specific = true
  } else {
    if (rules.proxyForHttp) { parts.push('http=' + proxyServerToRule(rules.proxyForHttp)); specific = true }
    if (rules.proxyForHttps) { parts.push('https=' + proxyServerToRule(rules.proxyForHttps)); specific = true }
    if (rules.proxyForFtp) { parts.push('ftp=' + proxyServerToRule(rules.proxyForFtp)); specific = true }
    if (rules.fallbackProxy) { parts.push('socks=' + proxyServerToRule(rules.fallbackProxy)); specific = true }
  }
  if (!specific) throw new Error('fixed_servers mode without any proxy server')

  const bypassList = Array.isArray(rules.bypassList)
    ? rules.bypassList.map((x) => String(x)).filter(Boolean)
    : []

  return {
    mode,
    proxyRules: parts.join(';'),
    proxyBypassRules: bypassList.join(',')
  }
}

// What chrome.proxy.settings.get() must answer with once a setting has been applied.
// Extension code branches on levelOfControl, so it has to be exact: Proton VPN's own
// hasProxy() helper only accepts 'controlled_by_this_extension', or 'controllable_by_this_extension'
// together with a live connection check.
function getResult(hasSetting, value) {
  if (hasSetting) {
    return { value: value || { mode: 'system' }, levelOfControl: 'controlled_by_this_extension' }
  }
  return { value: { mode: 'system' }, levelOfControl: 'controllable_by_this_extension' }
}

// chrome.privacy.network.webRTCIPHandlingPolicy accepts Chrome's five policy names.
// Electron exposes the same set through webContents.setWebRTCIPHandlingPolicy.
const WEBRTC_POLICIES = new Set([
  'default',
  'default_public_interface_only',
  'default_public_and_private_interfaces',
  'disable_non_proxied_udp'
])

function toWebRTCPolicy(value) {
  const v = String(value || 'default')
  if (!WEBRTC_POLICIES.has(v)) throw new Error('unsupported webRTCIPHandlingPolicy: ' + v)
  return v
}

module.exports = { toElectronProxy, getResult, toWebRTCPolicy, proxyServerToRule, WEBRTC_POLICIES }
