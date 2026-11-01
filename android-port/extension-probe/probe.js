(function (root) {
  'use strict';
  // Only capability names and control levels are reported, never proxy values or credentials.
  async function inspect(api) {
    const has = value => typeof value === 'function' ? 'available' : 'missing';
    const rows = [
      ['runtime messaging', has(api.runtime && api.runtime.sendMessage)],
      ['extension storage', has(api.storage && api.storage.local && api.storage.local.get)],
      ['tabs', has(api.tabs && api.tabs.query)],
      ['scripting', has(api.scripting && api.scripting.executeScript)],
      ['web request', has(api.webRequest && api.webRequest.onBeforeRequest && api.webRequest.onBeforeRequest.addListener)],
      ['proxy authentication events', has(api.webRequest && api.webRequest.onAuthRequired && api.webRequest.onAuthRequired.addListener)],
      ['notifications', has(api.notifications && api.notifications.create)],
      ['idle detection', has(api.idle && api.idle.queryState)],
      ['privacy settings', has(api.privacy && api.privacy.network && api.privacy.network.webRTCIPHandlingPolicy && api.privacy.network.webRTCIPHandlingPolicy.get)],
      ['proxy settings', has(api.proxy && api.proxy.settings && api.proxy.settings.get)]
    ];
    const settings = api.proxy && api.proxy.settings;
    if (settings && typeof settings.get === 'function') {
      const control = await new Promise(resolve => {
        const timer = setTimeout(() => resolve('timed out'), 3000);
        const finish = result => { clearTimeout(timer); resolve(result); };
        try {
          settings.get({}, result => {
            if (api.runtime && api.runtime.lastError) { finish('unavailable'); return; }
            const allowed = ['not_controllable', 'controlled_by_other_extensions', 'controllable_by_this_extension', 'controlled_by_this_extension'];
            finish(result && allowed.includes(result.levelOfControl) ? result.levelOfControl : 'unknown');
          });
        } catch { finish('unavailable'); }
      });
      rows.push(['proxy control level', control]);
    }
    return rows.map(([name, value]) => `${name}: ${value}`).join('\n');
  }
  if (typeof module !== 'undefined' && module.exports) module.exports = { inspect }; else root.Probe = { inspect };
})(typeof window === 'undefined' ? globalThis : window);
