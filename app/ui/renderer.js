// PsychoVault - chrome renderer
const $ = (s) => document.querySelector(s)
const params = new URLSearchParams(location.search)
const isPrivate = params.get('private') === '1'
if (isPrivate) document.body.classList.add('private')

let state = { tabs: [], activeId: null, canGoBack: false, canGoForward: false, loading: false, url: '', maximized: false }
let SUGGEST_H = 250
const FIND_H = 44
let MENU_H = 340
let suggestIndex = -1
let suggestItems = []

// ------------------------------------------------------------------ tabs
let audioCtx = null
function gxSound(kind) {
  const cfg = state.sounds
  if (!cfg || !cfg.on) return
  try {
    audioCtx = audioCtx || new (window.AudioContext || window.webkitAudioContext)()
    if (audioCtx.state === 'suspended') audioCtx.resume().catch(() => {})
    const t0 = audioCtx.currentTime
    const vol = Math.max(0.02, Math.min(1, cfg.vol || 0.4))
    const freq = kind === 'tab' ? 880 : kind === 'new' ? 660 : 1500
    const osc = audioCtx.createOscillator()
    const g = audioCtx.createGain()
    osc.type = 'triangle'
    osc.frequency.setValueAtTime(freq, t0)
    osc.frequency.exponentialRampToValueAtTime(freq * 0.45, t0 + 0.05)
    g.gain.setValueAtTime(0.0001, t0)
    g.gain.exponentialRampToValueAtTime(0.16 * vol, t0 + 0.005)
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.07)
    osc.connect(g).connect(audioCtx.destination)
    osc.start(t0); osc.stop(t0 + 0.08)
  } catch (e) { /* no audio device is not a browser bug */ }
}
window.addEventListener('keydown', (e) => {
  const tag = e.target && e.target.tagName
  if (tag === 'INPUT' || tag === 'TEXTAREA') gxSound('key')
})

// Tab islands: children of a collapsed root are hidden, the root keeps a count.
const collapsedIslands = new Set()
function containerOf(t) {
  if (!t.container) return null
  return (state.containers || []).find((c) => c.name === t.container) || { name: t.container, color: '#8b9bb4' }
}

// set when a drag ends, so the click that follows a drop does not also activate
let suppressTabClickUntil = 0
// state pushes land constantly (titles, favicons, loading flags) and every one rebuilds
// the strip - which would delete the element under the pointer mid-gesture. While a drag
// is live the rebuild is deferred instead of killing the drag.
let tabDragActive = false
let tabRenderPending = false

function renderTabs() {
  if (tabDragActive) { tabRenderPending = true; return }
  const wrap = $('#tabs')
  wrap.innerHTML = ''
  const islandCount = new Map()
  for (const t of state.tabs) if (t.island) islandCount.set(t.island, (islandCount.get(t.island) || 0) + 1)
  for (const t of state.tabs) {
    if (t.island && collapsedIslands.has(t.island) && t.id !== state.activeId) continue
    const el = document.createElement('div')
    el.className = 'tab' +
      (t.id === state.activeId ? ' active' : '') +
      (t.pinned ? ' pinned' : '') +
      (t.discarded ? ' dozing' : '') +
      (t.muted ? ' muted' : '')
    el.dataset.id = t.id
    el.setAttribute('role', 'tab')
    el.setAttribute('aria-selected', t.id === state.activeId ? 'true' : 'false')
    el.setAttribute('aria-label', prettyTitle(t) + (t.pinned ? ', pinned' : '') + (t.muted ? ', muted' : '') + (t.discarded ? ', sleeping' : ''))
    el.tabIndex = t.id === state.activeId ? 0 : -1
    el.title = (t.pinned ? 'Pinned - ' : '') + (t.url || 'New tab') + (t.discarded ? ' (sleeping)' : '')

    if (t.loading) {
      const sp = document.createElement('div')
      sp.className = 'tab-spinner'
      el.appendChild(sp)
    } else {
      const fav = document.createElement('div')
      fav.className = 'tab-fav'
      if (t.favicon && !/^(chrome|data|about):/.test(t.url)) {
        const img = document.createElement('img')
        img.src = t.favicon
        img.onerror = () => { img.remove(); fav.textContent = (t.title || '?')[0].toUpperCase() }
        fav.appendChild(img)
      } else {
        fav.textContent = (t.title || '?')[0].toUpperCase()
      }
      el.appendChild(fav)
    }

    const cont = containerOf(t)
    if (cont) {
      const dot = document.createElement('span')
      dot.className = 'cdot'
      dot.style.background = cont.color
      dot.title = 'Container: ' + cont.name
      const nm = document.createElement('span')
      nm.className = 'cname'
      nm.textContent = cont.name
      el.append(dot, nm)
      el.classList.add('contained')
    }
    if (islandCount.has(t.id)) {   // the root itself has no island id - count is the test
      const collapsed = collapsedIslands.has(t.id)
      const b = document.createElement('button')
      b.type = 'button'
      b.className = 'island-btn'
      b.textContent = collapsed ? '+' + islandCount.get(t.id) : '\u25be'
      b.title = collapsed ? 'Expand this island' : 'Collapse this island'
      b.setAttribute('aria-label', (collapsed ? 'Expand' : 'Collapse') + ' tabs opened from ' + prettyTitle(t))
      b.setAttribute('aria-expanded', String(!collapsed))
      b.onclick = (e) => {
        e.stopPropagation()
        if (collapsedIslands.has(t.id)) collapsedIslands.delete(t.id)
        else collapsedIslands.add(t.id)
        renderTabs()
      }
      el.insertBefore(b, el.firstChild)
    }

    const title = document.createElement('div')
    title.className = 'tab-title'
    title.textContent = prettyTitle(t)
    el.appendChild(title)

    if (!t.pinned) {
      const x = document.createElement('button')
      x.className = 'tab-close'
      x.textContent = '\u00d7'
      x.setAttribute('aria-label', 'Close ' + prettyTitle(t))
      x.title = 'Close tab'
      x.onclick = (e) => { e.stopPropagation(); window.pv.tab.close(t.id) }
      el.appendChild(x)
    }

    el.onkeydown = (e) => {
      if (e.target !== el || e.altKey || e.ctrlKey || e.metaKey) return
      const visible = [...wrap.querySelectorAll('.tab')]
      const current = visible.indexOf(el)
      let next = -1
      if (e.key === 'ArrowRight') next = (current + 1) % visible.length
      else if (e.key === 'ArrowLeft') next = (current - 1 + visible.length) % visible.length
      else if (e.key === 'Home') next = 0
      else if (e.key === 'End') next = visible.length - 1
      else if (e.key === 'Enter' || e.key === ' ') next = current
      if (next < 0 || !visible[next]) return
      e.preventDefault()
      const targetId = Number(visible[next].dataset.id)
      window.pv.tab.activate(targetId).then(() => {
        const active = [...wrap.querySelectorAll('.tab')].find((node) => Number(node.dataset.id) === targetId)
        if (active) active.focus()
      }).catch(() => {})
    }
    el.onclick = () => { if (Date.now() < suppressTabClickUntil) return; gxSound('tab'); window.pv.tab.activate(t.id) }
    el.onauxclick = (e) => { if (e.button === 1 && !t.pinned) window.pv.tab.close(t.id) }
    el.oncontextmenu = (e) => {
      e.preventDefault()
      window.pv.tab.menu({ id: t.id, x: Math.round(e.clientX), y: Math.round(e.clientY) })
    }
    // double-click pins, the way it does in Opera GX
    el.ondblclick = (e) => { e.preventDefault(); window.pv.tab.pin({ id: t.id, pinned: !t.pinned }) }

    // drag to reorder. Pointer events, not HTML5 drag and drop: the strip lives inside a
    // -webkit-app-region: drag band and Chromium swallows native drags there, which is
    // why there was no reordering at all before. The gesture only becomes a drag past
    // 5px horizontally, so a plain click still selects the tab.
    el.onpointerdown = (e) => {
      if (e.button !== 0 || (e.target.closest && e.target.closest('.tab-close'))) return
      const startX = e.clientX, startY = e.clientY
      let dragging = false
      const clearMarks = () => { for (const n of wrap.querySelectorAll('.tab.drop-l')) n.classList.remove('drop-l') }
      const move = (ev) => {
        if (!dragging) {
          const dx = Math.abs(ev.clientX - startX), dy = Math.abs(ev.clientY - startY)
          if (dx < 5 || dx < dy) return          // a wobble, or a vertical gesture: leave it alone
          dragging = true
          tabDragActive = true
          el.classList.add('dragging')
          document.body.classList.add('tab-dragging')
        }
        clearMarks()
        // the first tab whose midpoint is right of the pointer is the one we insert before
        for (const n of wrap.querySelectorAll('.tab')) {
          if (n === el) continue
          const r = n.getBoundingClientRect()
          if (ev.clientX < r.left + r.width / 2) { n.classList.add('drop-l'); break }
        }
      }
      const up = () => {
        document.removeEventListener('pointermove', move)
        document.removeEventListener('pointerup', up)
        document.removeEventListener('pointercancel', up)
        if (!dragging) return
        el.classList.remove('dragging')
        document.body.classList.remove('tab-dragging')
        const mark = wrap.querySelector('.tab.drop-l')
        const before = mark ? Number(mark.dataset.id) : null
        clearMarks()
        tabDragActive = false
        suppressTabClickUntil = Date.now() + 400
        window.pv.tab.move({ id: t.id, before })
        if (tabRenderPending) { tabRenderPending = false; renderTabs() }
      }
      document.addEventListener('pointermove', move)
      document.addEventListener('pointerup', up)
      document.addEventListener('pointercancel', up)
    }
    wrap.appendChild(el)
  }
}

