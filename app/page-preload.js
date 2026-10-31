// PsychoVault page preload.
//
// Runs in every tab. Remote pages get nothing at all - the bridge below is only
// installed when the document being loaded is one of our own internal pages
// (the settings screen and the new tab page), which are file:// documents
// inside the app directory.
const { contextBridge, ipcRenderer } = require('electron')

const isInternal = (() => {
  try {
    const href = String(window.location.href || '')
    return href.startsWith('file://') && /\/ui\/(settings|newtab|vpncheck|recovery)\.html(\?|#|$)/.test(href)
  } catch {
    return false
  }
})()

if (isInternal) {
  const invoke = (channel, ...args) => ipcRenderer.invoke(channel, ...args)
  contextBridge.exposeInMainWorld('pv', {
    settings: {
      get: () => invoke('settings:get'),
      set: (patch) => invoke('settings:set', patch)
    },
    history: {
      clear: () => invoke('history:clear')
    },
    nav: {
      go: (input) => invoke('nav:go', input)
    },
    openExternal: (url) => invoke('open:external', url),
    tab: {
      close: (id) => invoke('tab:close', id),
      open: (url) => invoke('tab:open', url)
    },
    // the settings page owns the download folder control, so it needs the picker
    downloads: {
      dir: () => invoke('downloads:dir'),
      pickdir: () => invoke('downloads:pickdir')
    },
    data: {
      report: () => invoke('data:report'),
      openErrorLog: () => invoke('data:openErrorLog'),
      clear: (what) => invoke('data:clear', what)
    },
    app: {
      info: () => invoke('app:info'),
      diagnostics: () => invoke('app:diagnostics'),
      diagnosticsCopy: () => invoke('app:diagnosticsCopy')
    },
    recovery: { list: () => invoke('recovery:list'), restore: selections => invoke('recovery:restore', selections) },
    vpncheck: { open: () => invoke('vpncheck:open'), ip: () => invoke('vpncheck:ip') },
    proxy: {
      selftest: () => invoke('proxy:selftest'),
      status: () => invoke('proxy:status')
    },
    updates: {
      check: () => invoke('update:check'),
      tokenstate: () => invoke('update:tokenstate'),
      settoken: (token) => invoke('update:settoken', token)
    },
    extensions: {
      list: () => invoke('ext:list'),
      addfolder: () => invoke('ext:addfolder'),
      addcrx: () => invoke('ext:addcrx'),
      remove: (dir) => invoke('ext:remove', dir),
      toggle: (o) => invoke('ext:toggle', o),
      openpage: (o) => invoke('ext:openpage', o),
      restorebuiltin: (key) => invoke('ext:restorebuiltin', key),
      status: () => invoke('ext:status')
    },
    gx: {
      switches: () => invoke('gx:switches'),
      relaunch: () => invoke('app:relaunch')
    },
    vault: {
      status: () => invoke('vault:status'),
      transfer: (action, password) => invoke('vault:transfer', action, password),
      list: () => invoke('vault:list'),
      add: (entry) => invoke('vault:add', entry),
      remove: (id) => invoke('vault:delete', id),
      reveal: (id) => invoke('vault:reveal', id),
      copy: (id, which) => invoke('vault:copy', { id, which }),
      generate: (opts) => invoke('vault:generate', opts),
      setMaster: (pw) => invoke('vault:setMaster', pw),
      forgetMaster: () => invoke('vault:forgetMaster'),
      unlock: (pw) => invoke('vault:unlock', pw),
      lock: () => invoke('vault:lock')
    },
    // the three the settings page needs to be more than a picture of a control
    spell: {
      status: () => invoke('spell:status'),
      add: (word) => invoke('spell:add', word),
      remove: (word) => invoke('spell:remove', word)
    },
    rail: {
      icons: () => invoke('rail:icons'),
      catalogue: () => invoke('catalogue:list'),
      toggle: (name) => invoke('catalogue:toggle', name)
      // no rail.menu here: the right-click menu belongs to the chrome's rail, not to a tab
    },
    on: (channel, cb) => {
      // the settings page is a tab, so it hears the same pushes the chrome does -
      // only these two, and only the ones that change what the page shows
      const allowed = ['spell', 'extensions']
      if (!allowed.includes(channel)) return
      ipcRenderer.on(channel, (_e, payload) => cb(payload))
    }
  })
}
