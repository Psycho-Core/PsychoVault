(() => {
  const $ = id => document.getElementById(id);
  const browser = window.Capacitor && window.Capacitor.Plugins.PsychoBrowser;
  const tabs = new Map(); let active = null; let count = 0;
  const show = text => { $('notice').textContent = text; $('notice').hidden = false; };
  if (!browser) { show('Native Android browser is unavailable. Build and open the Android app.'); return; }
  let storage;
  try { storage = window.localStorage; } catch { storage = null; }
  const store = PsychoStore.open(storage || { getItem() { throw new Error('Storage unavailable'); } });
  document.body.dataset.theme = store.read().settings.theme;
  if (!store.writable()) show('Browser data could not be read. Changes are disabled to avoid overwriting it.');
  const vault = typeof PsychoVault === 'undefined' ? null : PsychoVault.open(storage || { getItem() { throw new Error('Storage unavailable'); } });
  let currentPanel = '';
  let sessionReady = false;
  function saveSession() {
    if (!sessionReady || !store.read().settings.restoreSession) return;
    const entries = [...tabs.entries()];
    try { store.saveSession({ tabs: entries.map(([, tab]) => tab.url), active: entries.findIndex(([id]) => id === active) }); }
    catch { show('Could not save the open tabs.'); }
  }
  let railTimer = null;
  function hideRail() {
    if (railTimer !== null) clearTimeout(railTimer);
    railTimer = null;
    $('rail').hidden = true;
    $('rail-toggle').setAttribute('aria-expanded', 'false');
    $('rail-toggle').setAttribute('aria-label', 'Show sidebar');
    $('rail-toggle').title = 'Show sidebar';
  }
  function scheduleRailHide() {
    if (railTimer !== null) clearTimeout(railTimer);
    railTimer = setTimeout(hideRail, 5000);
  }
  $('rail-toggle').onclick = () => {
    if (!$('rail').hidden) { hideRail(); return; }
    $('rail').hidden = false;
    $('rail-toggle').setAttribute('aria-expanded', 'true');
    $('rail-toggle').setAttribute('aria-label', 'Hide sidebar');
    $('rail-toggle').title = 'Hide sidebar';
    scheduleRailHide();
  };
  $('rail').addEventListener('pointerdown', scheduleRailHide);
  $('toolbar').addEventListener('pointerdown', hideRail);
  $('tabs').addEventListener('pointerdown', hideRail);
  const run = promise => promise.catch(() => show('That action failed in the experimental browser.'));
  function paint() {
    $('tabs').replaceChildren();
    for (const [id, tab] of tabs) {
      const el = document.createElement('div'); el.className = 'tab' + (id === active ? ' active' : '');
      el.setAttribute('role', 'tab'); el.setAttribute('aria-selected', String(id === active));
      const label = document.createElement('span'); label.textContent = tab.title || 'New tab';
      label.onclick = () => run(select(id));
      const close = document.createElement('button'); close.className = 'tab-close'; close.textContent = '×'; close.setAttribute('aria-label', 'Close tab'); close.onclick = () => run(closeTab(id));
      el.append(label, close); $('tabs').append(el);
    }
    if (document.activeElement !== $('address')) $('address').value = tabs.get(active)?.url || '';
  }
  async function bounds() {
    const r = $('page').getBoundingClientRect();
    await browser.bounds({ x:r.x, y:r.y, width:r.width, height:r.height });
  }
  async function select(id) { await browser.activate({id}); active = id; paint(); saveSession(); await bounds(); }
  async function addTab(url = 'https://www.google.com/') {
    const id = String(++count); await browser.create({id}); tabs.set(id, {title:'New tab', url}); active = id; paint(); await bounds();
    await browser.navigate({id, url}); saveSession();
  }
  async function closeTab(id) {
    await browser.close({id}); tabs.delete(id);
    if (!tabs.size) { await addTab(); return; }
    if (active === id) await select(tabs.keys().next().value); else { paint(); saveSession(); }
  }
  $('new-tab').onclick = () => run(addTab());
  for (const id of ['back','forward','reload']) $(id).onclick = () => run(browser.command({id:active, command:id}));
  $('address-form').onsubmit = event => {
    event.preventDefault(); const url = PsychoUrl.destination($('address').value, store.read().settings.search);
    if (!url) { show('Enter a valid HTTP or HTTPS address, or a search.'); return; }
    $('address').blur(); run(browser.navigate({id:active, url}));
  };
  $('proton').onclick = () => show('Proton VPN is not implemented or connected in this preview. Traffic is not protected by Proton.');
  $('blocker').onclick = () => openPanel('blocker');
  function action(label, handler) {
    const button = document.createElement('button'); button.type = 'button'; button.textContent = label; button.onclick = handler; return button;
  }
  function closePanel() { if (vault) vault.lock(); $('panel-content').replaceChildren(); currentPanel = ''; $('panel').hidden = true; }
  window.addEventListener('pagehide', closePanel);
  window.addEventListener('visibilitychange', () => { if (document.hidden && currentPanel === 'vault') closePanel(); });
  function openPanel(kind) {
    hideRail();
    if (kind !== 'vault' && vault) vault.lock();
    currentPanel = kind;
    const content = $('panel-content'); content.replaceChildren();
    if (kind === 'vault') {
      const heading = document.createElement('h2'); heading.textContent = 'PsychoVault'; content.append(heading);
      const note = document.createElement('p'); note.textContent = 'Experimental encrypted vault. No autofill, backup import or export yet. Locking clears the open view but cannot guarantee memory is erased.'; content.append(note);
      if (!vault) { const warning = document.createElement('p'); warning.textContent = 'Vault is unavailable.'; content.append(warning); }
      else {
        let exists = false;
        try { exists = vault.exists(); } catch { const warning = document.createElement('p'); warning.textContent = 'Vault storage is unavailable.'; content.append(warning); $('panel').hidden = false; return; }
        const field = (label, type = 'text') => {
          const row = document.createElement('label'); row.textContent = label;
          const input = document.createElement('input'); input.type = type; input.autocomplete = 'off';
          row.append(input); content.append(row); return input;
        };
        if (!vault.unlocked()) {
          const master = field('Master password (at least 12 characters for a new vault)', 'password');
          const confirmation = exists ? null : field('Confirm master password', 'password');
          content.append(action(exists ? 'Unlock vault' : 'Create vault', async () => {
            const value = master.value; const matches = !confirmation || confirmation.value === value;
            master.value = ''; if (confirmation) confirmation.value = '';
            if (!matches) { show('Master passwords do not match.'); return; }
            try { if (exists) await vault.unlock(value); else await vault.create(value); openPanel('vault'); }
            catch { show(exists ? 'Could not unlock vault. Check the password and stored data.' : 'Could not create vault. Use a password of at least 12 characters.'); }
          }));
        } else {
          content.append(action('Lock vault', () => { vault.lock(); openPanel('vault'); }));
          for (const entry of vault.list()) {
            const row = document.createElement('p'); row.textContent = entry.site + ' - ' + entry.user; content.append(row);
            if (entry.note) { const note = document.createElement('p'); note.textContent = entry.note; content.append(note); }
            const secret = document.createElement('input'); secret.type = 'password'; secret.readOnly = true; secret.value = entry.pass; secret.setAttribute('aria-label', 'Password for ' + entry.site); content.append(secret);
            content.append(action('Show or hide password', () => { secret.type = secret.type === 'password' ? 'text' : 'password'; }));
            content.append(action('Remove entry', async () => {
              if (!window.confirm('Remove this vault entry?')) return;
              try { await vault.remove(entry.id); openPanel('vault'); } catch { show('Could not remove vault entry.'); }
            }));
          }
          const site = field('Website'), user = field('Username'), pass = field('Password', 'password'), noteInput = field('Note');
          content.append(action('Add entry', async () => {
            try { await vault.add({ site: site.value, user: user.value, pass: pass.value, note: noteInput.value }); pass.value = ''; openPanel('vault'); }
            catch (error) { show(error.message || 'Could not save vault entry.'); }
          }));
        }
      }
    } else if (kind === 'blocker') {
      const heading = document.createElement('h2'); heading.textContent = 'Ad blocker'; content.append(heading);
      const note = document.createElement('p');
      note.textContent = 'Uses the desktop domain list and your custom domains. Android WebView cannot intercept every request, including some redirects and service worker traffic. Blocking is not guaranteed.';
      content.append(note);
      for (const [field, label] of [['blockAds', 'Enable domain blocker'], ['blockUseBundled', 'Use bundled domain list']]) {
        const row = document.createElement('label'), input = document.createElement('input');
        input.type = 'checkbox'; input.checked = store.read().settings[field];
        input.onchange = async () => {
          input.disabled = true;
          try { store.settings({[field]: input.checked}); await configureBlocker(); }
          catch { input.checked = store.read().settings[field]; show('Could not apply blocker settings. Blocking may be unavailable.'); }
          finally { input.disabled = false; }
        };
        row.append(input, document.createTextNode(' ' + label)); content.append(row);
      }
      for (const [field, label] of [['blockCustomDomains', 'Blocked domains'], ['blockAllowSites', 'Allowed top-level sites']]) {
        const row = document.createElement('label'), input = document.createElement('textarea');
        row.textContent = label + ' (one per line)'; input.value = store.read().settings[field]; input.rows = 3;
        row.append(input); content.append(row);
        content.append(action('Save ' + label.toLowerCase(), async () => {
          try { store.settings({[field]: input.value}); await configureBlocker(); show('Blocker settings saved.'); }
          catch { show('Could not apply blocker settings. Blocking may be unavailable.'); }
        }));
      }
    } else if (kind === 'settings') {
      const heading = document.createElement('h2'); heading.textContent = 'Settings'; content.append(heading);
      for (const [field, label, choices] of [['search','Search engine',PsychoStore.engines], ['theme','Theme',PsychoStore.themes]]) {
        const row = document.createElement('label'); row.textContent = label + ' ';
        const select = document.createElement('select'); select.setAttribute('aria-label', label);
        for (const choice of choices) { const option = document.createElement('option'); option.value = choice; option.textContent = choice; select.append(option); }
        select.value = store.read().settings[field];
        select.onchange = () => { try { store.settings({ [field]: select.value }); document.body.dataset.theme = store.read().settings.theme; } catch { show('Could not save settings.'); } };
        row.append(select); content.append(row);
      }
      const restoreRow = document.createElement('label');
      const restore = document.createElement('input'); restore.type = 'checkbox';
      restore.checked = store.read().settings.restoreSession;
      restore.onchange = () => {
        try { store.settings({ restoreSession: restore.checked }); saveSession(); }
        catch { restore.checked = store.read().settings.restoreSession; show('Could not save settings.'); }
      };
      restoreRow.append(restore, document.createTextNode(' Restore open tabs on startup'));
      content.append(restoreRow);
      const privacy = document.createElement('p'); privacy.textContent = 'Restored tab addresses are stored locally without encryption. Browsing history within tabs is not restored.'; content.append(privacy);
      const note = document.createElement('p'); note.textContent = 'More desktop settings, extensions and private browsing are not implemented. The vault is experimental and incomplete.'; content.append(note);
    } else {
      content.append(action('PsychoVault', () => openPanel('vault')));
      const state = store.read();
      for (const [label, entries] of [['Bookmarks',state.bookmarks], ['History',state.history]]) {
        const heading = document.createElement('h2'); heading.textContent = label; content.append(heading);
        if (!entries.length) { const empty = document.createElement('p'); empty.textContent = 'Nothing saved yet.'; content.append(empty); }
        for (const item of entries) {
          const button = action(item.title || item.url, () => { closePanel(); run(browser.navigate({ id: active, url: item.url })); });
          button.className = 'entry'; button.title = item.url; content.append(button);
          const remove = action('Remove', () => {
            try {
              if (label === 'Bookmarks') store.removeBookmark(item.url); else store.removeHistory(item.url);
              openPanel('menu');
            } catch { show('Could not remove this entry.'); }
          });
          remove.setAttribute('aria-label', 'Remove ' + (item.title || item.url) + ' from ' + label.toLowerCase());
          content.append(remove);
        }
        if (label === 'History' && entries.length) content.append(action('Clear history', () => { try { store.clearHistory(); openPanel('menu'); } catch { show('Could not clear history.'); } }));
      }
    }
    $('panel').hidden = false;
  }
  $('menu').onclick = () => openPanel('menu');
  $('settings').onclick = () => openPanel('settings');
  $('panel-close').onclick = closePanel;
  $('bookmark').onclick = () => {
    const tab = tabs.get(active);
    try { const added = store.toggleBookmark(tab && tab.url, tab && tab.title); show(added ? 'Bookmark saved.' : 'Bookmark removed.'); }
    catch { show('Could not save this bookmark.'); }
  };
  browser.addListener('pageTouched', hideRail);
  browser.addListener('browserNotice', data => show(data.message || 'Browser action failed.'));
  browser.addListener('pageChanged', data => {
    const tab = tabs.get(data.id); if (!tab) return;
    tab.title = data.title || 'New tab'; tab.url = data.url || '';
    try { store.visit(tab.url, tab.title); } catch { show('Could not save history.'); }
    paint(); saveSession();
  });
  new ResizeObserver(() => run(bounds())).observe($('page'));
  window.addEventListener('resize', () => run(bounds()));
  async function configureBlocker() { await browser.configureBlocker(store.read().settings); }
  async function start() {
    const saved = store.read();
    $('new-tab').disabled = true;
    try {
      try { await configureBlocker(); } catch { show('Could not enable the domain blocker. Blocking is unavailable.'); }
      if (saved.settings.restoreSession && saved.session.tabs.length) {
        for (const url of saved.session.tabs) await addTab(url);
        await select([...tabs.keys()][saved.session.active] || tabs.keys().next().value);
      } else await addTab();
      sessionReady = true;
      saveSession();
    } catch {
      show('Could not restore the browser session. The saved session has not been overwritten.');
    } finally { $('new-tab').disabled = false; }
  }
  run(start());
})();
