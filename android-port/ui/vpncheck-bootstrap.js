// Only the Android VPN-checks document gets this bridge.
(function () {
  'use strict';
  if (location.origin !== 'chrome://psychovault-pages' || location.pathname !== '/vpncheck.html' || !window.PsychoVaultNative) return;
  const isPrivate = new URLSearchParams(location.search).get('private') === '1';
  const transport = PsychoNativeTransport.create({native:window.PsychoVaultNative, resolver:PsychoNavigationResolver, isPrivate, page:true});
  window.PsychoNativeTransport = transport;
  window.pv = PsychoInternalPageBridge.create(transport);
  transport.start().catch(() => { document.getElementById('ips').textContent = 'Android VPN checks are unavailable.'; });
  const script = document.createElement('script'); script.src = 'vpncheck.js'; document.body.appendChild(script);
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
