'use strict'
// Runs inside Proton's popup, not the browser chrome. Use its own connection controls
// so Proton can pick a server, refresh credentials, and update its state itself.
function inspectProtonPopup() {
  const visible = el => !!el && getComputedStyle(el).display !== 'none' && !!el.getClientRects().length
  const signedOut = document.querySelector('#sign-in-view')
  const logged = document.querySelector('#logged-view')
  if (visible(signedOut) && !visible(logged)) return { state: 'signed-out' }
  if (!visible(logged)) return { state: 'loading' }
  const disconnect = document.querySelector('#status button.disconnection-button')
  const quick = document.querySelector('#status button.quick-connect-button')
  if (visible(disconnect) && disconnect.textContent.trim()) return { state: 'on' }
  if (visible(quick) && quick.textContent.trim() && !quick.disabled) return { state: 'off' }
  return { state: 'loading' }
}
function clickProtonPopup(action) {
  const selector = action === 'disconnect' ? '#status button.disconnection-button' : '#status button.quick-connect-button'
  const button = document.querySelector(selector)
  if (!button || button.disabled || !button.getClientRects().length) return false
  button.click()
  return true
}
module.exports = { inspectProtonPopup, clickProtonPopup }
