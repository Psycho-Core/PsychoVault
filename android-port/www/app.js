(() => {
  const $ = id => document.getElementById(id);
  const browser = window.Capacitor && window.Capacitor.Plugins.PsychoBrowser;
  const tabs = new Map(); let active = null; let count = 0;
  const show = text => { $('notice').textContent = text; $('notice').hidden = false; };
  if (!browser) { show('Native Android browser is unavailable. Build and open the Android app.'); return; }
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
  async function select(id) { await browser.activate({id}); active = id; paint(); await bounds(); }
  async function addTab() {
    const id = String(++count); await browser.create({id}); tabs.set(id, {title:'New tab', url:''}); active = id; paint(); await bounds();
    await browser.navigate({id, url:'https://www.google.com/'});
  }
  async function closeTab(id) {
    await browser.close({id}); tabs.delete(id);
    if (!tabs.size) { await addTab(); return; }
    if (active === id) await select(tabs.keys().next().value); else paint();
  }
  $('new-tab').onclick = () => run(addTab());
  for (const id of ['back','forward','reload']) $(id).onclick = () => run(browser.command({id:active, command:id}));
  $('address-form').onsubmit = event => {
    event.preventDefault(); const url = PsychoUrl.destination($('address').value);
    if (!url) { show('Enter a valid HTTP or HTTPS address, or a search.'); return; }
    $('address').blur(); run(browser.navigate({id:active, url}));
  };
  $('proton').onclick = () => show('Proton VPN is not implemented or connected in this preview. Traffic is not protected by Proton.');
  $('blocker').onclick = () => show('The desktop ad blocker is not implemented in this preview.');
  $('settings').onclick = () => show('Settings, extensions, private browsing and the vault are not implemented in this preview.');
  browser.addListener('pageChanged', data => {
    const tab = tabs.get(data.id); if (!tab) return;
    tab.title = data.title || 'New tab'; tab.url = data.url || ''; paint();
  });
  new ResizeObserver(() => run(bounds())).observe($('page'));
  window.addEventListener('resize', () => run(bounds()));
  run(addTab());
})();
