'use strict'
// Deliberately small and dependency-free. Every call goes through chrome.proxy, which is the
// API a VPN extension uses, so if this works then a VPN extension has something to work with.

const $ = (id) => document.getElementById(id)
const status = (text) => { $('status').textContent = text }

function lastError() {
  const e = chrome.runtime && chrome.runtime.lastError
  return e ? String(e.message || e) : ''
}

function show(value) {
  const mode = (value && value.value && value.value.mode) || 'system'
  const control = (value && value.levelOfControl) || 'unknown'
  if (mode === 'fixed_servers') {
    const rules = value.value.rules || {}
    const p = rules.singleProxy || {}
    status('Proxied: ' + (p.scheme || 'http') + '://' + p.host + ':' + p.port + '\n(' + control + ')')
  } else if (mode === 'pac_script') {
    status('Proxied by a PAC script\n(' + control + ')')
  } else {
    status('Direct: no proxy is in use\n(' + control + ')')
  }
}

function read() {
  chrome.proxy.settings.get({}, (value) => {
    const err = lastError()
    if (err) { status('chrome.proxy refused the call: ' + err + '\nPsychoVault\'s shim is not installed in this context.'); return }
    show(value)
  })
}

$('apply').addEventListener('click', () => {
  const host = $('host').value.trim()
  const port = Number($('port').value.trim())
  if (!host || !Number.isFinite(port) || port < 1 || port > 65535) {
    status('Enter a host and a port between 1 and 65535.')
    return
  }
  const value = {
    mode: 'fixed_servers',
    rules: {
      singleProxy: { scheme: $('scheme').value, host, port },
      bypassList: ['localhost', '127.0.0.1', '<local>']
    }
  }
  chrome.proxy.settings.set({ value, scope: 'regular' }, () => {
    const err = lastError()
    if (err) { status('The proxy was refused: ' + err); return }
    read()
  })
})

$('clear').addEventListener('click', () => {
  chrome.proxy.settings.clear({ scope: 'regular' }, () => {
    const err = lastError()
    if (err) { status('Could not clear the proxy: ' + err); return }
    read()
  })
})

chrome.storage.local.get(['lastCheck'], (data) => {
  const last = data && data.lastCheck
  if (last && !last.ok) status('chrome.proxy call failed at startup: ' + last.reason)
})
read()
