// Android settings document. Only controls with a real native handler are enabled.
(function () {
  'use strict';
  if (location.origin !== 'chrome://psychovault-pages' || location.pathname !== '/settings.html' || !window.PsychoVaultNative) return;
  const isPrivate = new URLSearchParams(location.search).get('private') === '1';
  const transport = PsychoNativeTransport.create({native:window.PsychoVaultNative, resolver:PsychoNavigationResolver, isPrivate, page:true});
  window.PsychoNativeTransport = transport;
  window.pv = PsychoInternalPageBridge.create(transport);
  const s = id => document.getElementById(id);
  const status = document.createElement('div');
  status.className = 'hint'; status.setAttribute('aria-live', 'polite'); status.id = 'android-settings-status';
  document.querySelector('.wrap').prepend(status);

  // Category navigation is local to this document and does not need native storage.
  const buttons = [...document.querySelectorAll('[data-category]')];
  const panels = [...document.querySelectorAll('[data-category-panel]')];
  function select(key) {
    const panel = panels.find(p => p.dataset.categoryPanel === key) || panels[0];
    if (!panel) return;
    for (const b of buttons) { if (b.dataset.category === panel.dataset.categoryPanel) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current'); }
    for (const p of panels) p.hidden = p !== panel;
    const title = document.querySelector('.category-title'), desc = document.querySelector('.category-description');
    if (title) title.textContent = panel.dataset.title || 'Settings';
    if (desc) desc.textContent = panel.dataset.description || '';
    try { history.replaceState(null, '', '#' + panel.dataset.categoryPanel); } catch {}
  }
  for (const b of buttons) b.addEventListener('click', () => select(b.dataset.category));
  select(location.hash.slice(1) || 'general');

  const SUPPORTED = new Set(['search','customSearchEngines','customSearchSave','homepage','remember','theme','accent',
    'hideSidebar','highContrast','reduceMotion','showTabCloseButtons','tiles','tileAdd','tileSave']);
  // Disable every unsupported control explicitly; no fabricated success states.
  for (const el of document.querySelectorAll('input,select,textarea,button')) {
    if (el.hasAttribute('data-category') || SUPPORTED.has(el.id)) continue;
    el.disabled = true;
    if (!el.title) el.title = 'Not connected on Android yet';
  }
  const unsupportedNote = document.createElement('p');
  unsupportedNote.className = 'hint';
  unsupportedNote.textContent = 'Android source build: only the enabled controls below are connected. Disabled controls are not implemented yet.';
  document.querySelector('.settings-main').prepend(unsupportedNote);

  let settings = {};
  function paint(st) {
    settings = st;
    document.body.dataset.theme = st.theme || 'midnight';
    document.body.classList.toggle('high-contrast', !!st.highContrast);
    document.body.classList.toggle('reduce-motion', !!st.reduceMotion);
    for (const id of ['remember','hideSidebar','highContrast','reduceMotion','showTabCloseButtons']) if (s(id)) s(id).checked = !!st[id];
    for (const id of ['search','theme','accent']) if (s(id)) s(id).value = st[id] || s(id).options[0].value;
    if (s('homepage')) s('homepage').value = st.homepage || 'psychovault://newtab';
    if (s('customSearchEngines')) s('customSearchEngines').value = (st.customSearchEngines || []).map(e => [e.shortcut, e.name, e.url].join(' | ')).join('\n');
    renderTiles(st.speedDial || []);
  }
  async function save(patch) {
    try { paint(await window.pv.settings.set(patch)); status.textContent = 'Saved.'; }
    catch (e) { status.textContent = e.message || 'Could not save settings.'; paint(await window.pv.settings.get()); }
  }
  function renderTiles(list) {
    const wrap = s('tiles'); if (!wrap) return; wrap.replaceChildren();
    for (const t of list) {
      const row = document.createElement('div'); row.className = 'tile-row';
      const name = document.createElement('input'); name.className = 't'; name.value = t.title || ''; name.placeholder = 'Name';
      const url = document.createElement('input'); url.className = 'u'; url.value = t.url || ''; url.placeholder = 'https://...';
      const del = document.createElement('button'); del.className = 'mini'; del.textContent = 'Remove'; del.onclick = () => row.remove();
      row.append(name, url, del); wrap.appendChild(row);
    }
  }
  for (const id of ['remember','hideSidebar','highContrast','reduceMotion','showTabCloseButtons']) {
    const el = s(id); if (el) el.onchange = () => save({[id]: el.checked});
  }
  for (const id of ['search','theme','accent']) { const el = s(id); if (el) el.onchange = () => save({[id]: el.value}); }
  if (s('homepage')) s('homepage').onchange = () => save({homepage: s('homepage').value.trim() || 'psychovault://newtab'});
  if (s('customSearchSave')) s('customSearchSave').onclick = () => {
    const lines = s('customSearchEngines').value.split('\n').map(x => x.trim()).filter(Boolean);
    const engines = [], seen = new Set();
    for (const line of lines) {
      const parts = line.split('|').map(x => x.trim());
      let valid = parts.length === 3 && /^[a-z][a-z0-9_-]{0,19}$/i.test(parts[0]) && parts[1].length > 0 && parts[1].length <= 80 && parts[2].length <= 2048 && parts[2].includes('%s');
      try { const a = new URL(parts[2].replaceAll('%s', 'sample')); const b = new URL(parts[2].replaceAll('%s', 'different')); valid = valid && a.protocol === 'https:' && !a.username && !a.password && a.origin === b.origin; } catch { valid = false; }
      if (!valid || seen.has(parts[0].toLowerCase())) { status.textContent = 'Invalid or duplicate shortcut: ' + line; return; }
      seen.add(parts[0].toLowerCase()); engines.push({shortcut: parts[0], name: parts[1], url: parts[2]});
    }
    save({customSearchEngines: engines});
  };
  if (s('tileAdd')) s('tileAdd').onclick = () => {
    const rows = [...s('tiles').querySelectorAll('.tile-row')].map(r => ({title: r.querySelector('.t').value, url: r.querySelector('.u').value}));
    rows.push({title: 'New', url: 'https://'}); renderTiles(rows);
  };
  if (s('tileSave')) s('tileSave').onclick = () => save({speedDial: [...s('tiles').querySelectorAll('.tile-row')].map(r => ({title: r.querySelector('.t').value.trim(), url: r.querySelector('.u').value.trim()})).filter(t => t.title && t.url)});

  transport.start().then(() => window.pv.settings.get()).then(paint).catch(e => { status.textContent = e.message || 'Android settings are unavailable.'; });
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
