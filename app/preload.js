// PsychoVault - preload for the chrome UI. Whitelisted IPC only.
const { contextBridge, ipcRenderer } = require('electron')

const invoke = (channel, ...args) => ipcRenderer.invoke(channel, ...args)

contextBridge.exposeInMainWorld('pv', {
  tab: {
    new: (url) => invoke('tab:new', url),
    close: (id) => invoke('tab:close', id),
    activate: (id) => invoke('tab:activate', id),
    reopen: () => invoke('tab:reopen'),
    menu: (o) => invoke('tab:menu', o),
    pin: (o) => invoke('tab:pin', o),
    mute: (o) => invoke('tab:mute', o),
    move: (o) => invoke('tab:move', o)
  },
  nav: {
    go: (input) => invoke('nav:go', input),
    back: () => invoke('nav:back'),
    forward: () => invoke('nav:forward'),
    reload: () => invoke('nav:reload'),
    stop: () => invoke('nav:stop'),
    home: () => invoke('nav:home')
  },
  translate: {
    page: () => invoke('translate:page')
  },
  win: {
    minimize: () => invoke('win:minimize'),
    maximize: () => invoke('win:maximize'),
    close: () => invoke('win:close'),
    private: () => invoke('win:private')
  },
  bookmarks: {
    list: () => invoke('bookmarks:list'),
    add: (bm) => invoke('bookmarks:add', bm),
    remove: (url) => invoke('bookmarks:remove', url),
    isBookmarked: (url) => invoke('bookmarks:isBookmarked', url)
  },
  history: {
    list: () => invoke('history:list'),
    clear: () => invoke('history:clear'),
    search: (q) => invoke('history:search', q)
  },
  settings: {
    get: () => invoke('settings:get'),
    set: (patch) => invoke('settings:set', patch)
  },
  find: {
    start: (text) => invoke('find:start', text),
    stop: () => invoke('find:stop')
  },
  zoom: {
    in: () => invoke('zoom:in'),
    out: () => invoke('zoom:out'),
    reset: () => invoke('zoom:reset')
  },
  view: {
    split: () => invoke('view:split')
  },
  downloads: {
    list: () => invoke('downloads:list'),
    control: (o) => invoke('downloads:control', o),
    clear: () => invoke('downloads:clear'),
    open: (p) => invoke('downloads:open', p),
    reveal: (p) => invoke('downloads:reveal', p),
    dir: () => invoke('downloads:dir'),
    pickdir: () => invoke('downloads:pickdir')
  },
  ui: {
    layout: (o) => invoke('ui:layout', o),
    menu: () => invoke('ui:menu')
  },
  ws: {
    list: () => invoke('ws:list'),
    switch: (i) => invoke('ws:switch', i),
    new: (name) => invoke('ws:new', name),
    close: (i) => invoke('ws:close', i),
    rename: (i, name) => invoke('ws:rename', { i, name }),
    menu: (i) => invoke('ws:menu', i)
  },
  clip: {
    write: (t) => invoke('clipboard:write', t)
  },
  rail: {
    icons: () => invoke('rail:icons'),
    catalogue: () => invoke('catalogue:list'),
    toggle: (name) => invoke('catalogue:toggle', name),
    menu: (i) => invoke('gx:services', { i, action: 'menu' })
  },
  spell: {
    status: () => invoke('spell:status'),
    add: (w) => invoke('spell:add', w),
    remove: (w) => invoke('spell:remove', w)
  },
  updates: {
    banner: () => invoke('update:banners'),
    install: () => invoke('update:install'),
    later: () => invoke('update:later'),
    dismiss: () => invoke('update:dismiss')
  },
  proton: { toggle: () => invoke('proton:toggle') },
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
    hot: (o) => invoke('gx:hot', o),
    media: (cmd) => invoke('gx:media', cmd),
    snapshot: (kind) => invoke('gx:snapshot', kind),
    panic: (on) => invoke('gx:panic', on),
    services: (o) => invoke('gx:services', o),
    switches: () => invoke('gx:switches'),
    relaunch: () => invoke('app:relaunch')
  },
  vault: {
    status: () => invoke('vault:status'),
    unlock: (pw) => invoke('vault:unlock', pw),
    lock: () => invoke('vault:lock')
  },
  openExternal: (url) => invoke('open:external', url),
  on: (channel, cb) => {
    const allowed = ['state', 'hover', 'downloads', 'downloader', 'loadfail', 'cmd', 'menu-command', 'vault', 'workspaces', 'spell', 'icons', 'extensions', 'update-progress', 'update-available']
    if (!allowed.includes(channel)) return
    ipcRenderer.on(channel, (_e, payload) => cb(payload))
  }
})