function prettyTitle(t) {
  if (!t.title) return 'New Tab'
  if (t.url && t.url.startsWith('psychovault://')) return t.url === 'psychovault://settings' ? 'Settings' : 'New Tab'
  return t.title
}

function updateBookmarkButton(bookmarked) {
  const button = $('#btn-bookmark')
  if (!button) return
  const label = bookmarked ? 'Remove bookmark' : 'Bookmark this page'
  button.classList.toggle('on', !!bookmarked)
  button.setAttribute('aria-pressed', String(!!bookmarked))
  button.setAttribute('aria-label', label)
  button.title = label
}

function renderNav() {
  $('#btn-back').disabled = !state.canGoBack
  $('#btn-fwd').disabled = !state.canGoForward
  const om = $('#omnibox')
  if (document.activeElement !== om) {
    const u = state.url || ''
    om.value = u.startsWith('file://') ? '' : u
  }
  updateBookmarkButton(!!state.isBookmarked)
  const act = (state.tabs || []).find(t => t.id === state.activeId)
  const pageBlocked = act ? act.blocked || 0 : 0
  $('#shield-count').textContent = pageBlocked
  const shields = $('#shield')
  if (shields) {
    let host = ''
    try { const u = new URL(state.url); if (['http:', 'https:'].includes(u.protocol)) host = u.hostname.toLowerCase() } catch { /* internal page */ }
    const exceptions = String(state.settings?.blockAllowSites || '').split(/[\n,]+/).map(x => x.trim().toLowerCase())
    const exempt = !!host && exceptions.includes(host)
    const disabled = state.settings && state.settings.blockAds === false
    shields.disabled = !host || !!state.private
    shields.title = (disabled ? 'Blocking is off globally. ' : exempt ? 'Blocking is off on ' + host + '. ' : 'Blocking is on for ' + host + '. ') + pageBlocked + ' requests blocked on this page; ' + (state.blockedTotal || 0) + ' in this browser session. Click to toggle this site.'
    shields.setAttribute('aria-label', shields.title)
    shields.setAttribute('aria-pressed', String(!exempt))
    shields.classList.toggle('off', !!disabled || exempt)
  }
  const adblock = $('#btn-adblock')
  if (adblock) {
    const enabled = !state.settings || state.settings.blockAds !== false
    const label = enabled ? 'Turn ad and tracker blocking off' : 'Turn ad and tracker blocking on'
    adblock.title = label
    adblock.setAttribute('aria-label', label)
    adblock.setAttribute('aria-pressed', String(enabled))
    adblock.classList.toggle('on', enabled)
    adblock.classList.toggle('status-on', enabled)
    adblock.classList.toggle('status-off', !enabled)
  }
  const proton = $('#btn-proton')
  if (proton) {
    const configured = !!state.protonProxyConfigured
    proton.classList.toggle('status-on', configured)
    proton.classList.toggle('status-off', !configured)
    proton.title = configured ? 'Proton proxy configured - connection not verified' : 'Proton VPN - proxy not configured'
    proton.setAttribute('aria-label', proton.title)
  }
  const sp = $('#btn-split')
  if (sp) {
    sp.classList.toggle('on', !!state.splitId)
    sp.setAttribute('aria-pressed', String(!!state.splitId))
  }
}

const ACCENTS = {
  cyan: ['#38bdf8', '#0ea5e9'],
  red: ['#ff5a5a', '#e03131'],
  purple: ['#a78bfa', '#8b5cf6'],
  green: ['#3ddc97', '#22c55e'],
  orange: ['#ffb020', '#f97316'],
  pink: ['#f472b6', '#ec4899'],
  gold: ['#facc15', '#eab308'],
  teal: ['#2dd4bf', '#0d9488'],
  indigo: ['#818cf8', '#6366f1'],
  rose: ['#fb7185', '#e11d48']
}
let lastAccent = null

