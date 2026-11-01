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
    try { history.replaceState(null, '', '#' + panel.dataset.categoryPanel); } catch {}
  }
  for (const b of buttons) b.addEventListener('click', () => select(b.dataset.category));
  select(location.hash.slice(1) || 'general');

  const BOOLS = ['remember','hideSidebar','highContrast','reduceMotion','showTabCloseButtons','restore',
    'blockAds','blockUseBundled','workspacesEnabled','blockAutoplay','blockThirdPartyCookies','sendDNT',
    'batterySaver','vaultEnabled','enhanceVideo','enhanceImages','askSaveLocation','panicEnabled','railIcons',
    'spellCheck','spellBuiltin','stripReferrer'];
  const SELECTS = ['search','theme','accent','cpuLimit','webRtc','doh','translateTarget','vaultLockMin'];
  const TEXTS = ['homepage','blockAllowSites','blockCustomDomains','sleepExclude','panicUrl','dohTemplate','denyPerms','modCss','spellLangs'];
  const SUPPORTED = new Set([...BOOLS, ...SELECTS, ...TEXTS,
    'customSearchEngines','customSearchSave','tiles','tileAdd','tileSave','sleepTabsMin','blockSaveLists',
    'extList','extReload','extAddFolder','extAddCrx','protonVpnRestore','protonVpnStatus','vpnCheckOpen',
    'vstat','vunlock','vpw','vgo','vmaster','vmasterHint','vnew','vset','vBackup','vRestore','vImport',
    'vbackupPassword','vbackupConfirm','vTransferMsg','vlist','eSite','eUser','ePass','eGen','eSave','vLock','vRefresh','vnote',
    'downloadDir']);
  for (const el of document.querySelectorAll('input,select,textarea,button')) {
    if (el.hasAttribute('data-category') || SUPPORTED.has(el.id)) continue;
    el.disabled = true;
    if (!el.title) el.title = 'Not connected on Android yet';
  }
  const note = document.createElement('p');
  note.className = 'hint';
  note.textContent = 'Android source build: enabled controls have native handlers. Disabled controls are not implemented yet. Network limits, containers, custom dictionaries and clear-on-exit are not available on this Android engine.';
  document.querySelector('.settings-main').prepend(note);
  if (isPrivate) {
    const privateNote = document.createElement('p'); privateNote.className = 'hint';
    privateNote.textContent = 'This is a private profile. Settings are read-only here; private windows do not save preferences.';
    document.querySelector('.settings-main').prepend(privateNote);
  }

  let settings = {};
  function paint(st) {
    settings = st;
    document.body.dataset.theme = st.theme || 'midnight';
    document.body.classList.toggle('high-contrast', !!st.highContrast);
    document.body.classList.toggle('reduce-motion', !!st.reduceMotion);
    for (const id of BOOLS) if (s(id)) s(id).checked = !!st[id];
    for (const id of SELECTS) if (s(id)) s(id).value = String(st[id] === undefined ? s(id).options[0].value : st[id]);
    for (const id of TEXTS) if (s(id)) s(id).value = st[id] || '';
    if (s('homepage')) s('homepage').value = st.homepage || 'psychovault://newtab';
    if (s('sleepTabsMin')) s('sleepTabsMin').value = String(st.sleepTabsMin || 0);
    if (s('customSearchEngines')) s('customSearchEngines').value = (st.customSearchEngines || []).map(e => [e.shortcut, e.name, e.url].join(' | ')).join('\n');
    renderTiles(st.speedDial || []);
    if (s('downloadDir')) { s('downloadDir').value = 'System Downloads folder'; s('downloadDir').readOnly = true; }
    if (s('downloadDirPick')) { s('downloadDirPick').disabled = true; s('downloadDirPick').title = 'Android manages the Downloads folder; use Ask where to save for a per-download choice.'; }
    if (s('spellStatus')) s('spellStatus').textContent = 'Spellcheck preferences are sent to Chromium. Available dictionaries depend on this build.';
    if (s('spellAvail')) s('spellAvail').textContent = 'Dictionary enumeration is not exposed by this Android engine.';
    if (isPrivate) for (const el of document.querySelectorAll('input,select,textarea,button')) if (!el.hasAttribute('data-category')) el.disabled = true;
  }
  async function save(patch) {
    if (isPrivate) { status.textContent = 'Private profiles do not save settings.'; return; }
    try { paint(await window.pv.settings.set(patch)); status.textContent = 'Saved.'; }
    catch (e) { status.textContent = e.message || 'Could not save settings.'; try { paint(await window.pv.settings.get()); } catch {} }
  }
  for (const id of BOOLS) { const el = s(id); if (el) el.onchange = () => save({[id]: el.checked}); }
  for (const id of SELECTS) { const el = s(id); if (el) el.onchange = () => save({[id]: id === 'vaultLockMin' ? Number(el.value) : el.value}); }
  for (const id of TEXTS) { const el = s(id); if (el && id !== 'blockAllowSites' && id !== 'blockCustomDomains') el.onchange = () => save({[id]: el.value.trim()}); }
  if (s('sleepTabsMin')) s('sleepTabsMin').onchange = () => save({sleepTabsMin: Math.max(0, Math.min(1440, Number(s('sleepTabsMin').value) || 0))});
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
        toggle.onclick = async () => { try { await window.pv.extensions.toggle({id: x.id, enabled: !x.enabled}); paintExtensions(); } catch (e) { status.textContent = e.message; } };
        const remove = document.createElement('button'); remove.className = 'mini'; remove.textContent = 'Remove';
        remove.onclick = async () => { try { await window.pv.extensions.remove(x.id); paintExtensions(); } catch (e) { status.textContent = e.message; } };
        acts.append(toggle, remove);
        if (x.popup) { const open = document.createElement('button'); open.className = 'mini'; open.textContent = 'Open'; open.onclick = () => window.pv.extensions.openpage({id: x.id, page: x.popup}).catch(e => status.textContent = e.message); acts.append(open); }
        row.appendChild(acts); box.appendChild(row);
      }
    } catch (e) { box.textContent = e.message || 'Extensions unavailable.'; }
  }
  if (s('extReload')) s('extReload').onclick = paintExtensions;
  if (s('protonVpnStatus')) s('protonVpnStatus').onclick = async () => {
    try { const st = await window.pv.extensions.status(); status.textContent = st.lastError ? 'Extension engine: ' + st.lastError : 'Extension registry refreshed. Proton connection state must be checked inside Proton.'; paintExtensions(); }
    catch (e) { status.textContent = e.message; }
  };
  if (s('protonVpnRestore')) s('protonVpnRestore').onclick = async () => { try { await window.pv.extensions.restorebuiltin(); paintExtensions(); status.textContent = 'Bundled extensions restore requested; check the list for Chromium load results.'; } catch (e) { status.textContent = e.message; } };
  if (s('vpnCheckOpen')) s('vpnCheckOpen').onclick = () => window.pv.tab.open('https://browserleaks.com/webrtc').catch(e => status.textContent = e.message);
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

  const recoveryLink = document.createElement('button'); recoveryLink.className = 'mini'; recoveryLink.textContent = 'Open session recovery';
  recoveryLink.onclick = () => window.pv.nav.go('psychovault://recovery');
  document.querySelector('.settings-main').prepend(recoveryLink);

  transport.start().then(() => window.pv.settings.get()).then(st => { paint(st); paintExtensions(); refreshVault(); }).catch(e => { status.textContent = e.message || 'Android settings are unavailable.'; });
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
