// Trusted chrome document only. Never copy this script into a website preload.
(function () {
  'use strict';
  const expected = 'chrome://psychovault-chrome/index.html?private=';
  if (!location.href.startsWith(expected) || !window.PsychoVaultNative) return;
  const isPrivate = new URLSearchParams(location.search).get('private') === '1';
  const transport = PsychoNativeTransport.create({native:window.PsychoVaultNative, resolver:PsychoNavigationResolver, isPrivate});
  window.PsychoNativeTransport = transport;
  const library = PsychoLibrary.create(PsychoLibraryStorage.create(indexedDB));
  const libraryTransport = PsychoLibraryTransport.create(transport, library);
  const bridge = PsychoDesktopBridge.create(libraryTransport);
  window.pv = bridge;
  // Unsupported controls stay visible in their desktop order, but cannot pretend to work.
  function markUnavailable() {
    for (const button of document.querySelectorAll('#btn-split')) {
      button.disabled = true; button.title = 'Not connected on Android yet';
    }
    for (const button of document.querySelectorAll('#menu button')) {
      if (!['newtab','private','reopen','bookmarks','history','downloads','settings','extensions','find','translate','zoomin','zoomout','zoomreset'].includes(button.dataset.cmd)) { button.disabled = true; button.title = 'Not connected on Android yet'; }
    }
  }
  markUnavailable();
  const layout = PsychoHostLayout.start({document, window, invoke:transport.invoke});
  transport.subscribe('state', layout.setState);
  // The native host emits this only for committed regular-profile navigations
  // while remember=true. The library still re-checks the context before writing.
  transport.subscribe('library:visit', visit => {
    library.recordVisit(visit, {profile: isPrivate ? 'private' : 'regular', remember: true}).catch(() => {});
  });
  const originalEmit = transport.emit;
  // State is applied by the desktop renderer after it loads; keep layout in sync too.
  transport.emit = (event, payload) => { if (event === 'state') layout.setState(payload); originalEmit(event, payload); if (event === 'state') markUnavailable(); };
  window.syncChrome = layout.schedule;
  const shell = document.createElement('script');
  shell.src = 'mobile-shell.js'; shell.onload = () => setTimeout(markUnavailable, 0);
  document.body.appendChild(shell);
  transport.start().then(() => layout.schedule()).catch(() => {
    const status = document.getElementById('status-text');
    if (status) status.textContent = 'Android browser host did not become ready.';
  });
  window.addEventListener('pagehide', () => { layout.dispose(); transport.dispose(); }, {once:true});
})();