function rgba(hex, a) {
  const n = parseInt(hex.slice(1), 16)
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`
}

// Private windows keep the violet accent: it is the signal that nothing is stored.
function applyAccent(s) {
  const name = (s.settings && s.settings.accent) || 'cyan'
  const prefs = s.settings || {}
  document.body.dataset.theme = prefs.theme || 'midnight'
  document.body.classList.toggle('no-rail', !!prefs.hideSidebar)
  document.body.classList.toggle('high-contrast', !!prefs.highContrast)
  document.body.classList.toggle('reduce-motion', !!prefs.reduceMotion)
  document.body.classList.toggle('always-show-tab-close', !!prefs.showTabCloseButtons)
  if (isPrivate || name === lastAccent) return
  lastAccent = name
  const c = ACCENTS[name] || ACCENTS.cyan
  // Written as a style element, not inline: an inline custom property on the root
  // would outrank a user stylesheet mod and make it look broken.
  let ac = document.getElementById('accent-style')
  if (!ac) {
    ac = document.createElement('style')
    ac.id = 'accent-style'
    document.head.appendChild(ac)
  }
  ac.textContent = `:root{--accent:${c[0]};--accent2:${c[1]};--accent-soft:${rgba(c[0], 0.14)}}`
}

// Workspace chips sit at the right of the tab row: click to switch, double-click
// to rename inline, middle-click to close. Ctrl+1..9 also jumps between them.
function renderWorkspaces() {
  const wrap = $('#ws')
  if (!wrap) return
  // Private windows have no workspaces: nothing about them is written to disk.
  if (isPrivate) { wrap.innerHTML = ''; return }
  const ws = state.workspaces || { list: [], active: 0 }
  wrap.innerHTML = ''
  ws.list.forEach((w, i) => {
    const chip = document.createElement('div')
    chip.className = 'ws-chip' + (i === ws.active ? ' on' : '')
    chip.title = w.name + ' - click to switch, double-click to rename, right-click for more'
    const nm = document.createElement('span')
    nm.textContent = w.name
    const n = document.createElement('span')
    n.className = 'n'
    n.textContent = w.count || ''
    chip.append(nm, n)
    chip.onclick = () => window.pv.ws.switch(i)
    chip.ondblclick = (e) => {
      e.stopPropagation()
      chip.innerHTML = ''
      const inp = document.createElement('input')
      inp.value = w.name
      chip.appendChild(inp)
      inp.focus(); inp.select()
      const commit = () => window.pv.ws.rename(i, inp.value)
      inp.onkeydown = (ev) => {
        if (ev.key === 'Enter') commit()
        if (ev.key === 'Escape') renderWorkspaces()
        ev.stopPropagation()
      }
      inp.onblur = commit
    }
    chip.onauxclick = (e) => { if (e.button === 1) window.pv.ws.close(i) }
    // an x you can see beats a middle-click nobody finds
    const x = document.createElement('button')
    x.className = 'x'
    x.textContent = '\u00d7'
    x.title = ws.list.length > 1 ? 'Close this workspace' : 'Close all tabs in this workspace'
    x.onclick = (e) => { e.stopPropagation(); window.pv.ws.close(i) }
    chip.append(x)
    chip.oncontextmenu = (e) => { e.preventDefault(); window.pv.ws.menu(i) }
    wrap.appendChild(chip)
  })
  const add = document.createElement('button')
  add.id = 'ws-add'
  add.textContent = '+'
  add.title = 'New workspace'
  add.onclick = () => window.pv.ws.new('Workspace ' + (ws.list.length + 1))
  wrap.appendChild(add)
}

function applyState(update) {
  if (!update || typeof update !== 'object' || Array.isArray(update)) return
  // Some window lifecycle events carry a small patch. Merge them instead of replacing
  // the full state snapshot, otherwise tabs/settings disappear on maximize/restore.
  const s = state = { ...state, ...update }
  applyAccent(s)
  const maxButton = $('#wc-max')
  if (maxButton) {
    const label = s.maximized ? 'Restore window' : 'Maximise window'
    maxButton.title = label
    maxButton.setAttribute('aria-label', label)
  }
  let ms = document.getElementById('modstyle')
  if (!ms) {
    ms = document.createElement('style')
    ms.id = 'modstyle'
    document.head.appendChild(ms)
  }
  ms.textContent = s.modCss || ''
  document.body.classList.toggle('battery', !!s.battery)
  renderTabs()
  renderRail()
  renderWorkspaces()
  renderNav()
  const bookmarkUrl = s.url || ''
  window.pv.bookmarks.isBookmarked(bookmarkUrl).then((v) => {
    // A slower IPC reply for a previous tab must not repaint the current tab's star.
    if ((state.url || '') !== bookmarkUrl) return
    state.isBookmarked = v
    updateBookmarkButton(v)
  }).catch(() => { /* the window may be closing */ })
}

window.pv.on('state', applyState)
window.pv.on('hover', ({ url }) => {
  const bar = $('#statusbar')
  if (url) { $('#status-text').textContent = url; bar.classList.add('show') }
  else bar.classList.remove('show')
})
window.pv.on('vault', (v) => {
  if (v && v.toast) toast(v.toast)
})
window.pv.on('update-progress', ({ message }) => {
  if (message) toast(message)
})

window.pv.on('workspaces', (ws) => {
  if (ws) { state.workspaces = ws; renderWorkspaces() }
})
window.pv.on('icons', () => { window.pv.rail.icons().then((m) => { railIconMap = m || {}; renderRail() }).catch(() => {}) })
window.pv.on('spell', () => {})

window.pv.on('cmd', (what) => {
  if (what === 'palette') openPalette()
  else if (what === 'omnibox') { om.focus(); om.select() }
  else if (what === 'find') showFind()
  else if (what === 'bookmark') $('#btn-bookmark').click()
})
window.pv.on('downloads', (list) => {
  paintDlBadge(list)
  if ($('#panel').dataset.open === 'downloads' && !$('#panel').classList.contains('hidden')) {
    paintPanel('downloads')
  }
})

// Rail badge: how many transfers are moving, so "is anything happening?" is answerable
// with the panel shut - and it is the only part of the strip that moves while a big
// file lands behind a tab you are reading.
function paintDlBadge(list) {
  const btn = document.querySelector('.rail-btn[data-panel="downloads"]')
  if (!btn) return
  const live = (Array.isArray(list) ? list : []).filter((d) => d && d.state === 'progress').length
  let badge = btn.querySelector('.dl-badge')
  btn.classList.toggle('dl-busy', live > 0)
  if (live > 0) {
    if (!badge) { badge = document.createElement('span'); badge.className = 'dl-badge'; btn.appendChild(badge) }
    badge.textContent = String(live)
    btn.title = live === 1 ? 'Downloads - 1 in progress' : 'Downloads - ' + live + ' in progress'
    btn.setAttribute('aria-label', live === 1 ? 'Downloads, 1 in progress' : 'Downloads, ' + live + ' in progress')
  } else if (badge) {
    badge.remove()
    btn.title = 'Downloads'
    btn.setAttribute('aria-label', 'Downloads')
  }
}

// start and finish are announced; a download that says nothing is the bug this fixes
window.pv.on('downloader', (d) => {
  if (!d || !d.kind) return
  const folder = d.path ? String(d.path).replace(/[\\/][^\\/]*$/, '') : ''
  if (d.kind === 'start') toast('Downloading ' + d.name + (folder ? ' to ' + folder : ''))
  else if (d.kind === 'done') toast('Downloaded ' + d.name)
  else if (d.kind === 'failed') toast('Download failed - ' + d.name)
})
window.pv.on('loadfail', ({ desc, url }) => {
  toast(`${desc} - ${url}`.slice(0, 90))
})

// ------------------------------------------------------------------ nav
$('#btn-back').onclick = () => window.pv.nav.back()
$('#btn-fwd').onclick = () => window.pv.nav.forward()
$('#btn-reload').onclick = () => window.pv.nav.reload()
$('#btn-home').onclick = () => window.pv.nav.home()
$('#btn-newtab').onclick = () => window.pv.tab.new()
$('#btn-private').onclick = () => window.pv.win.private()
$('#wc-min').onclick = () => window.pv.win.minimize()
$('#wc-max').onclick = () => window.pv.win.maximize()
$('#wc-close').onclick = () => window.pv.win.close()

// ------------------------------------------------------------------ omnibox
const om = $('#omnibox')

om.addEventListener('focus', () => { om.select() })
om.addEventListener('blur', () => { renderNav() })

om.addEventListener('input', async () => {
  const q = om.value.trim()
  if (!q) return hideSuggest()
  const results = await window.pv.history.search(q)
  suggestItems = []
  if (q.includes('.') || q.startsWith('http')) {
    suggestItems.push({ type: 'go', main: `Go to ${q}`, url: q })
  }
  suggestItems.push({ type: 'search', main: `Search for "${q}"`, url: q })
  for (const r of results.slice(0, 6)) {
    suggestItems.push({ type: r.type, main: r.title || r.url, url: r.url })
  }
  suggestIndex = 0
  renderSuggest()
})

om.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    const v = suggestIndex >= 0 && suggestItems[suggestIndex] ? suggestItems[suggestIndex].url : om.value
    hideSuggest()
    window.pv.nav.go(v)
    om.blur()
  } else if (e.key === 'Escape') {
    hideSuggest(); om.blur()
  } else if (e.key === 'ArrowDown') {
    e.preventDefault(); suggestIndex = Math.min(suggestIndex + 1, suggestItems.length - 1); renderSuggest()
  } else if (e.key === 'ArrowUp') {
    e.preventDefault(); suggestIndex = Math.max(suggestIndex - 1, 0); renderSuggest()
  }
})

function renderSuggest() {
  const box = $('#suggest')
  box.innerHTML = ''
  if (!suggestItems.length) return hideSuggest()
  suggestItems.forEach((it, i) => {
    const row = document.createElement('div')
    row.className = 'sg' + (i === suggestIndex ? ' sel' : '')
    const type = document.createElement('span')
    type.className = 'sg-type'
    type.textContent = it.type
    const main = document.createElement('span')
    main.className = 'sg-main'
    main.textContent = it.main
    row.append(type, main)
    row.onmousedown = (e) => {
      e.preventDefault()
      hideSuggest()
      window.pv.nav.go(it.url)
      om.blur()
    }
    box.appendChild(row)
  })
  box.classList.remove('hidden')
  const r = $('#omnibox-wrap').getBoundingClientRect()
  box.style.left = Math.round(r.left) + 'px'
  box.style.width = Math.round(r.width) + 'px'
  box.style.top = Math.round(r.bottom + 6) + 'px'
  SUGGEST_H = box.offsetHeight + 14
  syncChrome()
}

function hideSuggest() {
  $('#suggest').classList.add('hidden')
  suggestIndex = -1
  suggestItems = []
  syncChrome()
}

// The page area never sits under chrome: whenever a dropdown is open we ask the
// main process to reserve that strip at the top.
function syncChrome() {
  let extra = 0
  if (!$('#suggest').classList.contains('hidden')) extra += SUGGEST_H
  if (!$('#findbar').classList.contains('hidden')) extra += FIND_H
  if (!$('#menu').classList.contains('hidden')) extra += MENU_H
  window.pv.ui.layout({ extraTop: extra, panel: !$('#panel').classList.contains('hidden') })
}

document.addEventListener('mousedown', (e) => {
  if (!e.target.closest('#suggest') && !e.target.closest('#omnibox-wrap')) hideSuggest()
  if (!e.target.closest('#menu') && !e.target.closest('#btn-menu') && !$('#menu').classList.contains('hidden')) {
    $('#menu').classList.add('hidden')
    $('#btn-menu').setAttribute('aria-expanded', 'false')
    syncChrome()
  }
})

// ------------------------------------------------------------------ bookmark
$('#btn-bookmark').onclick = async () => {
  const url = state.url
  if (!url || url.startsWith('psychovault://')) return
  if (state.isBookmarked) {
    await window.pv.bookmarks.remove(url)
    toast('Bookmark removed')
  } else {
    await window.pv.bookmarks.add({ url, title: state.title || url })
    toast('Bookmarked')
  }
  state.isBookmarked = !state.isBookmarked
  updateBookmarkButton(state.isBookmarked)
}

// ------------------------------------------------------------------ panels
let railIconMap = {}
window.pv.rail.icons().then((m) => { railIconMap = m || {}; renderRail() }).catch(() => {})

function renderRail() {
  const box = $('#rail-svc')
  if (!box) return
  box.innerHTML = ''
  const svcs = state.services || []
  const open = new Set((state.tabs || []).filter((t) => t.service).map((t) => {
    const u = (t.url || '').replace(/\/$/, '')
    return u
  }))
  svcs.forEach((svc, i) => {
    const b = document.createElement('button')
    b.className = 'rail-btn svc'
    b.title = svc.name + ' - opens in a tab, keeps playing when you switch away. Right-click for the rest.'
    b.setAttribute('aria-label', 'Open ' + svc.name + ' sidebar service')
    b.style.setProperty('--svc', svc.color || 'var(--dim)')
    const icon = svc.origin && railIconMap[svc.origin]
    if (icon) {
      const img = document.createElement('img')
      img.className = 'rail-ic'
      img.src = icon
      img.alt = ''
      img.onerror = () => { img.remove(); const sp = document.createElement('span'); sp.textContent = svc.initials || '?'; sp.className = 'rail-tx'; b.appendChild(sp) }
      b.appendChild(img)
    } else {
      const sp = document.createElement('span')
      sp.className = 'rail-tx'
      sp.textContent = svc.initials || String(svc.name || '?').slice(0, 2).toUpperCase()
      b.appendChild(sp)
    }
    if (open.has((svc.url || '').replace(/\/$/, ''))) b.classList.add('live')
    b.onclick = async () => { gxSound('tab'); await window.pv.gx.services({ i }) }
    b.oncontextmenu = (e) => { e.preventDefault(); window.pv.rail.menu(i) }
    box.appendChild(b)
  })
}

const railBtns = document.querySelectorAll('.rail-btn[data-panel]')
railBtns.forEach((b) => {
  b.onclick = () => openPanel(b.dataset.panel, b)
})

async function openPanel(name, btn) {
  const panel = $('#panel')
  if (panel.dataset.open === name && !panel.classList.contains('hidden')) {
    panel.classList.add('hidden'); panel.dataset.open = ''
    document.querySelectorAll('.rail-btn[data-panel]').forEach(b => {
      b.classList.remove('active')
      b.setAttribute('aria-expanded', 'false')
    })
    if (btn && !btn.matches('.rail-btn[data-panel]')) btn.classList.remove('active')
    syncChrome()
    return
  }
  document.querySelectorAll('.rail-btn[data-panel]').forEach(b => {
    const expanded = b.dataset.panel === name
    b.classList.toggle('active', expanded)
    b.setAttribute('aria-expanded', String(expanded))
  })
  if (btn && !btn.matches('.rail-btn[data-panel]')) btn.classList.add('active')
  panel.dataset.open = name
  panel.classList.remove('hidden')
  syncChrome()
  $('#panel-title').textContent = name[0].toUpperCase() + name.slice(1)
  return paintPanel(name)
}

// Repaint an open panel in place. The list refreshes several times a second while a file
// moves, and routing that through openPanel() again *toggles the panel shut* - which is
// exactly what happened the first time progress was broadcast live.
async function paintPanel(name) {
  const body = $('#panel-body')
  if (!body) return
  body.innerHTML = ''

  if (name === 'bookmarks') {
    const list = await window.pv.bookmarks.list()
    if (!list.length) return empty(body, 'No bookmarks yet. Click the star in the address bar.')
    for (const b of list) body.appendChild(panelRow(b, true))
  } else if (name === 'history') {
    const list = await window.pv.history.list()
    if (!list.length) return empty(body, 'No history yet.')
    for (const h of list) body.appendChild(panelRow(h, false))
    const act = document.createElement('div')
    act.className = 'pl-actions'
    const clr = document.createElement('button')
    clr.className = 'nav'
    clr.style.cssText = 'width:auto;padding:6px 10px;font-size:12px'
    clr.textContent = 'Clear history'
    clr.onclick = async () => { await window.pv.history.clear(); openPanel('history') }
    act.appendChild(clr)
    body.appendChild(act)
  } else if (name === 'hot') {
    const rows = await window.pv.gx.hot()
    const head = document.createElement('div')
    head.className = 'hint'
    head.textContent = state.netLimit
      ? `Per-process memory and CPU. Bandwidth cap: ${state.netLimit} KB/s.`
      : 'Per-process memory and CPU. No bandwidth cap set.'
    body.appendChild(head)
    if (!rows.length) return empty(body, 'No tabs open.')
    for (const r of rows) {
      const row = document.createElement('div')
      row.className = 'hot-row'
      const t = document.createElement('div')
      t.className = 't'
      t.textContent = (r.active ? '\u25b8 ' : '') + (r.title || 'Untitled') + (r.discarded ? ' (sleeping)' : '')
      t.title = r.url
      const mem = document.createElement('div'); mem.className = 'm'; mem.textContent = r.memMB + ' MB'
      const cpu = document.createElement('div'); cpu.className = 'm'; cpu.textContent = r.cpu + '%'
      const btn = document.createElement('button')
      btn.className = 'nav'
      btn.style.cssText = 'width:auto;padding:3px 7px;font-size:11px'
      btn.textContent = r.discarded ? 'Wake' : 'Sleep'
      btn.onclick = async () => { await window.pv.gx.hot({ action: r.discarded ? 'wake' : 'sleep', id: r.id }); openPanel('hot') }
      row.append(t, mem, cpu, btn)
      row.onclick = (e) => { if (e.target === row || e.target === t) window.pv.tab.activate(r.id) }
      body.appendChild(row)
    }
    const act = document.createElement('div')
    act.className = 'pl-actions'
    const rf = document.createElement('button')
    rf.className = 'nav'
    rf.style.cssText = 'width:auto;padding:6px 10px;font-size:12px'
    rf.textContent = 'Refresh'
    rf.onclick = () => openPanel('hot')
    act.appendChild(rf)
    body.appendChild(act)
  } else if (name === 'downloads') {
    const list = await window.pv.downloads.list()
    // Where files land is half the answer to "did my download work?", so the panel says
    // it and offers to change it without a trip to Settings.
    const dirBox = document.createElement('div')
    dirBox.className = 'dl-dir'
    const dirTxt = document.createElement('div')
    dirTxt.className = 'dl-dir-t'
    const dirPath = await window.pv.downloads.dir()
    const ask = !state.settings || state.settings.askSaveLocation !== false
    dirTxt.textContent = (ask ? 'Ask each time · starting folder: ' : 'Automatically save to ') + dirPath
    dirTxt.title = dirPath
    const chg = document.createElement('button')
    chg.className = 'mini'
    chg.textContent = 'Change'
    chg.onclick = async (ev) => {
      ev.stopPropagation()
      const picked = await window.pv.downloads.pickdir()
      if (picked) paintPanel('downloads')
    }
    dirBox.append(dirTxt, chg)
    body.appendChild(dirBox)
    if (!list.length) return empty(body, 'Downloads you start will appear here. Files go to the folder above, or wherever you choose each time.')
    for (const d of list) body.appendChild(downloadRow(d))
    const act = document.createElement('div')
    act.className = 'pl-actions'
    const clr = document.createElement('button')
    clr.className = 'nav'
    clr.style.cssText = 'width:auto;padding:6px 10px;font-size:12px'
    clr.textContent = 'Clear list'
    clr.onclick = async () => { await window.pv.downloads.clear(); paintPanel('downloads') }
    act.appendChild(clr)
    body.appendChild(act)
  }
}

function downloadRow(d) {
  const row = document.createElement('div')
  row.className = 'dl'
  const top = document.createElement('div')
  top.className = 'dl-top'
  const name = document.createElement('div')
  name.className = 'dl-name'
  name.textContent = d.name
  name.title = d.name
  const state = document.createElement('div')
  state.className = 'dl-state'
  const pct = d.total > 0 ? Math.min(100, Math.round((d.received / d.total) * 100)) : (d.state === 'completed' ? 100 : 0)
  state.textContent = d.state === 'completed' ? 'done' : d.state === 'progress' ? (d.total > 0 ? pct + '%' : 'downloading') : d.state
  top.append(name, state)
  const bar = document.createElement('div')
  bar.className = 'dl-bar'
  const fill = document.createElement('div')
  fill.className = 'dl-fill'
  fill.style.width = pct + '%'
  bar.appendChild(fill)
  row.append(top, bar)
  const meta = document.createElement('div')
  meta.className = 'dl-state'
  const bytes = n => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : (n / 1024).toFixed(1) + ' KB'
  meta.textContent = bytes(d.received || 0) + (d.total > 0 ? ' / ' + bytes(d.total) : '') + (d.state === 'progress' ? ' · ' + bytes(d.speed || 0) + '/s' : '')
  row.appendChild(meta)
  if (d.id && (d.state === 'progress' || d.state === 'paused' || (d.state === 'interrupted' && d.canResume))) {
    const action = d.state === 'progress' ? 'pause' : 'resume'
    const button = document.createElement('button'); button.className = 'mini'; button.style.marginTop = '8px'
    button.textContent = action === 'pause' ? 'Pause' : (d.state === 'interrupted' ? 'Retry / resume' : 'Resume')
    button.onclick = async ev => { ev.stopPropagation(); const result = await window.pv.downloads.control({ id: d.id, action }); if (!result || !result.ok) alert((result && result.error) || 'Download control failed.') }
    row.appendChild(button)
  }
  if (d.state === 'completed' && d.path) {
    const actions = document.createElement('div'); actions.className = 'dl-actions'
    const open = document.createElement('button'); open.className = 'mini'; open.textContent = 'Open'; open.onclick = () => window.pv.downloads.open(d.path)
    const show = document.createElement('button'); show.className = 'mini'; show.textContent = 'Show in folder'; show.onclick = () => window.pv.downloads.reveal(d.path)
    actions.append(open, show); row.appendChild(actions)
    row.title = d.path
  }
  return row
}

function empty(body, msg) {
  const d = document.createElement('div')
  d.className = 'pl-empty'
  d.textContent = msg
  body.appendChild(d)
}

function panelRow(item, removable) {
  const row = document.createElement('div')
  row.className = 'pl-item'
  const fav = document.createElement('div')
  fav.className = 'pl-fav'
  const txt = document.createElement('div')
  txt.className = 'pl-txt'
  const t = document.createElement('div')
  t.className = 'pl-title'
  t.textContent = item.title || item.url
  const u = document.createElement('div')
  u.className = 'pl-url'
  u.textContent = item.url
  txt.append(t, u)
  row.append(fav, txt)
  row.onclick = () => { window.pv.nav.go(item.url); $('#panel').classList.add('hidden') }
  if (removable) {
    const x = document.createElement('button')
    x.className = 'pl-x'
    x.textContent = '\u00d7'
    x.onclick = async (e) => {
      e.stopPropagation()
      await window.pv.bookmarks.remove(item.url)
      openPanel('bookmarks')
      railBtns[0].classList.add('active')
    }
    row.appendChild(x)
  }
  return row
}

$('#panel-close').onclick = () => {
  $('#panel').classList.add('hidden')
  $('#panel').dataset.open = ''
  railBtns.forEach(b => {
    b.classList.remove('active')
    b.setAttribute('aria-expanded', 'false')
  })
  syncChrome()
}

async function translateCurrentPage() {
  try {
    const result = await window.pv.translate.page()
    if (result && result.ok) toast('Opened ' + result.language + ' translation in a new tab')
    else if (!result || !result.cancelled) toast((result && result.error) || 'Could not translate this page')
  } catch { toast('Could not translate this page') }
}

// ------------------------------------------------------------------ menu
$('#btn-menu').onclick = (e) => {
  e.stopPropagation()
  $('#menu').classList.toggle('hidden')
  const open = !$('#menu').classList.contains('hidden')
  $('#btn-menu').setAttribute('aria-expanded', String(open))
  if (open) MENU_H = $('#menu').offsetHeight + 14
  syncChrome()
}

$('#menu').addEventListener('click', async (e) => {
  const btn = e.target.closest('button')
  if (!btn) return
  $('#menu').classList.add('hidden')
  $('#btn-menu').setAttribute('aria-expanded', 'false')
  syncChrome()
  const cmd = btn.dataset.cmd
  if (cmd === 'newtab') window.pv.tab.new()
  else if (cmd === 'private') window.pv.win.private()
  else if (cmd === 'reopen') window.pv.tab.reopen()
  else if (cmd === 'find') showFind()
  else if (cmd === 'translate') translateCurrentPage()
  else if (cmd === 'zoomin') window.pv.zoom.in()
  else if (cmd === 'zoomout') window.pv.zoom.out()
  else if (cmd === 'zoomreset') window.pv.zoom.reset()
  else if (cmd === 'bookmarks') openPanel('bookmarks', railBtns[0])
  else if (cmd === 'history') openPanel('history', railBtns[1])
  else if (cmd === 'downloads') openPanel('downloads', railBtns[2])
  else if (cmd === 'settings') window.pv.tab.new('psychovault://settings')
  else if (cmd === 'devtools') toast('Right-click any page and choose Inspect element')
})

$('#shield').onclick = async () => {
  if (state.private) return
  let host
  try { const u = new URL(state.url); if (!['http:', 'https:'].includes(u.protocol)) return; host = u.hostname.toLowerCase() } catch { return }
  const list = String(state.settings?.blockAllowSites || '').split(/[\n,]+/).map(x => x.trim()).filter(Boolean)
  const present = list.some(x => x.toLowerCase() === host)
  const next = present ? list.filter(x => x.toLowerCase() !== host) : list.concat([host])
  try { await window.pv.settings.set({ blockAllowSites: next.join('\n') }); toast((present ? 'Blocking enabled for ' : 'Blocking disabled for ') + host + ' - reload the page') }
  catch { toast('Could not change the site blocker') }
}

// This controls PsychoVault's built-in domain blocker, not uBlock Origin.
$('#btn-adblock').onclick = async () => {
  try {
    const enabled = !state.settings || state.settings.blockAds !== false
    await window.pv.settings.set({ blockAds: !enabled })
    toast(enabled ? 'Ad and tracker blocking is off' : 'Ad and tracker blocking is on')
  } catch (error) {
    toast('Could not change ad blocking: ' + (error.message || 'unknown error'))
  }
}

async function openProtonSettings() {
  try {
    const items = await window.pv.extensions.list()
    const proton = items.find((item) => item.builtin === 'proton-vpn')
    if (!proton) return toast('Proton VPN is not installed. Restore it in Settings > Extensions.')
    if (!proton.enabled || !proton.loaded) return toast('Enable Proton VPN in Settings > Extensions first.')
    if (!proton.popup) return toast('Proton VPN has no window to open. Check Settings > Extensions.')
    const result = await window.pv.extensions.openpage({ dir: proton.dir, page: proton.popup })
    if (!result || !result.ok) toast('Could not open Proton VPN: ' + ((result && result.error) || 'unknown error'))
  } catch (error) { toast('Could not open Proton VPN: ' + (error.message || 'unknown error')) }
}
$('#btn-proton').onclick = async () => {
  try {
    const result = await window.pv.proton.toggle()
    if (!result || !result.ok) return toast((result && result.error) || 'Could not change Proton VPN.')
    toast(result.action === 'connect' ? 'Proton proxy configured - connection not verified' : 'Proton VPN disconnected')
  } catch (error) { toast('Could not change Proton VPN: ' + (error.message || 'unknown error')) }
}
$('#btn-proton').oncontextmenu = (event) => { event.preventDefault(); openProtonSettings() }
$('#btn-adblock').oncontextmenu = (event) => { event.preventDefault(); window.pv.tab.new('psychovault://settings#privacy') }

$('#btn-settings').onclick = () => window.pv.tab.new('psychovault://settings')

$('#btn-split').onclick = () => window.pv.view.split()

// --------------------------------------------------------- command palette
const COMMANDS = [
  { id: 'newtab',   title: 'New tab',            keys: 'Ctrl+T',      run: () => window.pv.tab.new() },
  { id: 'closetab', title: 'Close tab',          keys: 'Ctrl+W',      run: () => state.activeId && window.pv.tab.close(state.activeId) },
  { id: 'reopen',   title: 'Reopen closed tab',  keys: 'Ctrl+Shift+T',run: () => window.pv.tab.reopen() },
  { id: 'private',  title: 'New private window', keys: 'Ctrl+Shift+P',run: () => window.pv.win.private() },
  { id: 'split',    title: 'Toggle split view',  keys: 'Ctrl+\\',     run: () => window.pv.view.split() },
  { id: 'settings', title: 'Open settings',      keys: '',            run: () => window.pv.tab.new('psychovault://settings') },
  { id: 'newtab2',  title: 'Open new tab page',  keys: '',            run: () => window.pv.nav.go('psychovault://newtab') },
  { id: 'bookmarks',title: 'Show bookmarks',     keys: '',            run: () => openPanel('bookmarks', railBtns[0]) },
  { id: 'history',  title: 'Show history',       keys: '',            run: () => openPanel('history', railBtns[1]) },
  { id: 'downloads',title: 'Show downloads',     keys: '',            run: () => openPanel('downloads', railBtns[2]) },
  { id: 'find',     title: 'Find in page',       keys: 'Ctrl+F',      run: () => showFind() },
  { id: 'translate',title: 'Translate current page', keys: '',             run: () => translateCurrentPage() },
  { id: 'wsnew',    title: 'New workspace',       keys: '',            run: () => window.pv.ws.new('Workspace') },
  { id: 'ws1',      title: 'Switch to workspace 1', keys: 'Ctrl+1',    run: () => window.pv.ws.switch(0) },
  { id: 'ws2',      title: 'Switch to workspace 2', keys: 'Ctrl+2',    run: () => window.pv.ws.switch(1) },
  { id: 'ws3',      title: 'Switch to workspace 3', keys: 'Ctrl+3',    run: () => window.pv.ws.switch(2) },
  { id: 'ws4',      title: 'Switch to workspace 4', keys: 'Ctrl+4',    run: () => window.pv.ws.switch(3) },
  { id: 'vaultlock',title: 'Lock the password vault', keys: '',        run: () => window.pv.vault.lock().then(() => toast('Vault locked')) },
  { id: 'hottabs',  title: 'Performance: hot tabs, memory and CPU per tab', keys: '', run: () => openPanel('hot', null) },
  { id: 'snap',     title: 'Snapshot: capture this page as PNG', keys: 'Ctrl+Shift+5', run: () => window.pv.gx.snapshot('png').then((r) => toast(r.ok ? 'Saved ' + r.file : 'Snapshot failed')) },
  { id: 'pdf',      title: 'Save this page as PDF', keys: '',          run: () => window.pv.gx.snapshot('pdf').then((r) => toast(r.ok ? 'Saved ' + r.file : 'PDF failed: ' + (r.reason || ''))) },
  { id: 'rgxv',     title: 'Enhance videos: contrast, saturation, brightness', keys: '',        run: () => window.pv.settings.get().then((st) => window.pv.settings.set({ enhanceVideo: !st.enhanceVideo })) },
  { id: 'rgxi',     title: 'Enhance images: contrast, saturation, brightness', keys: '',        run: () => window.pv.settings.get().then((st) => window.pv.settings.set({ enhanceImages: !st.enhanceImages })) },
  { id: 'panic',    title: 'Panic button: hide everything', keys: 'F12', run: () => window.pv.gx.panic(true) },
  { id: 'unpanic',  title: 'Panic button: bring the tabs back', keys: 'F11', run: () => window.pv.gx.panic(false) },
  { id: 'medpa',    title: 'Media: play or pause', keys: '',           run: () => window.pv.gx.media('toggle') },
  { id: 'svc0',     title: 'Open service: sidebar app 1', keys: '',     run: () => window.pv.gx.services({ i: 0 }) },
  { id: 'svc1',     title: 'Open service: sidebar app 2', keys: '',     run: () => window.pv.gx.services({ i: 1 }) },
  { id: 'zoomin',   title: 'Zoom in',            keys: 'Ctrl++',      run: () => window.pv.zoom.in() },
  { id: 'zoomout',  title: 'Zoom out',           keys: 'Ctrl+-',      run: () => window.pv.zoom.out() },
  { id: 'zoomreset',title: 'Reset zoom',         keys: 'Ctrl+0',      run: () => window.pv.zoom.reset() },
  { id: 'reload',   title: 'Reload page',        keys: 'Ctrl+R',      run: () => window.pv.nav.reload() },
  { id: 'back',     title: 'Go back',            keys: 'Alt+Left',    run: () => window.pv.nav.back() },
  { id: 'home',     title: 'Go home',            keys: '',            run: () => window.pv.nav.home() },
  { id: 'copyurl',  title: 'Copy current address', keys: '',          run: () => window.pv.clip.write(state.url || '') },
  { id: 'wsclose',  title: 'Close this workspace', keys: '',          run: () => window.pv.ws.close((state.workspaces || {}).active || 0) },
  { id: 'spellset', title: 'Spelling settings', keys: '', run: () => window.pv.tab.new('psychovault://settings') },
  { id: 'ddg',      title: 'Search engine: DuckDuckGo', keys: '',     run: () => window.pv.settings.set({ search: 'duckduckgo' }) },
  { id: 'brave',    title: 'Search engine: Brave',      keys: '',     run: () => window.pv.settings.set({ search: 'brave' }) },
  { id: 'startpage',title: 'Search engine: Startpage',  keys: '',     run: () => window.pv.settings.set({ search: 'startpage' }) },
  { id: 'block',    title: 'Toggle ad and tracker blocking', keys: '', run: () => window.pv.settings.set({ blockAds: !state.settings.blockAds }) },
  { id: 'clear',    title: 'Clear browsing history',  keys: '',       run: () => window.pv.history.clear() }
]

let palIndex = 0
let palItems = []

function openPalette() {
  $('#palette').classList.remove('hidden')
  window.pv.ui.layout({ palette: true })
  const inp = $('#palette-input')
  inp.value = ''
  inp.focus()
  palIndex = 0
  filterPalette('')
}

function closePalette() {
  $('#palette').classList.add('hidden')
  $('#palette-input').blur()
  window.pv.ui.layout({ palette: false })
}

function filterPalette(q) {
  const s = q.trim().toLowerCase()
  palItems = COMMANDS.filter(c => c.title.toLowerCase().includes(s))
  if (s && (s.includes('.') || s.startsWith('http'))) {
    palItems.unshift({ id: 'url', title: `Open ${q.trim()}`, keys: '', run: () => window.pv.nav.go(q.trim()) })
  }
  if (s && !palItems.some(c => c.id === 'url')) {
    palItems.push({ id: 'search', title: `Search the web for "${q.trim()}"`, keys: '', run: () => window.pv.nav.go(q.trim()) })
  }
  palIndex = 0
  renderPalette()
}

function renderPalette() {
  const list = $('#palette-list')
  list.innerHTML = ''
  palItems.slice(0, 9).forEach((c, i) => {
    const row = document.createElement('div')
    row.className = 'pal' + (i === palIndex ? ' sel' : '')
    const t = document.createElement('span')
    t.textContent = c.title
    const k = document.createElement('span')
    k.className = 'pal-keys'
    k.textContent = c.keys || ''
    row.append(t, k)
    row.onmousedown = (e) => { e.preventDefault(); runPalette(i) }
    list.appendChild(row)
  })
  if (!palItems.length) {
    const d = document.createElement('div')
    d.className = 'pal-empty'
    d.textContent = 'No matching command'
    list.appendChild(d)
  }
}

function runPalette(i) {
  const cmd = palItems[i]
  closePalette()
  if (cmd) cmd.run()
}

$('#palette-input').addEventListener('input', (e) => filterPalette(e.target.value))
$('#palette-input').addEventListener('keydown', (e) => {
  if (e.key === 'Escape') closePalette()
  else if (e.key === 'Enter') runPalette(palIndex)
  else if (e.key === 'ArrowDown') { e.preventDefault(); palIndex = Math.min(palIndex + 1, Math.min(palItems.length, 9) - 1); renderPalette() }
  else if (e.key === 'ArrowUp') { e.preventDefault(); palIndex = Math.max(palIndex - 1, 0); renderPalette() }
})
$('#palette').addEventListener('mousedown', (e) => { if (e.target.id === 'palette') closePalette() })

// ------------------------------------------------------------------ find
function showFind() {
  $('#findbar').classList.remove('hidden')
  syncChrome()
  $('#find-input').focus()
  $('#find-input').select()
}
$('#find-close').onclick = () => {
  $('#findbar').classList.add('hidden')
  window.pv.find.stop()
  syncChrome()
}
$('#find-input').addEventListener('input', async (e) => {
  const v = e.target.value
  if (!v) { $('#find-count').textContent = ''; window.pv.find.stop(); return }
  const r = await window.pv.find.start(v)
  if (r && typeof r === 'object') $('#find-count').textContent = `${r.activeMatchOrdinal}/${r.matches}`
})
$('#find-input').addEventListener('keydown', (e) => {
  if (e.key === 'Escape') $('#find-close').click()
})

// ------------------------------------------------------------------ keyboard
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && !$('#menu').classList.contains('hidden')) {
    $('#menu').classList.add('hidden')
    $('#btn-menu').setAttribute('aria-expanded', 'false')
    $('#btn-menu').focus()
    syncChrome()
    return
  }
  const mod = e.ctrlKey || e.metaKey
  if (mod && e.key.toLowerCase() === 'k') { e.preventDefault(); openPalette() }
  else if (mod && e.key === '\\') { e.preventDefault(); window.pv.view.split() }
  else if (mod && e.shiftKey && e.key.toLowerCase() === 't') { e.preventDefault(); window.pv.tab.reopen() }
  else if (mod && e.shiftKey && e.key.toLowerCase() === 'p') { e.preventDefault(); window.pv.win.private() }
  else if (mod && e.key.toLowerCase() === 't') { e.preventDefault(); window.pv.tab.new() }
  else if (mod && e.key.toLowerCase() === 'w') { e.preventDefault(); if (state.activeId) window.pv.tab.close(state.activeId) }
  else if (mod && e.key.toLowerCase() === 'l') { e.preventDefault(); om.focus(); om.select() }
  else if (mod && e.key.toLowerCase() === 'f') { e.preventDefault(); showFind() }
  else if (mod && e.key.toLowerCase() === 'r') { e.preventDefault(); window.pv.nav.reload() }
  else if (mod && e.key === '+') { e.preventDefault(); window.pv.zoom.in() }
  else if (mod && e.key === '-') { e.preventDefault(); window.pv.zoom.out() }
  else if (mod && e.key === '0') { e.preventDefault(); window.pv.zoom.reset() }
  else if (mod && e.key.toLowerCase() === 'd') {
    e.preventDefault()
    $('#btn-bookmark').click()
  }
  else if (e.key === 'F5') { e.preventDefault(); window.pv.nav.reload() }
})

// ------------------------------------------------------------------ toast
let toastTimer
function toast(msg) {
  const t = $('#toast')
  if (!t || !msg) return
  t.textContent = msg
  t.classList.remove('hidden')
  clearTimeout(toastTimer)
  toastTimer = setTimeout(() => t.classList.add('hidden'), 2000)
}

// one more rail button: Performance (CPU/RAM/bandwidth), the speedometer slot

(function addGxButton() {
  const rail = document.getElementById('rail')
  if (!rail) return
  const b = document.createElement('button')
  b.className = 'rail-btn'
  b.dataset.panel = 'hot'
  b.title = 'Performance: hot tabs, memory, bandwidth cap'
  b.setAttribute('aria-label', 'Performance')
  b.innerHTML = '<svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 20a8 8 0 1 1 8-8"/><path d="M12 12l5-3"/></svg>'
  b.onclick = () => openPanel('hot', b)
  rail.appendChild(b)
})()

// ---------------------------------------------------------------- update banner
// A verified update is offered in a bar, not a modal. "Later" hides it for this run, the
// close button remembers this version so it is never offered again, and a newer version
// still shows up. Nothing downloads until Install is pressed.
const updateBanner = $('#updateBanner')
let updateHiddenThisRun = false

function paintUpdateBanner(state) {
  if (!updateBanner) return
  const show = !!(state && state.available && state.version && !state.dismissed && !updateHiddenThisRun)
  updateBanner.classList.toggle('hidden', !show)
  // The bar is an overlay, so the rail and tab strip are pushed down while it is shown.
  const appEl = document.getElementById('app')
  if (appEl) appEl.classList.toggle('has-update-banner', show)
  if (!show) return
  const text = $('#updateBannerText')
  if (text) text.textContent = 'PsychoVault ' + state.version + ' is available' + (state.notes ? ' - ' + state.notes.slice(0, 90) + (state.notes.length > 90 ? '\u2026' : '') : '')
  const install = $('#updateBannerInstall')
  if (install) install.disabled = false
  const progress = $('#updateBannerProgress')
  if (progress) progress.textContent = ''
}

async function refreshUpdateBanner() {
  try { paintUpdateBanner(await window.pv.updates.banner()) } catch { /* no updater on this build */ }
}

if (updateBanner) {
  $('#updateBannerLater').onclick = async () => {
    updateHiddenThisRun = true
    await window.pv.updates.later()
    paintUpdateBanner({ available: false })
  }
  $('#updateBannerClose').onclick = async () => {
    const r = await window.pv.updates.dismiss()
    if (r && r.ok) toast('Not offering ' + r.version + ' again')
    paintUpdateBanner({ available: false })
  }
  $('#updateBannerInstall').onclick = async () => {
    const install = $('#updateBannerInstall')
    const progress = $('#updateBannerProgress')
    if (install) install.disabled = true
    if (progress) progress.textContent = 'starting\u2026'
    let r = null
    try { r = await window.pv.updates.install() } catch (e) { r = { ok: false, error: String((e && e.message) || e) } }
    if (install) install.disabled = false
    if (progress) progress.textContent = ''
    if (r && r.ok) { paintUpdateBanner({ available: false }); return }
    if (r && r.reason === 'save-as-cancelled') return
    toast('Update not installed: ' + ((r && (r.error || r.reason)) || 'no reason given'))
  }
  window.pv.on('update-available', paintUpdateBanner)
  window.pv.on('update-progress', ({ message }) => {
    const progress = $('#updateBannerProgress')
    if (progress && message && !updateBanner.classList.contains('hidden')) progress.textContent = message
  })
  refreshUpdateBanner()
}

// initial paint
if (isPrivate) $('#private-badge').classList.remove('hidden')

window.pv.settings.get().then(() => {
  renderTabs()
  renderNav()
})
