// Only the Android new-tab document gets this narrow bridge.
(function () {
  'use strict';
  if (location.origin !== 'chrome://psychovault-pages' || location.pathname !== '/newtab.html' || !window.PsychoVaultNative) return;
  const isPrivate = new URLSearchParams(location.search).get('private') === '1';
  const transport = PsychoNativeTransport.create({native:window.PsychoVaultNative, resolver:PsychoNavigationResolver, isPrivate, page:true});
  window.PsychoNativeTransport = transport;
  window.pv = PsychoInternalPageBridge.create(transport);
  transport.start().catch(() => {});
  const script = document.createElement('script'); script.src = 'newtab.js'; document.body.appendChild(script);
  window.addEventListener('pagehide', () => transport.dispose(), {once:true});
})();
