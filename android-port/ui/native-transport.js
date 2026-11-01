// Installed only by the Android WebUI bootstrap after checking its exact document.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoNativeTransport = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  function create({native, resolver, isPrivate = false, page = false, setTimeout: later = setTimeout, clearTimeout: cancel = clearTimeout}) {
    if (!native || typeof native.post !== 'function' || !resolver) throw new TypeError('Native host transport required');
    const pending = new Map(), listeners = new Map();
    let sequence = 0, ready = false, disposed = false, settings = {};
    function invoke(channel, args = []) {
      if (disposed) return Promise.reject(new Error('Android chrome is closed'));
      if (!ready && channel !== 'ui:ready') return Promise.reject(new Error('Android chrome is not ready'));
      if (pending.size >= 64) return Promise.reject(new Error('Too many Android browser requests'));
      if ((channel === 'nav:go' || channel === 'tab:open' || (channel === 'tab:new' && args[0])) && typeof args[0] === 'string') {
        try { args = [resolver.resolve(args[0], settings, isPrivate)]; } catch (e) { return Promise.reject(e); }
      }
      return new Promise((resolve, reject) => {
        const id = sequence++;
        const slow = {'ext:addfolder':180000,'ext:addcrx':180000,'ext:remove':60000,'vault:transfer':180000,'vault:unlock':60000,'vault:setMaster':60000,'perf:snapshot':30000};
        const timer = later(() => { pending.delete(id); reject(new Error('Android browser request timed out')); }, slow[channel] || 15000);
        pending.set(id, {resolve, reject, timer, channel});
        try { native.post(JSON.stringify({id, channel, args})); }
        catch (e) { cancel(timer); pending.delete(id); reject(e); }
      });
    }
    function reply(id, ok, encoded) {
      const request = pending.get(id);
      if (!request) return;
      cancel(request.timer); pending.delete(id);
      let value;
      try { value = encoded === 'null' ? null : JSON.parse(encoded); } catch { request.reject(new Error('Invalid Android browser reply')); return; }
      if (ok && request.channel === 'settings:get' && value && typeof value === 'object') settings = value;
      if (ok) request.resolve(value); else request.reject(new Error(typeof value === 'string' ? value : 'Android browser operation failed'));
    }
    function emit(event, payload) {
      if (disposed) return;
      if (event === 'state' && payload && payload.settings) {
        settings = payload.settings; isPrivate = !!payload.private;
        if (payload.tabs) for (const tab of payload.tabs) tab.url = resolver.display(tab.url, isPrivate);
        if (payload.url) payload.url = resolver.display(payload.url, isPrivate);
      }
      if (event === 'page-touch') window.dispatchEvent(new Event('psychovault-page-touch'));
      for (const listener of listeners.get(event) || []) {
        try { listener(payload); } catch { /* A UI listener cannot break native dispatch. */ }
      }
    }
    function subscribe(event, listener) {
      if (typeof listener !== 'function') throw new TypeError('Listener required');
      if (!listeners.has(event)) listeners.set(event, new Set());
      listeners.get(event).add(listener);
      return () => listeners.get(event)?.delete(listener);
    }
    function dispose() {
      disposed = true;
      for (const request of pending.values()) { cancel(request.timer); request.reject(new Error('Android chrome is closed')); }
      pending.clear(); listeners.clear();
    }
    return {invoke, reply, emit, subscribe, dispose, start: () => invoke('ui:ready').then(state => { ready = true; emit('state', state); return state; }),
      get ready() { return ready; }};
  }
  return {create};
});
