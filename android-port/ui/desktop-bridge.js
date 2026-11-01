// Adapter for the desktop renderer. Only the future trusted Chromium UI host may install it.
// This file does not implement the native operations and must never be injected into web pages.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoDesktopBridge = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  const channels = Object.freeze({
    tab:{new:'tab:new',close:'tab:close',activate:'tab:activate',reopen:'tab:reopen',menu:'tab:menu',pin:'tab:pin',mute:'tab:mute',move:'tab:move'},
    nav:{go:'nav:go',back:'nav:back',forward:'nav:forward',reload:'nav:reload',stop:'nav:stop',home:'nav:home'},
    translate:{page:'translate:page'}, win:{minimize:'win:minimize',maximize:'win:maximize',close:'win:close',private:'win:private'},
    bookmarks:{list:'bookmarks:list',add:'bookmarks:add',remove:'bookmarks:remove',isBookmarked:'bookmarks:isBookmarked'},
    history:{list:'history:list',clear:'history:clear',search:'history:search'}, settings:{get:'settings:get',set:'settings:set'},
    find:{start:'find:start',stop:'find:stop'}, zoom:{in:'zoom:in',out:'zoom:out',reset:'zoom:reset'}, view:{split:'view:split'},
    downloads:{list:'downloads:list',control:'downloads:control',clear:'downloads:clear',open:'downloads:open',reveal:'downloads:reveal',dir:'downloads:dir',pickdir:'downloads:pickdir'},
    ui:{layout:'ui:layout',menu:'ui:menu'}, ws:{list:'ws:list',switch:'ws:switch',new:'ws:new',close:'ws:close',rename:'ws:rename',menu:'ws:menu'},
    clip:{write:'clipboard:write'}, rail:{icons:'rail:icons',catalogue:'catalogue:list',toggle:'catalogue:toggle',menu:'gx:services'},
    spell:{status:'spell:status',add:'spell:add',remove:'spell:remove'}, updates:{banner:'update:banners',install:'update:install',later:'update:later',dismiss:'update:dismiss'},
    proton:{toggle:'proton:toggle'}, extensions:{list:'ext:list',addfolder:'ext:addfolder',addcrx:'ext:addcrx',remove:'ext:remove',toggle:'ext:toggle',openpage:'ext:openpage',restorebuiltin:'ext:restorebuiltin',status:'ext:status'},
    gx:{hot:'gx:hot',media:'gx:media',snapshot:'gx:snapshot',panic:'gx:panic',services:'gx:services',switches:'gx:switches',relaunch:'app:relaunch'},
    vault:{status:'vault:status',unlock:'vault:unlock',lock:'vault:lock'}
  });
  const events = new Set(['state','hover','downloads','downloader','loadfail','cmd','menu-command','vault','workspaces','spell','icons','extensions','update-progress','update-available']);
  const noArguments = new Set(["tab.reopen","nav.back","nav.forward","nav.reload","nav.stop","nav.home","translate.page","win.minimize","win.maximize","win.close","win.private","bookmarks.list","history.list","history.clear","settings.get","find.stop","zoom.in","zoom.out","zoom.reset","view.split","downloads.list","downloads.clear","downloads.dir","downloads.pickdir","ui.menu","ws.list","rail.icons","rail.catalogue","spell.status","updates.banner","updates.install","updates.later","updates.dismiss","proton.toggle","extensions.list","extensions.addfolder","extensions.addcrx","extensions.status","gx.switches","gx.relaunch","vault.status","vault.lock"]);
  function create(transport) {
    if (!transport || typeof transport.invoke !== 'function' || typeof transport.subscribe !== 'function') throw new TypeError('A native Chromium transport is required');
    const api = {};
    for (const [group, methods] of Object.entries(channels)) {
      const obj = {};
      for (const [method, channel] of Object.entries(methods)) obj[method] = (...args) => {
        const count = noArguments.has(group + '.' + method) ? 0 : (group === 'vault' && method === 'transfer' ? 2 : 1);
        const forwarded = Array.from({length:count}, (_, index) => args[index]);
        return Promise.resolve().then(() => transport.invoke(channel, forwarded));
      };
      api[group] = Object.freeze(obj);
    }
    api.ws = Object.freeze({...api.ws, rename:(i,name)=>Promise.resolve().then(()=>transport.invoke('ws:rename',[{i,name}]))});
    api.rail = Object.freeze({...api.rail, menu:i=>Promise.resolve().then(()=>transport.invoke('gx:services',[{i,action:'menu'}]))});
    api.openExternal = url => Promise.resolve().then(()=>transport.invoke('open:external',[url]));
    api.on = (event, listener) => {
      if (!events.has(event) || typeof listener !== 'function') throw new TypeError('Unsupported browser event');
      return transport.subscribe(event, listener);
    };
    return Object.freeze(api);
  }
  return {create};
});
