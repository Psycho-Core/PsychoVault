// Contract for trusted internal pages only. The Chromium host must enforce the origin boundary.
// This adapter is not installed automatically and supplies no native implementation.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PsychoInternalPageBridge = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';
  const channels = {
    settings:{get:'settings:get',set:'settings:set'}, history:{clear:'history:clear'}, nav:{go:'nav:go'},
    tab:{close:'tab:close',open:'tab:open'}, downloads:{dir:'downloads:dir',pickdir:'downloads:pickdir'},
    data:{report:'data:report',openErrorLog:'data:openErrorLog',clear:'data:clear'},
    app:{info:'app:info',diagnostics:'app:diagnostics',diagnosticsCopy:'app:diagnosticsCopy'},
    recovery:{list:'recovery:list',restore:'recovery:restore'}, vpncheck:{open:'vpncheck:open',ip:'vpncheck:ip'},
    proxy:{selftest:'proxy:selftest',status:'proxy:status'}, updates:{check:'update:check',tokenstate:'update:tokenstate',settoken:'update:settoken'},
    extensions:{list:'ext:list',addfolder:'ext:addfolder',addcrx:'ext:addcrx',remove:'ext:remove',toggle:'ext:toggle',openpage:'ext:openpage',restorebuiltin:'ext:restorebuiltin',status:'ext:status'},
    gx:{switches:'gx:switches',relaunch:'app:relaunch'},
    vault:{status:'vault:status',transfer:'vault:transfer',list:'vault:list',add:'vault:add',remove:'vault:delete',reveal:'vault:reveal',copy:'vault:copy',generate:'vault:generate',setMaster:'vault:setMaster',forgetMaster:'vault:forgetMaster',unlock:'vault:unlock',lock:'vault:lock'},
    spell:{status:'spell:status',add:'spell:add',remove:'spell:remove'}, rail:{icons:'rail:icons',catalogue:'catalogue:list',toggle:'catalogue:toggle'}
  };
  const noArguments = new Set(["settings.get","history.clear","downloads.dir","downloads.pickdir","data.report","data.openErrorLog","app.info","app.diagnostics","app.diagnosticsCopy","recovery.list","vpncheck.open","vpncheck.ip","proxy.selftest","proxy.status","updates.check","updates.tokenstate","extensions.list","extensions.addfolder","extensions.addcrx","extensions.status","gx.switches","gx.relaunch","vault.status","vault.list","vault.forgetMaster","vault.lock","spell.status","rail.icons","rail.catalogue"]);
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
    api.vault = Object.freeze({...api.vault,copy:(id,which)=>Promise.resolve().then(()=>transport.invoke('vault:copy',[{id,which}]))});
    api.openExternal = url => Promise.resolve().then(()=>transport.invoke('open:external',[url]));
    api.on = (event, listener) => {
      if (!['spell','extensions'].includes(event) || typeof listener !== 'function') throw new TypeError('Unsupported internal page event');
      return transport.subscribe(event, listener);
    };
    return Object.freeze(api);
  }
  return {create};
});
