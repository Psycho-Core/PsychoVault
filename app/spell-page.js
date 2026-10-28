// PsychoVault built-in spellcheck. This runs in the page's own world (main world, no Node).
//
// Why it exists: an Electron build has no dictionary machinery - setSpellCheckerEnabled only
// flips a flag, there is no downloadDictionary, <profile>/Dictionaries is not read, and the
// download URL is never fetched (all of that was verified against the APIs on this build). So
// the red underline, the word under the caret and the suggestions in the edit menu come from
// here instead, against the frequency list in spell-words.txt plus your own words.
(function () {
  if (window.__pvSpellBoot) { return }
  var DICT = new Set()
  var USER = new Set()
  var RANK = new Map()
  var IDX = null
  var ON = false
  var CUR = null
  var MIRROR = null
  var TIMER = null
  var BAD = 0

  function isWordChar(c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === "'" }

  function tokens(text) {
    var out = []
    var i = 0
    var n = text.length
    while (i < n) {
      while (i < n && !isWordChar(text.charAt(i))) i++
      var s = i
      while (i < n && isWordChar(text.charAt(i))) i++
      if (i > s) out.push({ raw: text.slice(s, i), start: s, end: i })
    }
    return out
  }

  function known(raw) {
    var w = raw.replace(/'/g, '')
    if (w.length < 2) return true
    if (/[0-9]/.test(raw)) return true
    if (/^[A-Z]{2,}$/.test(raw)) return true
    var l = w.toLowerCase()
    if (DICT.has(l) || USER.has(l)) return true
    var cuts = [l.replace(/ies$/, 'y'), l.replace(/(es|s)$/, ''), l.replace(/ying$/, 'y'),
      l.replace(/(ing|ied)$/, ''), l.replace(/ed$/, ''), l.replace(/e$/, ''),
      l.replace(/(ly|ness|ment|tion)$/, '')]
    for (var i = 0; i < cuts.length; i++) {
      var c = cuts[i]
      if (c && c.length > 2 && (DICT.has(c) || USER.has(c))) return true
    }
    return false
  }

  function dl(a, b) {            // Damerau-Levenshtein, bounded for ranking only
    var la = a.length
    var lb = b.length
    if (!la) return lb
    if (!lb) return la
    if (Math.abs(la - lb) > 2) return 9
    var d = []
    var i
    var j
    for (i = 0; i <= la; i++) { d.push(new Array(lb + 1)); d[i][0] = i }
    for (j = 0; j <= lb; j++) d[0][j] = j
    for (i = 1; i <= la; i++) {
      for (j = 1; j <= lb; j++) {
        var cost = a.charAt(i - 1) === b.charAt(j - 1) ? 0 : 1
        d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost)
        if (i > 1 && j > 1 && a.charAt(i - 1) === b.charAt(j - 2) && a.charAt(i - 2) === b.charAt(j - 1)) {
          d[i][j] = Math.min(d[i][j], d[i - 2][j - 2] + cost)
        }
      }
    }
    return d[la][lb]
  }

  function index() {            // deletion neighbourhood of the dictionary, built once
    if (IDX) return IDX
    IDX = new Map()
    DICT.forEach(function (w) {
      for (var i = -1; i < w.length; i++) {
        var key = i < 0 ? w : w.slice(0, i) + w.slice(i + 1)
        var cur = IDX.get(key)
        if (!cur) { cur = []; IDX.set(key, cur) }
        if (cur.length < 8) cur.push(w)
      }
    })
    return IDX
  }

  function suggest(raw) {
    var w = raw.toLowerCase().replace(/'/g, '')
    if (w.length < 2) return []
    var seen = new Set()
    var out = []
    function add(cand, dist) {
      if (!cand || seen.has(cand) || cand === w) return
      if (!DICT.has(cand) && !USER.has(cand)) return
      seen.add(cand)
      out.push({ word: cand, dist: dist, rank: RANK.has(cand) ? RANK.get(cand) : 99999 })
    }
    var i
    var j
    for (i = 0; i < w.length; i++) {
      add(w.slice(0, i) + w.slice(i + 1), 1)                                  // delete
      if (i + 1 < w.length) add(w.slice(0, i) + w.charAt(i + 1) + w.charAt(i) + w.slice(i + 2), 1)   // transpose
      for (j = 0; j < 26; j++) add(w.slice(0, i) + String.fromCharCode(97 + j) + w.slice(i + 1), 1)  // replace
      for (j = 0; j < 26; j++) add(w.slice(0, i) + String.fromCharCode(97 + j) + w.slice(i), 1)       // insert
    }
    if (out.length < 4) {
      var ix = index()
      for (i = -1; i < w.length; i++) {
        var key = i < 0 ? w : w.slice(0, i) + w.slice(i + 1)
        var list = ix.get(key)
        if (!list) continue
        for (j = 0; j < list.length; j++) add(list[j], dl(w, list[j]))
      }
    }
    out.sort(function (a, b) { return a.dist - b.dist || a.rank - b.rank })
    var keep = out.slice(0, 5).map(function (x) { return x.word })
    if (raw.charAt(0) === raw.charAt(0).toUpperCase() && raw.charAt(0) !== raw.charAt(0).toLowerCase()) {
      keep = keep.map(function (x) { return x.charAt(0).toUpperCase() + x.slice(1) })
    }
    return keep
  }

  function editable(el) {
    if (el.tagName === 'TEXTAREA') return !el.readOnly && !el.disabled
    if (el.tagName === 'INPUT') {
      if (el.readOnly || el.disabled) return false
      var t = (el.getAttribute('type') || 'text').toLowerCase()
      return t === 'text' || t === 'search' || t === 'url' || t === 'tel' || t === 'email' || t === ''
    }
    return false
  }

  function mirrorEl() {
    if (!MIRROR || !MIRROR.isConnected) {
      MIRROR = document.createElement('div')
      MIRROR.id = '__pv_spell_mirror'
      MIRROR.setAttribute('aria-hidden', 'true')
      MIRROR.style.cssText = 'position:absolute;display:none;background:transparent;color:transparent;pointer-events:none;z-index:2147483000;overflow:hidden'
      document.documentElement.appendChild(MIRROR)
    }
    return MIRROR
  }

  var COPY = ['fontFamily', 'fontSize', 'fontWeight', 'fontStyle', 'fontVariant', 'lineHeight',
    'letterSpacing', 'wordSpacing', 'textTransform', 'textIndent', 'textAlign', 'direction',
    'paddingTop', 'paddingRight', 'paddingBottom', 'paddingLeft', 'borderTopWidth', 'borderRightWidth',
    'borderBottomWidth', 'borderLeftWidth', 'boxSizing']

  function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') }

  function paint() {
    var el = CUR
    if (!ON || !editable(el)) { if (MIRROR) MIRROR.style.display = 'none'; BAD = 0; return }
    var m = mirrorEl()
    var cs = getComputedStyle(el)
    for (var i = 0; i < COPY.length; i++) { try { m.style[COPY[i]] = cs[COPY[i]] } catch (e) { /* vendor */ } }
    m.style.whiteSpace = el.tagName === 'INPUT' ? 'pre' : 'pre-wrap'
    m.style.overflowWrap = 'break-word'
    m.style.wordBreak = cs.wordBreak || 'normal'
    var r = el.getBoundingClientRect()
    m.style.width = Math.max(20, r.width) + 'px'
    m.style.height = Math.max(12, r.height) + 'px'
    m.style.left = (r.left + window.scrollX) + 'px'
    m.style.top = (r.top + window.scrollY) + 'px'
    m.style.border = '0'
    m.style.display = 'block'
    m.scrollTop = el.scrollTop || 0
    m.scrollLeft = el.scrollLeft || 0
    var text = el.value || ''
    var parts = []
    var at = 0
    BAD = 0
    var tks = tokens(text)
    for (var k = 0; k < tks.length; k++) {
      var t = tks[k]
      if (known(t.raw)) continue
      BAD++
      parts.push(esc(text.slice(at, t.start)))
      parts.push('<u style="text-decoration:underline wavy #ff5b5b;text-decoration-skip-ink:none;text-decoration-thickness:from-font">' + esc(t.raw) + '</u>')
      at = t.end
    }
    parts.push(esc(text.slice(at)) + '\n')
    m.innerHTML = parts.join('')
  }

  function reschedule() {
    if (TIMER) clearTimeout(TIMER)
    TIMER = setTimeout(function () { TIMER = null; paint() }, 180)
  }

  function attach(el) {
    if (CUR === el) return
    CUR = el
    el.addEventListener('input', reschedule, true)
    el.addEventListener('keyup', reschedule, true)
    el.addEventListener('scroll', function () { if (CUR === el && MIRROR) { MIRROR.scrollTop = el.scrollTop; MIRROR.scrollLeft = el.scrollLeft } }, true)
    el.addEventListener('blur', function () { if (MIRROR && CUR === el) MIRROR.style.display = 'none' }, true)
    paint()
  }

  function caretWord(el) {
    if (!editable(el)) return null
    var text = el.value || ''
    var pos = typeof el.selectionStart === 'number' ? el.selectionStart : 0
    var tks = tokens(text)
    for (var i = 0; i < tks.length; i++) {
      var t = tks[i]
      if (pos >= t.start && pos <= t.end) return known(t.raw) ? null : { raw: t.raw, start: t.start, end: t.end }
    }
    // right-click in Chromium does not always move the caret; fall back to the word nearest it
    var best = null
    var bestD = 1e9
    for (var j = 0; j < tks.length; j++) {
      var u = tks[j]
      if (known(u.raw)) continue
      var d = Math.abs(((u.start + u.end) / 2) - pos)
      if (d < bestD) { bestD = d; best = u }
    }
    return best && bestD < 40 ? { raw: best.raw, start: best.start, end: best.end } : null
  }

  window.__pvSpellBoot = function (cfg) {
    cfg = cfg || {}
    ON = !!cfg.on
    if (cfg.words && (!DICT.size || cfg.reseed)) {
      DICT = new Set()
      RANK = new Map()
      for (var i = 0; i < cfg.words.length; i++) { DICT.add(cfg.words[i]); RANK.set(cfg.words[i], i) }
      IDX = null
    }
    USER = new Set()
    var u = cfg.user || []
    for (var j = 0; j < u.length; j++) USER.add(String(u[j]).toLowerCase())
    if (!ON) { if (MIRROR) MIRROR.style.display = 'none'; BAD = 0; return 'off' }
    if (document.activeElement) attach(document.activeElement)
    paint()
    return 'ok'
  }
  window.__pvSpellGet = function () {
    if (!ON) return null
    var el = document.activeElement
    if (!editable(el)) el = CUR
    var w = caretWord(el)
    if (!w) return null
    return { word: w.raw, start: w.start, end: w.end, suggestions: suggest(w.raw), field: el.tagName.toLowerCase() }
  }
  window.__pvSpellApply = function (start, end, word) {
    var el = document.activeElement
    if (!editable(el)) el = CUR
    if (!editable(el) || typeof start !== 'number') return false
    var v = el.value || ''
    el.value = v.slice(0, start) + word + v.slice(end)
    var pos = start + word.length
    try { el.setSelectionRange(pos, pos) } catch (e) { /* type refuses selection */ }
    el.dispatchEvent(new Event('input', { bubbles: true }))
    paint()
    return true
  }
  window.__pvSpellAdd = function (word) {
    var w = String(word || '').toLowerCase()
    if (!w) return false
    USER.add(w)
    paint()
    return true
  }
  window.__pvSpellState = function () {
    return { on: ON, dict: DICT.size, user: USER.size, bad: BAD, field: CUR ? CUR.tagName.toLowerCase() : '', has: editable(document.activeElement) || editable(CUR) }
  }
  document.addEventListener('focusin', function (e) { if (ON && editable(e.target)) attach(e.target) }, true)
  document.addEventListener('contextmenu', function (e) { if (ON && editable(e.target)) { attach(e.target); reschedule() } }, true)
  window.addEventListener('resize', function () { if (ON) reschedule() }, true)
})()
