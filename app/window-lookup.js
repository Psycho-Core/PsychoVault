'use strict'
// Which window owns the webContents that sent an IPC message?
//
// The chrome UI (tab strip, omnibox, rail) is the window's own webContents, so a naive
// `w.win.webContents === sender` works for it. But Settings is a *tab* - a WebContentsView
// owned by a tab - and so is every web page. Matching only the window's own webContents
// therefore returns nothing for any message sent from Settings or from a page.
//
// That is exactly why "Open its window" on an installed extension answered "no window to
// open it in": the request was valid, the extension was loaded, and the lookup could not see
// the window it came from. Keeping the lookup here, free of Electron, means it can be tested
// against fakes instead of only being discovered by a user on Windows.

function findWindowFor(sender, windows) {
  if (!sender) return null
  const list = [...(windows || [])]

  // 1. the window's own chrome UI
  for (const w of list) {
    try {
      if (w && w.win && w.win.webContents === sender) return w
    } catch { /* destroyed window */ }
  }

  // 2. any tab view: Settings, the new tab page, or a web page.
  // Reading .tabs from a destroyed window throws, so even the property access is guarded -
  // a closed window must not break a lookup made on behalf of an open one.
  for (const w of list) {
    let tabs
    try {
      if (!w || !w.tabs) continue
      tabs = w.tabs instanceof Map ? [...w.tabs.values()] : Object.values(w.tabs)
    } catch { continue }
    for (const t of tabs) {
      try {
        if (t && t.view && t.view.webContents === sender) return w
      } catch { /* destroyed view */ }
    }
  }

  return null
}

module.exports = { findWindowFor }
