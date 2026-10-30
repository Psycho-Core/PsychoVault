'use strict'
// Reports, on every start, what chrome.proxy actually is. If PsychoVault's shim installed,
// settings.get answers with a real mode; if this is Chromium's stub, the call fails and the
// badge says so. That difference is the whole VPN problem in one line.

function paint(text, colour) {
  try { chrome.action.setBadgeText({ text }) } catch (e) { /* no action API */ }
  try { chrome.action.setBadgeBackgroundColor({ color: colour }) } catch (e) { /* optional */ }
}

function mark(result) {
  try { chrome.storage.local.set({ lastCheck: result }) } catch (e) { /* storage gone */ }
}

function check() {
  if (!chrome.proxy || !chrome.proxy.settings || typeof chrome.proxy.settings.get !== 'function') {
    paint('X', '#c0392b')
    mark({ ok: false, reason: 'chrome.proxy is missing entirely' })
    return
  }
  chrome.proxy.settings.get({}, (value) => {
    const err = chrome.runtime && chrome.runtime.lastError
    if (err) {
      paint('X', '#c0392b')
      mark({ ok: false, reason: String(err.message || err) })
      return
    }
    const mode = (value && value.value && value.value.mode) || 'system'
    const control = (value && value.levelOfControl) || 'unknown'
    paint(mode === 'fixed_servers' || mode === 'pac_script' ? 'ON' : 'off', mode === 'system' ? '#7f8c8d' : '#16a085')
    mark({ ok: true, mode, levelOfControl: control })
  })
}

chrome.runtime.onStartup.addListener(check)
chrome.runtime.onInstalled.addListener(check)
check()
