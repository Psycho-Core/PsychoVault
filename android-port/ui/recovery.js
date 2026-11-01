// Trusted recovery document only; the host checks the exact internal origin and path.
(function () {
  'use strict';
  if (location.origin !== 'chrome://psychovault-pages' || location.pathname !== '/recovery.html' || !window.PsychoVaultNative) return;
  const isPrivate = new URLSearchParams(location.search).get('private') === '1';
  const transport = PsychoNativeTransport.create({native:window.PsychoVaultNative, resolver:PsychoNavigationResolver, isPrivate, page:true});
  window.PsychoNativeTransport = transport;
  const pv = PsychoInternalPageBridge.create(transport);
  const status = document.getElementById('recovery-status'), list = document.getElementById('recovery-list');
  async function refresh() {
    try {
      const data = await pv.recovery.list(); const items = data.items || [];
      list.replaceChildren();
      if (!items.length) { status.textContent = 'No recovery snapshots yet.'; return; }
      status.textContent = items.length + ' snapshot(s).';
      for (const item of items) {
        const row = document.createElement('div'); row.className = 'ext-item';
        const title = document.createElement('div'); title.className = 'ext-name';
        title.textContent = new Date(item.at).toLocaleString() + ' - ' + item.count + ' tab(s) - ' + item.reason;
        const acts = document.createElement('div'); acts.className = 'ext-acts';
        const restore = document.createElement('button'); restore.className = 'mini'; restore.textContent = 'Restore as new tabs';
        restore.onclick = async () => { try { const r = await pv.recovery.restore(item.id); status.textContent = 'Opened ' + r.count + ' tab(s).'; } catch (e) { status.textContent = e.message; } };
        const remove = document.createElement('button'); remove.className = 'mini'; remove.textContent = 'Delete snapshot';
        remove.onclick = async () => { try { await pv.recovery.remove(item.id); refresh(); } catch (e) { status.textContent = e.message; } };
        acts.append(restore, remove); row.append(title, acts); list.appendChild(row);
      }
    } catch (e) { status.textContent = e.message || 'Recovery is unavailable.'; }
  }
  document.getElementById('recovery-refresh').onclick = refresh;
  document.getElementById('recovery-settings').onclick = () => pv.nav.go('psychovault://settings');
  transport.start().then(refresh).catch(e => { status.textContent = e.message || 'Android recovery is unavailable.'; });
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
