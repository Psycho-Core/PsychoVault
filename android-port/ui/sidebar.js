// Mobile rail controller. The Chromium host must forward page touches separately.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoMobileSidebar = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  function create({ rail, toggle, document, setTimer, clearTimer, delay = 5000, onVisibility = () => {} }) {
    if (!rail || !toggle || !document || typeof setTimer !== 'function' || typeof clearTimer !== 'function' || !Number.isFinite(delay) || delay < 0) throw new TypeError('Invalid sidebar configuration');
    let timer = null, visible = false, disposed = false;
    function cancelTimer() { if (timer !== null) clearTimer(timer); timer = null; }
    function paint() {
      rail.hidden = !visible;
      toggle.setAttribute('aria-expanded', String(visible));
      toggle.setAttribute('aria-label', visible ? 'Hide sidebar' : 'Show sidebar');
      onVisibility(visible);
    }
    function hide() { if (disposed) return; cancelTimer(); if (visible) { visible = false; paint(); } }
    function schedule() { cancelTimer(); if (visible && delay > 0) timer = setTimer(hide, delay); }
    function show() { if (disposed) return; visible = true; paint(); schedule(); }
    function clicked() { if (visible) hide(); else show(); }
    function inside() { if (visible) schedule(); }
    function selected(event) { if (visible && event.target && typeof event.target.closest === 'function' && event.target.closest('button')) hide(); }
    function outside(event) {
      if (visible && event.target !== toggle && !toggle.contains(event.target) && !rail.contains(event.target)) hide();
    }
    function key(event) { if (visible && event.key === 'Escape') { hide(); if (typeof toggle.focus === 'function') toggle.focus(); } }
    toggle.addEventListener('click', clicked);
    rail.addEventListener('pointerdown', inside);
    rail.addEventListener('click', selected);
    document.addEventListener('pointerdown', outside);
    document.addEventListener('keydown', key);
    paint();
    return {
      show, hide, pageTouched: hide,
      dispose() {
        if (disposed) return; cancelTimer(); disposed = true;
        toggle.removeEventListener('click', clicked); rail.removeEventListener('pointerdown', inside); rail.removeEventListener('click', selected);
        document.removeEventListener('pointerdown', outside); document.removeEventListener('keydown', key);
      }
    };
  }
  return { create };
});
