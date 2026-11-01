// IndexedDB storage for a dedicated trusted browser UI origin. Never install in websites.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoLibraryStorage = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  function create(indexedDB) {
    if (!indexedDB || typeof indexedDB.open !== 'function') throw new TypeError('IndexedDB is required');
    let opened;
    function db() {
      if (!opened) {
        opened = new Promise((resolve, reject) => {
          const req = indexedDB.open('psychovault-library', 1);
          let blocked = false;
          req.onupgradeneeded = () => req.result.createObjectStore('data');
          req.onerror = () => reject(req.error || new Error('Unable to open library'));
          req.onblocked = () => { blocked = true; reject(new Error('Library database upgrade blocked')); };
          req.onsuccess = () => { if (blocked) { req.result.close(); return; } req.result.onversionchange = () => { req.result.close(); opened = null; }; resolve(req.result); };
        }).catch(error => { opened = null; throw error; });
      }
      return opened;
    }
    async function transaction(mode, operation) {
      const database = await db();
      return new Promise((resolve, reject) => {
        const tx = database.transaction('data', mode);
        let request, result;
        try { request = operation(tx.objectStore('data')); }
        catch (err) { tx.abort(); reject(err); return; }
        request.onsuccess = () => { result = request.result; };
        tx.oncomplete = () => resolve(result === undefined ? null : result);
        tx.onerror = () => reject(tx.error || new Error('Library transaction failed'));
        tx.onabort = () => reject(tx.error || new Error('Library transaction aborted'));
      });
    }
    return Object.freeze({
      read: () => transaction('readonly', store => store.get('library')),
      write: data => transaction('readwrite', store => store.put(data, 'library')),
      // Read-modify-write inside one readwrite transaction so two chrome windows
      // cannot lose each other's updates. The mutator must be synchronous.
      update: async mutator => {
        const database = await db();
        return new Promise((resolve, reject) => {
          const tx = database.transaction('data', 'readwrite');
          const store = tx.objectStore('data');
          let next;
          const request = store.get('library');
          request.onsuccess = () => {
            try { next = mutator(request.result === undefined ? null : request.result); store.put(next, 'library'); }
            catch (error) { tx.abort(); }
          };
          tx.oncomplete = () => resolve(next);
          tx.onerror = () => reject(tx.error || new Error('Library transaction failed'));
          tx.onabort = () => reject(tx.error || new Error('Library transaction aborted'));
        });
      }
    });
  }
  return Object.freeze({create});
});
