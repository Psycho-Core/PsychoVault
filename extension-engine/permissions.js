'use strict';
// PsychoVault permission policy. This is my own MIT-licensed code. It describes
// which browser APIs an extension may call; it does not implement the APIs.

const CONTEXTS = ['worker', 'popup', 'options', 'content'];

// Method -> permission and allowed extension contexts. A missing entry is a
// hard deny, not an implicit grant. Host access is checked separately because
// several APIs need both an API permission and a matching host permission.
const METHODS = Object.freeze({
  'runtime.getManifest': { permissions: [], contexts: CONTEXTS },
  'runtime.getURL': { permissions: [], contexts: CONTEXTS },
  'runtime.sendMessage': { permissions: [], contexts: CONTEXTS },
  'runtime.connect': { permissions: [], contexts: ['worker', 'popup', 'options'] },
  'storage.local.get': { permissions: ['storage'], contexts: CONTEXTS },
  'storage.local.set': { permissions: ['storage'], contexts: CONTEXTS },
  'storage.local.remove': { permissions: ['storage'], contexts: CONTEXTS },
  'storage.local.clear': { permissions: ['storage'], contexts: CONTEXTS },
  'storage.session.get': { permissions: ['storage'], contexts: ['worker', 'popup', 'options'] },
  'storage.session.set': { permissions: ['storage'], contexts: ['worker', 'popup', 'options'] },
  'storage.session.remove': { permissions: ['storage'], contexts: ['worker', 'popup', 'options'] },
  'storage.session.clear': { permissions: ['storage'], contexts: ['worker', 'popup', 'options'] },
  'proxy.settings.get': { permissions: ['proxy'], contexts: ['worker', 'popup', 'options'] },
  'proxy.settings.set': { permissions: ['proxy'], contexts: ['worker', 'popup', 'options'] },
  'proxy.settings.clear': { permissions: ['proxy'], contexts: ['worker', 'popup', 'options'] },
  'privacy.network.webRTCIPHandlingPolicy.get': { permissions: ['privacy'], contexts: ['worker', 'popup', 'options'] },
  'privacy.network.webRTCIPHandlingPolicy.set': { permissions: ['privacy'], contexts: ['worker', 'popup', 'options'] },
  'privacy.network.webRTCIPHandlingPolicy.clear': { permissions: ['privacy'], contexts: ['worker', 'popup', 'options'] },
  'tabs.create': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'tabs.query': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'tabs.get': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'tabs.update': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'tabs.remove': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'tabs.sendMessage': { permissions: ['tabs'], contexts: ['worker', 'popup', 'options'] },
  'scripting.executeScript': { permissions: ['scripting'], contexts: ['worker', 'popup', 'options'], hostAccess: true },
  'scripting.insertCSS': { permissions: ['scripting'], contexts: ['worker', 'popup', 'options'], hostAccess: true },
  'scripting.removeCSS': { permissions: ['scripting'], contexts: ['worker', 'popup', 'options'], hostAccess: true },
  'webRequest.onAuthRequired.addListener': { permissions: ['webRequest', 'webRequestAuthProvider'], contexts: ['worker'] },
  'webRequest.onAuthRequired.removeListener': { permissions: ['webRequest', 'webRequestAuthProvider'], contexts: ['worker'] },
  'idle.queryState': { permissions: ['idle'], contexts: ['worker', 'popup', 'options'] },
  'idle.setDetectionInterval': { permissions: ['idle'], contexts: ['worker'] },
  'notifications.create': { permissions: ['notifications'], contexts: ['worker', 'popup', 'options'] },
  'notifications.clear': { permissions: ['notifications'], contexts: ['worker', 'popup', 'options'] }
});

function methodPolicy(method) {
  return Object.prototype.hasOwnProperty.call(METHODS, method) ? METHODS[method] : null;
}
function missingPermissions(requirements, method, grantedOptional = []) {
  const policy = methodPolicy(method);
  if (!policy) return ['unknown-method'];
  const granted = new Set([...(requirements.permissions || []), ...grantedOptional]);
  return policy.permissions.filter(permission => !granted.has(permission));
}
function contextAllowed(method, kind) {
  const policy = methodPolicy(method);
  return !!policy && policy.contexts.includes(kind);
}

function globMatch(pattern, value) {
  // Linear-time '*' glob. A regex built from many '*' segments can backtrack
  // catastrophically on a long hostile path, so match iteratively instead.
  let p = 0, v = 0, star = -1, mark = 0;
  while (v < value.length) {
    if (p < pattern.length && pattern[p] === '*') { star = p++; mark = v; continue; }
    if (p < pattern.length && pattern[p] === value[v]) { p++; v++; continue; }
    if (star !== -1) { p = star + 1; v = ++mark; continue; }
    return false;
  }
  while (p < pattern.length && pattern[p] === '*') p++;
  return p === pattern.length;
}
function matchPattern(pattern, rawUrl, options = {}) {
  let url;
  try { url = new URL(rawUrl); } catch { return false; }
  if (url.username || url.password) return false;
  if (pattern === '<all_urls>') {
    return ['http:', 'https:', 'file:', 'ftp:', 'ws:', 'wss:'].includes(url.protocol);
  }
  const match = /^([a-z*]+):\/\/([^/]*)\/(.*)$/.exec(pattern);
  if (!match) return false;
  const [, scheme, host, rawPath] = match;
  const protocol = url.protocol.slice(0, -1);
  if (scheme === '*') { if (!['http', 'https'].includes(protocol)) return false; }
  else if (scheme !== protocol) return false;
  if (scheme === 'file') {
    // file:// permissions are hostless. The path still has to match.
  } else {
    const hostname = url.hostname.toLowerCase();
    if (host !== '*') {
      if (host.startsWith('*.')) {
        const base = host.slice(2).toLowerCase();
        if (hostname !== base && !hostname.endsWith('.' + base)) return false;
      } else if (hostname !== host.toLowerCase()) return false;
    }
  }
  // Host permissions are origin grants. Proton publishes "https://*/", and
  // Chromium's short form must not become a one-path-only VPN permission.
  if (options.ignorePath) return true;
  const path = rawPath === '' ? '/*' : '/' + rawPath;
  return globMatch(path, url.pathname);
}
function hasHostAccess(requirements, url, grantedOptionalHosts = []) {
  const patterns = [...(requirements.hostPermissions || []), ...grantedOptionalHosts];
  return patterns.some(pattern => matchPattern(pattern, url, { ignorePath: true }));
}
function allowsProxyAuth(requirements, grantedOptional = []) {
  const granted = new Set([...(requirements.permissions || []), ...grantedOptional]);
  if (!granted.has('webRequest') || !granted.has('webRequestAuthProvider')) return false;
  const hosts = requirements.hostPermissions || [];
  const covers = scheme => hosts.some(pattern => pattern === '<all_urls>' || pattern === '*://*/*' ||
    pattern === `${scheme}://*/*` || pattern === `${scheme}://*/`);
  return covers('http') && covers('https');
}

module.exports = { METHODS, methodPolicy, missingPermissions, contextAllowed, matchPattern, hasHostAccess, allowsProxyAuth };
