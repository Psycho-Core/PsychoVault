// DOM measurements are CSS pixels. Native converts them to physical pixels.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoHostLayout = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  function measure(document, nativeTopPx = 0, density = 1) {
    const visible = el => el && !el.hidden && !el.classList.contains('hidden') && getComputedStyle(el).display !== 'none';
    const rect = el => { const r = el.getBoundingClientRect(); return {left:r.left, top:r.top, right:r.right, bottom:r.bottom}; };
    const top = document.getElementById('top');
    const nativeTop = nativeTopPx / Math.max(1, density);
    if (top) top.style.minHeight = nativeTop + 'px';
    const extraTop = Math.max(0, (top ? top.getBoundingClientRect().height : 96) - nativeTop);
    const panel = document.getElementById('panel');
    const regions = [top, document.getElementById('rail'), document.getElementById('mobile-rail-toggle'),
      panel, document.getElementById('menu'), document.getElementById('palette'), document.getElementById('toast'),
      document.getElementById('suggest'), document.getElementById('findbar')].filter(visible).map(rect);
    return {chromeTop: nativeTop + extraTop, extraTop, extraBottom: 0,
      panelLeft: visible(panel) ? panel.getBoundingClientRect().width : 0, hitRegions: regions};
  }
  function start({document, window, invoke}) {
    let queued = false, disposed = false, last = '', nativeTopPx = 0;
    let density = window.devicePixelRatio || 1;
    function send() {
      queued = false;
      if (disposed) return;
      density = window.devicePixelRatio || 1;
      const value = measure(document, nativeTopPx, density), encoded = JSON.stringify(value);
      if (encoded !== last) { last = encoded; Promise.resolve(invoke('ui:layout', [value])).catch(() => { last = ''; }); }
    }
    function schedule() { if (!queued && !disposed) { queued = true; window.requestAnimationFrame(send); } }
    const observer = new ResizeObserver(schedule);
    observer.observe(document.body);
    for (const id of ['top','rail','panel','menu','palette','toast','suggest','findbar']) {
      const el = document.getElementById(id); if (el) observer.observe(el);
    }
    window.addEventListener('resize', schedule);
    window.addEventListener('orientationchange', schedule);
    const viewport = window.visualViewport;
    if (viewport) { viewport.addEventListener('resize', schedule); viewport.addEventListener('scroll', schedule); }
    const mutation = new MutationObserver(schedule);
    mutation.observe(document.body, {subtree:true, attributes:true, attributeFilter:['hidden','class','style']});
    return {schedule, setState(state) { nativeTopPx = Number(state.nativeTopPx) || 0; schedule(); },
      dispose() { disposed = true; observer.disconnect(); mutation.disconnect(); window.removeEventListener('resize', schedule); window.removeEventListener('orientationchange', schedule); if (viewport) { viewport.removeEventListener('resize', schedule); viewport.removeEventListener('scroll', schedule); } }};
  }
  return {measure, start};
});
