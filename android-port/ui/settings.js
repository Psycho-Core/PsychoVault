// Android settings document. Controls are enabled only where a native handler exists.
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
    try { history.replaceState(null, '', '#' + panel.dataset.categoryPanel); localStorage.setItem('psychovault.settings.category', panel.dataset.categoryPanel); } catch {}
    const active = buttons.find(b => b.dataset.category === panel.dataset.categoryPanel);
    if (active && typeof active.scrollIntoView === 'function') active.scrollIntoView({block:'nearest', inline:'center'});
  }
  for (const b of buttons) b.addEventListener('click', () => select(b.dataset.category));
  window.addEventListener('hashchange', () => select(location.hash.slice(1)));
  let savedCategory = ''; try { savedCategory = localStorage.getItem('psychovault.settings.category') || ''; } catch {}
  select(location.hash.slice(1) || savedCategory || 'general');

  const BOOLS = ['remember','hideSidebar','highContrast','reduceMotion','showTabCloseButtons','restore',
    'blockAds','blockUseBundled','workspacesEnabled','blockAutoplay','blockThirdPartyCookies','sendDNT',
    'batterySaver','batteryAuto','vaultEnabled','enhanceVideo','enhanceImages','askSaveLocation','panicEnabled','railIcons',
    'spellCheck','spellBuiltin','stripReferrer','uiSounds','downloadNotify','mediaKeys','mediaAutoPause'];
  const SELECTS = ['search','theme','accent','cpuLimit','webRtc','doh','translateTarget','vaultLockMin','clearOnExit','netLimit'];
  const TEXTS = ['homepage','blockAllowSites','blockCustomDomains','sleepExclude','panicUrl','dohTemplate','denyPerms','modCss','spellLangs','spellDictUrl'];
  const SUPPORTED = new Set([...BOOLS, ...SELECTS, ...TEXTS,
    'customSearchEngines','customSearchSave','tiles','tileAdd','tileSave','sleepTabsMin','blockSaveLists',
    'extList','extReload','extAddFolder','extAddCrx','protonVpnOpen','protonVpnRestore','protonVpnStatus','vpnCheckOpen',
    'vstat','vunlock','vpw','vgo','vmaster','vmasterHint','vnew','vset','vBackup','vRestore','vImport',
    'vbackupPassword','vbackupConfirm','vTransferMsg','vlist','eSite','eUser','ePass','eGen','eSave','vLock','vRefresh','vnote',
    'downloadDir','downloadDirPick','soundVol','permOrigin','permLoad','permCamera','permMicrophone','permLocation','permNotifications','permSave','permRemove',
    'spellUserWords','spellWordsSave','spellWordsRefresh','contList','contAdd','contSave','updateCheckNow','updateDismiss',
    'clear','dataRefresh','dataFiles','dataCookies','modSave','modClear','svcList','svcAdd','svcSave','svcCat',
    'openErrorLog','errorLogResult','proxySelfTest','proxyStatus','proxySelfTestResult','proxyStatusResult','diagnosticsRefresh','diagnosticsCopy','diagnosticsReport','diagnosticsResult','appVersion','appElectron']);
  // The copied desktop switches rely on adjacent text; give Android TalkBack an explicit name.
  for (const input of document.querySelectorAll('.switch input[type=checkbox]')) {
    const label = input.closest('.row')?.querySelector('.lbl')?.textContent?.trim();
    if (label && !input.getAttribute('aria-label')) input.setAttribute('aria-label', label);
  }
  const PLACEHOLDERS = {
    relaunch: 'Desktop relaunch is not connected on Android. Restart the app from Android if needed.',
    vforget: 'No OS-keychain vault fallback is enabled on Android; the master-password vault remains available.',
    updateToken: 'Public GitHub releases do not need an update token.',
    updateTokenSave: 'Public GitHub releases do not need an update token.',
    updateTokenForget: 'Public GitHub releases do not need an update token.'
  };
  for (const el of document.querySelectorAll('input,select,textarea,button')) {
    if (el.hasAttribute('data-category') || el.hasAttribute('data-clear') || SUPPORTED.has(el.id)) continue;
    el.disabled = true;
    el.dataset.androidPlaceholder = 'true';
    el.classList.add('android-placeholder');
    el.title = PLACEHOLDERS[el.id] || 'Not connected on Android yet - visible placeholder';
    if (Object.hasOwn(PLACEHOLDERS, el.id)) {
      el.hidden = false; el.removeAttribute('hidden');
      for (let parent = el.parentElement; parent && parent !== document.body; parent = parent.parentElement) { if (parent.id === 'restartNote') parent.style.display = 'block'; }
      const badge = document.createElement('span'); badge.className = 'android-placeholder-note'; badge.textContent = 'Not connected on Android';
      el.insertAdjacentElement('afterend', badge);
    }
  }
  const note = document.createElement('p');
  note.className = 'hint';
  note.textContent = 'Android source build: enabled controls have native handlers. Disabled controls are visible placeholders for desktop-only features. Containers use ephemeral off-the-record profiles, download folders are limited to Chromium\'s available directories, media keys depend on Android routing, and the dictionary mirror must use HTTPS. Proton connection state must be checked inside Proton.';
  document.querySelector('.settings-main').prepend(note);
  if (isPrivate) {
    const privateNote = document.createElement('p'); privateNote.className = 'hint';
    privateNote.textContent = 'This is a private profile. Settings are read-only here; private windows do not save preferences.';
    document.querySelector('.settings-main').prepend(privateNote);
  }

  let settings = {};
  function renderSearchOptions(st) {
    const el = s('search'); if (!el) return;
    for (const option of [...el.options]) if (option.value.startsWith('custom:')) option.remove();
    for (const engine of st.customSearchEngines || []) { const option = document.createElement('option'); option.value = 'custom:' + engine.shortcut; option.textContent = engine.name + ' (' + engine.shortcut + ')'; el.appendChild(option); }
  }
  function paint(st) {
    settings = st;
    document.body.dataset.theme = st.theme || 'midnight';
    document.body.classList.toggle('high-contrast', !!st.highContrast);
    document.body.classList.toggle('reduce-motion', !!st.reduceMotion);
    for (const id of BOOLS) if (s(id)) s(id).checked = !!st[id];
    renderSearchOptions(st);
    for (const id of SELECTS) if (s(id)) s(id).value = String(st[id] === undefined ? s(id).options[0].value : st[id]);
    for (const id of TEXTS) if (s(id)) s(id).value = st[id] || '';
    if (s('homepage')) s('homepage').value = st.homepage || 'psychovault://newtab';
    if (s('sleepTabsMin')) s('sleepTabsMin').value = String(st.sleepTabsMin || 0);
    if (s('soundVol')) s('soundVol').value = String(st.soundVol === undefined ? 0.4 : st.soundVol);
    if (s('customSearchEngines')) s('customSearchEngines').value = (st.customSearchEngines || []).map(e => [e.shortcut, e.name, e.url].join(' | ')).join('\n');
    renderTiles(st.speedDial || []);
    renderServices(st.services || []);
    if (s('appVersion')) s('appVersion').textContent = 'PsychoVault Android';
    if (s('appElectron')) s('appElectron').textContent = 'Chromium 156';
    if (s('downloadDir')) { s('downloadDir').value = st.downloadDir || 'Chromium default directory'; s('downloadDir').readOnly = true; }
    if (s('downloadDirPick')) { s('downloadDirPick').disabled = isPrivate; s('downloadDirPick').title = 'Choose among the download directories Chromium exposes on this device.'; }
    if (s('spellUserWords')) s('spellUserWords').value = (st.spellUserWords || []).join('\n');
    if (s('spellStatus')) s('spellStatus').textContent = 'Spellcheck preferences are sent to Chromium. User words are written to the profile custom dictionary.';
    if (s('spellAvail')) s('spellAvail').textContent = 'HTTPS dictionary mirrors supply <lang>.bdic files; platform languages may still use the Android spellchecker.';
    paintPermissionSites(st);
    renderContainers(st.containers || []);
    if (isPrivate) for (const el of document.querySelectorAll('input,select,textarea,button')) if (!el.hasAttribute('data-category')) el.disabled = true;
  }
  async function save(patch) {
    if (isPrivate) { status.textContent = 'Private profiles do not save settings.'; return; }
    try { paint(await window.pv.settings.set(patch)); status.textContent = 'Saved.'; }
    catch (e) { status.textContent = e.message || 'Could not save settings.'; try { paint(await window.pv.settings.get()); } catch {} }
  }
  for (const id of BOOLS) { const el = s(id); if (el) el.onchange = () => save({[id]: el.checked}); }
  for (const id of SELECTS) { const el = s(id); if (el) el.onchange = () => save({[id]: (id === 'vaultLockMin' || id === 'netLimit') ? Number(el.value) : el.value}); }
  for (const id of TEXTS) { const el = s(id); if (el && id !== 'blockAllowSites' && id !== 'blockCustomDomains') el.onchange = () => save({[id]: el.value.trim()}); }
  if (s('sleepTabsMin')) s('sleepTabsMin').onchange = () => save({sleepTabsMin: Math.max(0, Math.min(1440, Number(s('sleepTabsMin').value) || 0))});
  if (s('soundVol')) s('soundVol').onchange = () => save({soundVol: Math.max(0, Math.min(1, Number(s('soundVol').value) || 0))});
  if (s('blockSaveLists')) s('blockSaveLists').onclick = async () => {
    await save({blockAllowSites: s('blockAllowSites').value, blockCustomDomains: s('blockCustomDomains').value});
    if (s('blockListsMsg')) s('blockListsMsg').textContent = 'Blocker lists saved. Counts apply to new requests; they are not a leak test.';
  };

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

  function renderServices(list) {
    const wrap = s('svcList'); if (!wrap) return; wrap.replaceChildren();
    for (const t of list) {
      const row = document.createElement('div'); row.className = 'tile-row';
      const name = document.createElement('input'); name.className = 't'; name.value = t.name || ''; name.placeholder = 'Name';
      const url = document.createElement('input'); url.className = 'u'; url.value = t.url || ''; url.placeholder = 'https://...';
      const del = document.createElement('button'); del.className = 'mini'; del.textContent = 'Remove'; del.onclick = () => row.remove();
      row.append(name, url, del); wrap.appendChild(row);
    }
  }
  function servicesValue() {
    return [...s('svcList').querySelectorAll('.tile-row')].map(r => ({name: r.querySelector('.t').value.trim(), url: r.querySelector('.u').value.trim()})).filter(t => t.name && t.url);
  }
  if (s('svcAdd')) s('svcAdd').onclick = () => { const rows = servicesValue(); rows.push({name: 'New', url: 'https://'}); renderServices(rows); };
  if (s('svcSave')) s('svcSave').onclick = () => save({services: servicesValue()});
  async function paintCatalogue() {
    const wrap = s('svcCat'); if (!wrap) return;
    try {
      const items = await window.pv.rail.catalogue(); wrap.replaceChildren();
      for (const item of items) {
        const button = document.createElement('button'); button.className = 'mini'; button.textContent = (item.on ? '✓ ' : '') + item.name;
        button.disabled = isPrivate;
        button.onclick = async () => { try { await window.pv.rail.toggle(item.name); paintCatalogue(); const st = await window.pv.settings.get(); renderServices(st.services || []); } catch (e) { status.textContent = e.message; } };
        wrap.appendChild(button);
      }
    } catch (e) { wrap.textContent = e.message || 'Catalogue unavailable.'; }
  }
  if (s('modSave')) s('modSave').onclick = () => save({modCss: s('modCss') ? s('modCss').value : ''});
  if (s('modClear')) s('modClear').onclick = () => { if (s('modCss')) s('modCss').value = ''; save({modCss: ''}); };
  if (s('clear')) s('clear').onclick = async () => { try { await window.pv.history.clear(); status.textContent = 'History cleared.'; } catch (e) { status.textContent = e.message || 'Could not clear history.'; } };
  async function refreshData() {
    try {
      const r = await window.pv.data.report();
      if (s('dataFiles')) { s('dataFiles').replaceChildren(); for (const file of r.files || []) { const row = document.createElement('tr'); const name = document.createElement('td'); name.textContent = file.name; const size = document.createElement('td'); size.textContent = String(file.bytes) + ' bytes'; row.append(name, size); s('dataFiles').appendChild(row); } if (!(r.files || []).length) { const row = document.createElement('tr'); const cell = document.createElement('td'); cell.colSpan = 2; cell.textContent = r.note || 'No PsychoVault-owned files were listed.'; row.appendChild(cell); s('dataFiles').appendChild(row); } }
      if (s('dataCookies')) s('dataCookies').textContent = r.cookies == null ? 'Cookie counts are not exposed by Android.' : String(r.cookies) + ' cookies';
    } catch (e) { if (s('dataCookies')) s('dataCookies').textContent = e.message || 'Storage report unavailable.'; }
  }
  if (s('dataRefresh')) s('dataRefresh').onclick = refreshData;
  for (const button of document.querySelectorAll('[data-clear]')) button.onclick = async () => {
    const what = button.dataset.clear;
    try { await window.pv.data.clear({[what]: true}); status.textContent = 'Cleared ' + what + '.'; refreshData(); }
    catch (e) { status.textContent = e.message || 'Could not clear ' + what + '.'; }
  };
  if (s('proxySelfTest')) { s('proxySelfTest').textContent = 'Check proxy configuration'; s('proxySelfTest').onclick = async () => { try { const r = await window.pv.proxy.selftest(); if (s('proxySelfTestResult')) s('proxySelfTestResult').textContent = r.message || (r.proxyConfigured ? 'Proxy preference is configured; routing is not verified.' : 'No extension proxy preference is configured.'); } catch (e) { status.textContent = e.message; } }; }
  if (s('proxyStatus')) s('proxyStatus').onclick = async () => { try { const r = await window.pv.proxy.status(); if (s('proxyStatusResult')) s('proxyStatusResult').textContent = r.proxyConfigured ? 'A proxy preference is configured. This does not prove Proton is connected or that DNS/WebRTC are leak-free.' : 'No extension proxy preference is configured.'; } catch (e) { status.textContent = e.message; } };
  async function refreshDiagnostics() {
    try { const r = await window.pv.app.diagnostics(); if (s('diagnosticsReport')) s('diagnosticsReport').value = Object.entries(r).map(([k, v]) => k + ': ' + v).join('\n'); }
    catch (e) { if (s('diagnosticsResult')) s('diagnosticsResult').textContent = e.message || 'Diagnostics unavailable.'; }
  }
  if (s('diagnosticsRefresh')) s('diagnosticsRefresh').onclick = refreshDiagnostics;
  if (s('diagnosticsCopy')) s('diagnosticsCopy').onclick = async () => { try { await window.pv.app.diagnosticsCopy(); if (s('diagnosticsResult')) s('diagnosticsResult').textContent = 'Report copied.'; } catch (e) { status.textContent = e.message; } };

  async function paintExtensions() {
    const box = s('extList'); if (!box) return;
    try {
      const items = await window.pv.extensions.list();
      box.replaceChildren();
      if (!items.length) { box.textContent = 'No extensions installed.'; return; }
      for (const x of items) {
        const row = document.createElement('div'); row.className = 'ext-item';
        const head = document.createElement('div'); head.className = 'ext-head';
        const name = document.createElement('span'); name.className = 'ext-name'; name.textContent = x.name || x.id;
        const ver = document.createElement('span'); ver.className = 'ext-ver'; ver.textContent = 'v' + (x.version || '?') + ' - ' + (x.state || (x.enabled ? 'enabled' : 'disabled'));
        head.append(name, ver); row.appendChild(head);
        const acts = document.createElement('div'); acts.className = 'ext-acts';
        const toggle = document.createElement('button'); toggle.className = 'mini'; toggle.textContent = x.enabled ? 'Disable' : 'Enable';
        toggle.disabled = isPrivate;
        toggle.onclick = async () => { try { await window.pv.extensions.toggle({id: x.id, enabled: !x.enabled}); paintExtensions(); } catch (e) { status.textContent = e.message; } };
        const remove = document.createElement('button'); remove.className = 'mini'; remove.textContent = 'Remove';
        remove.disabled = isPrivate;
        remove.onclick = async () => { try { await window.pv.extensions.remove(x.id); paintExtensions(); } catch (e) { status.textContent = e.message; } };
        acts.append(toggle, remove);
        if (x.popup) { const open = document.createElement('button'); open.className = 'mini'; open.textContent = 'Open'; open.disabled = isPrivate; open.onclick = () => window.pv.extensions.openpage({id: x.id, page: x.popup}).catch(e => status.textContent = e.message); acts.append(open); }
        if (x.options) { const options = document.createElement('button'); options.className = 'mini'; options.textContent = 'Options'; options.disabled = isPrivate; options.onclick = () => window.pv.extensions.openpage({id: x.id, page: x.options}).catch(e => status.textContent = e.message); acts.append(options); }
        row.appendChild(acts); box.appendChild(row);
      }
    } catch (e) { box.textContent = e.message || 'Extensions unavailable.'; }
  }
  if (s('extReload')) s('extReload').onclick = paintExtensions;
  if (s('protonVpnOpen')) s('protonVpnOpen').onclick = async () => {
    try {
      const st = await window.pv.extensions.status();
      const proton = st.proton || (st.items || []).find(x => x.builtin === 'proton-vpn');
      if (!proton) { status.textContent = 'Proton VPN is not installed. Use Restore / reinstall Proton VPN.'; return; }
      if (!proton.enabled || !proton.loaded) { status.textContent = 'Proton VPN is not enabled and loaded. Check the extension list below.'; return; }
      await window.pv.extensions.openpage({id: proton.id, page: proton.popup || 'popup.html'});
      status.textContent = 'Proton opened. Connect inside Proton; protection is not verified.';
    } catch (e) { status.textContent = e.message || 'Could not open Proton VPN.'; }
  };
  if (s('protonVpnStatus')) s('protonVpnStatus').onclick = async () => {
    try { const st = await window.pv.extensions.status(); status.textContent = st.lastError ? 'Extension engine: ' + st.lastError : 'Extension registry refreshed. Proton connection state must be checked inside Proton.'; paintExtensions(); }
    catch (e) { status.textContent = e.message; }
  };
  if (s('protonVpnRestore')) s('protonVpnRestore').onclick = async () => { try { await window.pv.extensions.restorebuiltin(); paintExtensions(); status.textContent = 'Bundled extensions restore requested; check the list for Chromium load results.'; } catch (e) { status.textContent = e.message; } };
  if (s('vpnCheckOpen')) s('vpnCheckOpen').onclick = () => window.pv.vpncheck.open().catch(e => status.textContent = e.message);
  if (s('openErrorLog')) { s('openErrorLog').textContent = 'View Android error log'; s('openErrorLog').onclick = async () => { try { const r = await window.pv.data.openErrorLog(); const rows = (r.entries || []).map(x => new Date(x.at).toISOString() + ' ' + x.channel + ' ' + x.error); if (s('errorLogResult')) s('errorLogResult').textContent = r.message + (rows.length ? '\n' + rows.join('\n') : ''); } catch (e) { status.textContent = e.message; } }; }
  if (s('extAddFolder')) { s('extAddFolder').textContent = 'Import extension ZIP'; s('extAddFolder').onclick = async () => { try { const r = await window.pv.extensions.addfolder(); status.textContent = r.ok ? 'Extension installed.' : (r.error || 'Import failed.'); paintExtensions(); } catch (e) { status.textContent = e.message; } }; }
  if (s('extAddCrx')) { s('extAddCrx').textContent = 'Install CRX'; s('extAddCrx').onclick = async () => { try { const r = await window.pv.extensions.addcrx(); status.textContent = r.ok ? 'Extension installed.' : (r.error || 'Install failed.'); paintExtensions(); } catch (e) { status.textContent = e.message; } }; }

  function vnote(msg) { if (s('vnote')) s('vnote').textContent = msg || ''; }
  async function refreshVault() {
    try {
      const st = await window.pv.vault.status();
      if (s('vstat')) s('vstat').textContent = (st.enabled ? 'vault on' : 'vault off') + ' - ' + (st.method === 'master' ? 'master password, scrypt + AES-256-GCM' : 'no master password set') + ' - ' + (st.locked ? 'locked' : 'unlocked') + ' - ' + st.count + ' entries';
      if (s('vmaster')) s('vmaster').style.display = st.method === 'master' ? 'none' : 'block';
      if (s('vmasterHint')) s('vmasterHint').textContent = 'Android uses the master-password vault. Set a master password before saving entries; passwords are never written unencrypted.';
      if (s('vunlock')) s('vunlock').style.display = st.locked && st.method === 'master' ? 'block' : 'none';
      if (s('vforget')) { s('vforget').disabled = true; s('vforget').title = 'No OS-keychain vault fallback is enabled on Android.'; }
      if (st.recovery && !s('vRecover')) {
        const button = document.createElement('button'); button.className = 'mini'; button.id = 'vRecover'; button.textContent = 'Recover interrupted vault migration';
        button.onclick = async () => { const r = await window.pv.vault.recover(); vnote(r.ok ? 'Vault recovered from the migration backups.' : (r.reason || 'Recovery failed.')); refreshVault(); };
        s('vstat').after(button);
      }
      const r = await window.pv.vault.list(), wrap = s('vlist'); if (!wrap) return;
      wrap.replaceChildren();
      if (r.error) { const d = document.createElement('div'); d.className = 'note'; d.textContent = r.error === 'locked' ? 'Locked. Enter the master password above.' : 'The vault is off.'; wrap.appendChild(d); return; }
      if (!r.entries.length) { const d = document.createElement('div'); d.className = 'note'; d.textContent = 'No entries yet.'; wrap.appendChild(d); return; }
      for (const e of r.entries) {
        const row = document.createElement('div'); row.className = 'tile-row';
        const site = document.createElement('input'); site.value = e.site; site.readOnly = true;
        const user = document.createElement('input'); user.value = e.user || ''; user.readOnly = true;
        const pass = document.createElement('input'); pass.value = '••••••••'; pass.readOnly = true;
        const mk = (label, fn) => { const b = document.createElement('button'); b.className = 'mini'; b.textContent = label; b.onclick = fn; return b; };
        row.append(site, user, pass,
          mk('Show', async () => { const v = await window.pv.vault.reveal(e.id); pass.value = v.pass || 'locked'; }),
          mk('Copy', async () => { await window.pv.vault.copy(e.id, 'pass'); vnote('Password copied - the vault locks and the clipboard is cleared in 60 seconds.'); refreshVault(); }),
          mk('Delete', async () => { await window.pv.vault.remove(e.id); vnote('Deleted.'); refreshVault(); }));
        wrap.appendChild(row);
      }
    } catch (e) { vnote(e.message || 'Vault unavailable.'); }
  }
  if (s('vgo')) s('vgo').onclick = async () => { const r = await window.pv.vault.unlock(s('vpw').value); vnote(r.ok ? 'Unlocked.' : (r.reason || 'Unlock failed.')); if (r.ok) s('vpw').value = ''; refreshVault(); };
  if (s('vpw')) s('vpw').onkeydown = e => { if (e.key === 'Enter') s('vgo').click(); e.stopPropagation(); };
  if (s('vset')) s('vset').onclick = async () => { const r = await window.pv.vault.setMaster(s('vnew').value); vnote(r.ok ? 'Master password set.' : (r.reason || 'Failed.')); if (r.ok) s('vnew').value = ''; refreshVault(); };
  if (s('eGen')) s('eGen').onclick = async () => { s('ePass').value = await window.pv.vault.generate({mode:'words', words:4}); };
  if (s('eSave')) s('eSave').onclick = async () => { const r = await window.pv.vault.add({site:s('eSite').value, user:s('eUser').value, pass:s('ePass').value, note:''}); vnote(r.ok ? 'Saved.' : (r.reason || 'Not saved.')); if (r.ok) { s('eSite').value = ''; s('eUser').value = ''; s('ePass').value = ''; } refreshVault(); };
  if (s('vLock')) s('vLock').onclick = async () => { await window.pv.vault.lock(); vnote('Vault locked.'); refreshVault(); };
  if (s('vRefresh')) s('vRefresh').onclick = refreshVault;
  async function transfer(action) {
    const pw = s('vbackupPassword').value;
    if (action === 'backup' && (pw.length < 12 || pw !== s('vbackupConfirm').value)) { s('vTransferMsg').textContent = 'Use at least 12 characters and make sure both backup passwords match.'; return; }
    if (action === 'backup' && !confirm('Keep this backup password safe. If you lose it, this backup cannot be recovered. Continue?')) return;
    if (action === 'csv' && !confirm('CSV exports contain plaintext passwords. Import without overwriting existing entries?')) return;
    try {
      const r = await window.pv.vault.transfer(action, pw);
      s('vbackupPassword').value = ''; s('vbackupConfirm').value = '';
      s('vTransferMsg').textContent = r.ok ? (action === 'backup' ? 'Encrypted backup saved (' + r.count + ' entries).' : 'Imported ' + r.count + ' entries; skipped ' + r.skipped + '.') : (r.reason || 'Transfer failed.');
      refreshVault();
    } catch (e) { s('vTransferMsg').textContent = e.message || 'Transfer failed.'; }
  }
  if (s('vBackup')) s('vBackup').onclick = () => transfer('backup');
  if (s('vRestore')) s('vRestore').onclick = () => transfer('restore');
  if (s('vImport')) s('vImport').onclick = () => transfer('csv');

  const permFields = { camera: 'permCamera', microphone: 'permMicrophone', geolocation: 'permLocation', notifications: 'permNotifications' };
  let editingOrigin = '';
  function permissionOrigin(raw) {
    try { const u = new URL(String(raw).trim()); return ['http:', 'https:'].includes(u.protocol) && !u.username && !u.password ? u.origin : ''; } catch { return ''; }
  }
  function paintPermissionSites(st) {
    const box = s('permSaved'); if (!box) return; box.replaceChildren();
    const sites = Object.keys(st.sitePermissions || {}).sort();
    if (!sites.length) { box.textContent = 'No site-specific rules saved.'; return; }
    box.append('Saved sites: ');
    sites.forEach((site, i) => {
      if (i) box.append(' · ');
      const button = document.createElement('button'); button.className = 'mini'; button.textContent = site;
      button.onclick = () => { s('permOrigin').value = site; loadPermissionSite(); };
      box.appendChild(button);
    });
  }
  function loadPermissionSite() {
    const origin = permissionOrigin(s('permOrigin').value);
    if (!origin) { if (s('permMsg')) s('permMsg').textContent = 'Enter a valid http or https site, such as https://example.com.'; if (s('permEditor')) s('permEditor').hidden = true; return; }
    editingOrigin = origin; s('permOrigin').value = origin;
    const rule = (settings.sitePermissions || {})[origin] || {};
    for (const [key, id] of Object.entries(permFields)) if (s(id)) s(id).value = ['allow', 'deny'].includes(rule[key]) ? rule[key] : 'default';
    if (s('permEditor')) s('permEditor').hidden = false;
    if (s('permMsg')) s('permMsg').textContent = 'Editing ' + origin;
  }
  if (s('permLoad')) s('permLoad').onclick = loadPermissionSite;
  if (s('permSave')) s('permSave').onclick = async () => {
    if (!editingOrigin) return;
    const rules = { ...(settings.sitePermissions || {}) }; const rule = {};
    for (const [key, id] of Object.entries(permFields)) if (s(id) && s(id).value !== 'default') rule[key] = s(id).value;
    if (Object.keys(rule).length) rules[editingOrigin] = rule; else delete rules[editingOrigin];
    await save({ sitePermissions: rules }); if (s('permMsg')) s('permMsg').textContent = 'Saved rules for ' + editingOrigin;
  };
  if (s('permRemove')) s('permRemove').onclick = async () => {
    if (!editingOrigin) return;
    const rules = { ...(settings.sitePermissions || {}) }; delete rules[editingOrigin];
    await save({ sitePermissions: rules }); loadPermissionSite(); if (s('permMsg')) s('permMsg').textContent = 'Removed site-specific rules.';
  };

  function renderContainers(list) {
    const wrap = s('contList'); if (!wrap) return; wrap.replaceChildren();
    if (!list.length) { const hint = document.createElement('div'); hint.className = 'hint'; hint.textContent = 'No containers.'; wrap.appendChild(hint); return; }
    for (const c of list) {
      const row = document.createElement('div'); row.className = 'tile-row';
      const name = document.createElement('input'); name.className = 't'; name.value = c.name || ''; name.placeholder = 'Name';
      const color = document.createElement('input'); color.className = 'c'; color.value = c.color || '#38bdf8'; color.placeholder = '#rrggbb';
      const del = document.createElement('button'); del.className = 'mini'; del.textContent = 'Remove'; del.onclick = () => row.remove();
      row.append(name, color, del); wrap.appendChild(row);
    }
  }
  function containersValue() {
    return [...s('contList').querySelectorAll('.tile-row')].map(r => ({name: r.querySelector('.t').value.trim(), color: r.querySelector('.c').value.trim() || '#38bdf8'}));
  }
  if (s('contAdd')) s('contAdd').onclick = () => { const rows = containersValue(); rows.push({name: 'New', color: '#38bdf8'}); renderContainers(rows); };
  if (s('contSave')) s('contSave').onclick = () => save({containers: containersValue().filter(c => c.name)});

  if (s('downloadDirPick')) s('downloadDirPick').onclick = async () => {
    try { const picked = await window.pv.downloads.pickdir(); if (picked && typeof picked === 'string') { await save({downloadDir: picked}); status.textContent = 'Download folder saved.'; } }
    catch (e) { if (String(e.message || e) !== 'cancelled') status.textContent = e.message || 'Could not choose a folder.'; }
  };
  if (s('spellWordsSave')) s('spellWordsSave').onclick = async () => {
    const want = s('spellUserWords').value.split(/[\n,]+/).map(x => x.trim().toLowerCase()).filter(Boolean).slice(0, 2000);
    const now = (settings.spellUserWords || []).map(x => String(x).toLowerCase());
    try {
      for (const w of want) if (!now.includes(w)) await window.pv.spell.add(w);
      for (const w of now) if (!want.includes(w)) await window.pv.spell.remove(w);
      await save({spellUserWords: want}); status.textContent = want.length + ' words in the dictionary.';
    } catch (e) { status.textContent = e.message || 'Could not save dictionary words.'; }
  };
  if (s('spellWordsRefresh')) s('spellWordsRefresh').onclick = async () => { try { const st = await window.pv.spell.status(); if (s('spellUserWords')) s('spellUserWords').value = ((st.words || [])).join('\n'); } catch (e) { status.textContent = e.message; } };
  if (s('updateCheckNow') && !s('updateDismiss')) {
    const dismiss = document.createElement('button'); dismiss.className = 'mini'; dismiss.id = 'updateDismiss'; dismiss.textContent = 'Dismiss this release'; dismiss.disabled = true;
    s('updateCheckNow').after(dismiss);
  }
  if (s('updateCheckNow')) s('updateCheckNow').onclick = async () => {
    s('updateCheckNow').disabled = true; if (s('updateCheckResult')) s('updateCheckResult').textContent = 'Checking…';
    try {
      const r = await window.pv.updates.check();
      if (s('updateCheckResult')) s('updateCheckResult').textContent = !r.ok ? ('Check failed: ' + (r.message || 'unknown')) : r.release ? ('Current engine ' + r.version + '. Latest Android release: ' + r.release + (r.dismissed ? ' (dismissed)' : '') + '. Install from Releases; this build does not self-install.') : 'No Android release found.';
      if (s('updateDismiss')) s('updateDismiss').disabled = !r.release;
      if (r.release) s('updateDismiss').dataset.version = r.release;
    } catch (e) { if (s('updateCheckResult')) s('updateCheckResult').textContent = e.message || 'Check failed.'; }
    s('updateCheckNow').disabled = false;
  };
  if (s('updateDismiss')) s('updateDismiss').onclick = async () => {
    const version = s('updateDismiss').dataset.version; if (!version) return;
    try { await window.pv.updates.dismiss({version}); status.textContent = 'Dismissed ' + version + '.'; } catch (e) { status.textContent = e.message; }
  };

  const recoveryLink = document.createElement('button'); recoveryLink.className = 'mini'; recoveryLink.textContent = 'Open session recovery';
  recoveryLink.onclick = () => window.pv.nav.go('psychovault://recovery');
  document.querySelector('.settings-main').prepend(recoveryLink);

  transport.start().then(() => window.pv.settings.get()).then(st => { paint(st); paintExtensions(); refreshVault(); paintCatalogue(); refreshData(); refreshDiagnostics(); }).catch(e => { status.textContent = e.message || 'Android settings are unavailable.'; });
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
