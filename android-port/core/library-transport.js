// Decorates a trusted chrome transport with real library handlers. The native host
// must enforce the trust boundary and call recordVisit from committed tab events.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoLibraryTransport = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  function create(nativeTransport, library) {
    if (!nativeTransport || typeof nativeTransport.invoke !== 'function' || typeof nativeTransport.subscribe !== 'function' || !library) throw new TypeError('Native transport and library are required');
    const handlers = Object.freeze({
      'bookmarks:list': () => library.listBookmarks(),
      'bookmarks:add': args => library.addBookmark(args[0]),
      'bookmarks:remove': args => library.removeBookmark(args[0]),
      'bookmarks:isBookmarked': args => library.isBookmarked(args[0]),
      'history:list': () => library.listHistory(),
      'history:clear': () => library.clearHistory(),
      'history:search': args => library.search(args[0])
    });
    return Object.freeze({
      invoke(channel, args) {
        if (!Array.isArray(args)) throw new TypeError('Arguments must be an array');
        if (Object.hasOwn(handlers, channel)) return handlers[channel](args);
        return nativeTransport.invoke(channel, args);
      },
      subscribe: (event, listener) => nativeTransport.subscribe(event, listener)
    });
  }
  return Object.freeze({create});
});
