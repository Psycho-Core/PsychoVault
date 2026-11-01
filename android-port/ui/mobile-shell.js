// Runs only inside the future trusted Chromium browser UI, never in a website tab.
(function () {
  'use strict';
  const controller = window.PsychoMobileSidebar.create({
    rail: document.getElementById('rail'), toggle: document.getElementById('mobile-rail-toggle'), document,
    setTimer: window.setTimeout.bind(window), clearTimer: window.clearTimeout.bind(window), delay: 5000
  });
  // A native Chromium view is separate from the UI document; it must forward page touches.
  window.addEventListener('psychovault-page-touch', controller.pageTouched);
  window.addEventListener('pagehide', () => {
    window.removeEventListener('psychovault-page-touch', controller.pageTouched);
    controller.dispose();
  }, { once: true });
  // Do not load the Electron renderer without a real host bridge. A webpage must never receive this bridge.
  if (!window.pv) {
    const status = document.getElementById('status-text');
    status.textContent = 'Android Chromium bridge not connected - browser controls are unavailable.';
    return;
  }
  const script = document.createElement('script'); script.src = 'renderer.js'; document.body.appendChild(script);
})();
