'use strict'
// Tests for the chrome.proxy -> Electron proxy translation in app/ext-proxy.js.
// This layer decides which traffic a VPN extension's tunnel covers, so the cases that
// matter are the failures: a config that cannot be applied must throw rather than fall
// back to DIRECT while the extension believes it is protected.
const test = require('node:test')
const assert = require('node:assert')
const { toElectronProxy, getResult, toWebRTCPolicy, proxyServerToRule, proxyOutcome } = require('../app/ext-proxy.js')

test('system, direct and auto_detect pass through as themselves', () => {
  assert.deepStrictEqual(toElectronProxy({ mode: 'system' }), { mode: 'system' })
  assert.deepStrictEqual(toElectronProxy({ mode: 'direct' }), { mode: 'direct' })
  assert.deepStrictEqual(toElectronProxy({ mode: 'auto_detect' }), { mode: 'auto_detect' })
  assert.deepStrictEqual(toElectronProxy({}), { mode: 'system' })
  assert.deepStrictEqual(toElectronProxy(undefined), { mode: 'system' })
})

test('PAC source becomes a data: URL, because Electron pacScript is a URL', () => {
  // Electron's docs describe pacScript as "the URL associated with the PAC file". Handing it
  // the source made Chromium fail to load the PAC and fall back to DIRECT - the silent leak,
  // where the extension showed "connected" and the public address never changed.
  const pac = 'function FindProxyForURL(url, host) { return "HTTPS proxy.example:4443"; }'
  const out = toElectronProxy({ mode: 'pac_script', pacScript: { data: pac } })
  assert.strictEqual(out.mode, 'pac_script')
  assert.match(out.pacScript, /^data:application\/x-ns-proxy-autoconfig;base64,/)
  assert.strictEqual(Buffer.from(out.pacScript.split(',')[1], 'base64').toString('utf8'), pac)
  assert.notStrictEqual(out.pacScript, pac, 'raw PAC source was handed over as if it were a URL')
})

test('a PAC URL is accepted as well as inline data', () => {
  const out = toElectronProxy({ mode: 'pac_script', pacScript: { url: 'https://example/p.pac' } })
  assert.strictEqual(out.pacScript, 'https://example/p.pac')
})

test('a pac_script with neither data nor url throws instead of going direct', () => {
  assert.throws(() => toElectronProxy({ mode: 'pac_script' }), /without pacScript/)
  assert.throws(() => toElectronProxy({ mode: 'pac_script', pacScript: {} }), /without pacScript/)
})

test('singleProxy becomes one http= rule plus the bypass list', () => {
  const out = toElectronProxy({
    mode: 'fixed_servers',
    rules: {
      singleProxy: { scheme: 'https', host: 'proxy.protonvpn.net', port: 4443 },
      bypassList: ['localhost', '127.0.0.1', '<local>']
    }
  })
  assert.strictEqual(out.mode, 'fixed_servers')
  assert.strictEqual(out.proxyRules, 'https://proxy.protonvpn.net:4443')
  assert.strictEqual(out.proxyBypassRules, 'localhost,127.0.0.1,<local>')
})

test('per-scheme rules are joined the way Electron expects', () => {
  const out = toElectronProxy({
    mode: 'fixed_servers',
    rules: {
      proxyForHttp: { host: 'a.example', port: 8080 },
      proxyForHttps: { host: 'b.example', port: 8443 },
      fallbackProxy: { scheme: 'socks5', host: 'c.example', port: 1080 }
    }
  })
  // The value keeps the scheme the proxy itself speaks: b.example is a plain-HTTP proxy
  // serving https traffic, so "https=http://..." is correct and "https=https://..." is not.
  assert.strictEqual(out.proxyRules, 'http=http://a.example:8080;https=http://b.example:8443;socks=socks://c.example:1080')
})

test('socks5 is expressed as the socks prefix Electron understands', () => {
  assert.strictEqual(proxyServerToRule({ scheme: 'socks5', host: 'h', port: 1080 }), 'socks://h:1080')
  assert.strictEqual(proxyServerToRule({ scheme: 'socks4', host: 'h', port: 1080 }), 'socks://h:1080')
})

test('a fixed_servers config with no server throws', () => {
  assert.throws(() => toElectronProxy({ mode: 'fixed_servers' }), /without any proxy server/)
  assert.throws(() => toElectronProxy({ mode: 'fixed_servers', rules: {} }), /without any proxy server/)
})

test('a server with a bad port throws rather than producing a broken rule', () => {
  assert.throws(() => proxyServerToRule({ host: 'h', port: 0 }), /no usable port/)
  assert.throws(() => proxyServerToRule({ host: 'h', port: 99999 }), /no usable port/)
  assert.throws(() => proxyServerToRule({ host: '', port: 4443 }), /no usable host/)
})

test('an unknown mode throws', () => {
  assert.throws(() => toElectronProxy({ mode: 'quantum' }), /unsupported proxy mode/)
})

test('settings.get reports controlled_by_this_extension only once something is set', () => {
  const before = getResult(false, null)
  assert.strictEqual(before.levelOfControl, 'controllable_by_this_extension')
  const after = getResult(true, { mode: 'system' })
  assert.strictEqual(after.levelOfControl, 'controlled_by_this_extension')
  assert.deepStrictEqual(after.value, { mode: 'system' })
})

test('WebRTC policies are limited to the names Chromium accepts', () => {
  assert.strictEqual(toWebRTCPolicy('disable_non_proxied_udp'), 'disable_non_proxied_udp')
  assert.strictEqual(toWebRTCPolicy(undefined), 'default')
  assert.throws(() => toWebRTCPolicy('protect_everything'), /unsupported webRTCIPHandlingPolicy/)
})

test('a proxy that resolves to DIRECT is reported as not applied', () => {
  const config = toElectronProxy({ mode: 'fixed_servers', rules: { singleProxy: { scheme: 'https', host: 'nl-1.protonvpn.net', port: 4443 } } })
  const out = proxyOutcome(config, 'DIRECT')
  assert.strictEqual(out.ok, false)
  assert.match(out.reason, /DIRECT/)
})

test('a proxy that resolves to the requested server is accepted', () => {
  const fixed = toElectronProxy({ mode: 'fixed_servers', rules: { singleProxy: { scheme: 'https', host: 'nl-1.protonvpn.net', port: 4443 } } })
  assert.strictEqual(proxyOutcome(fixed, 'HTTPS nl-1.protonvpn.net:4443').ok, true)
  const http = toElectronProxy({ mode: 'fixed_servers', rules: { singleProxy: { scheme: 'http', host: 'proxy.local', port: 8080 } } })
  assert.strictEqual(proxyOutcome(http, 'PROXY proxy.local:8080').ok, true)
})

test('a different proxy than the one asked for is rejected', () => {
  const config = toElectronProxy({ mode: 'fixed_servers', rules: { singleProxy: { host: 'wanted.example', port: 1 } } })
  assert.strictEqual(proxyOutcome(config, 'PROXY other.example:8080').ok, false)
})

test('direct and system modes are not leaks', () => {
  assert.strictEqual(proxyOutcome({ mode: 'direct' }, 'DIRECT').ok, true)
  assert.strictEqual(proxyOutcome({ mode: 'system' }, 'DIRECT').ok, true)
})

test('an empty resolution is a failure, not a pass', () => {
  const config = toElectronProxy({ mode: 'pac_script', pacScript: { data: 'x' } })
  assert.strictEqual(proxyOutcome(config, '').ok, false)
})
