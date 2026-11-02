'use strict';
// PsychoVault extension manifest reader. This file is my own MIT-licensed code.
// It validates and describes a package; it does not execute extension code or
// claim that an API named here is implemented by the browser.

const MAX_MANIFEST_BYTES = 1024 * 1024;
const MAX_DEPTH = 32;
const KNOWN_PERMISSIONS = new Set([
  'activeTab', 'alarms', 'bookmarks', 'cookies', 'contextMenus', 'declarativeNetRequest',
  'declarativeNetRequestWithHostAccess', 'downloads', 'favicon', 'fontSettings', 'geolocation',
  'history', 'idle', 'management', 'nativeMessaging', 'notifications', 'offscreen',
  'privacy', 'proxy', 'scripting', 'sessions', 'storage', 'tabGroups', 'tabs',
  'topSites', 'unlimitedStorage', 'webNavigation', 'webRequest', 'webRequestAuthProvider'
]);
const SCHEMES = new Set(['http', 'https', 'ws', 'wss', 'ftp', 'file']);

function fail(message) { throw new Error(message); }
function object(value, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) fail(`${label} must be an object`);
  return value;
}
function text(value, label, max = 512) {
  if (typeof value !== 'string' || !value.trim() || value.length > max || /[\u0000-\u001f]/.test(value)) {
    fail(`${label} must be a non-empty string of at most ${max} characters`);
  }
  return value;
}
function strings(value, label, maxItems = 512) {
  if (!Array.isArray(value) || value.length > maxItems) fail(`${label} must be an array of at most ${maxItems} items`);
  return [...new Set(value.map(item => text(item, label, 2048)))];
}
function safeResourcePath(value, label = 'resource path') {
  text(value, label, 1024);
  let decoded;
  try { decoded = decodeURIComponent(value); } catch { fail(`${label} has invalid escaping`); }
  if (value.startsWith('/') || value.includes('\\') || /^[a-z][a-z0-9+.-]*:/i.test(value) ||
      /[?#]/.test(value) || /[\u0000-\u001f]/.test(decoded) ||
      decoded.split('/').some(part => part === '..' || part === '.' || part === '')) {
    fail(`${label} must stay inside the extension package`);
  }
  return value;
}
function matchPattern(value) {
  text(value, 'match pattern', 2048);
  if (value === '<all_urls>') return value;
  const match = /^([a-z*]+):\/\/([^/]*)\/(.*)$/.exec(value);
  if (!match) fail('invalid match pattern');
  const [, scheme, host, path] = match;
  if (scheme !== '*' && !SCHEMES.has(scheme)) fail('unsupported match-pattern scheme');
  if (scheme === 'file' ? host !== '' : !host) fail('invalid match-pattern host');
  if (host && host !== '*' && !/^(\*\.)?[^*@:/\\]+$/.test(host)) fail('invalid match-pattern host');
  // Chromium accepts "https://*/" (a slash with an empty path) as well as
  // "https://*/*". Proton's published manifest uses the shorter form.
  if (/[?#\\]/.test(path)) fail('invalid match-pattern path');
  return value;
}
function uniqueKeys(source) {
  // JSON.parse keeps the last duplicate key. A permission manifest must not
  // have two meanings, so check object keys before parsing. Escaped keys are
  // decoded too: "proxy" and "\u0070roxy" are the same key.
  const stack = [];
  for (let i = 0; i < source.length; i += 1) {
    const ch = source[i];
    if (ch === '"') {
      const start = i;
      i += 1;
      while (i < source.length) {
        if (source[i] === '\\') { i += 2; continue; }
        if (source[i++] === '"') break;
      }
      const token = source.slice(start, i);
      const top = stack[stack.length - 1];
      if (top && top.object && top.key) {
        const key = JSON.parse(token);
        if (top.keys.has(key)) fail('duplicate manifest key');
        top.keys.add(key);
      }
      continue;
    }
    if (ch === '{') { stack.push({ object: true, key: true, keys: new Set() }); continue; }
    if (ch === '[') { stack.push({ object: false }); continue; }
    if (ch === '}' || ch === ']') { stack.pop(); continue; }
    const top = stack[stack.length - 1];
    if (top && top.object && ch === ':') top.key = false;
    if (top && top.object && ch === ',') top.key = true;
  }
}
function freeze(value, depth = 0) {
  if (depth > MAX_DEPTH) fail('manifest is nested too deeply');
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child, depth + 1);
    Object.freeze(value);
  }
  return value;
}
function csp(manifest) {
  if (manifest.content_security_policy === undefined) return "script-src 'self'; object-src 'self'";
  const policy = object(manifest.content_security_policy, 'content_security_policy');
  const source = text(policy.extension_pages, 'extension_pages CSP', 8192);
  const directives = new Map();
  for (const part of source.split(';')) {
    const tokens = part.trim().split(/\s+/).filter(Boolean);
    if (!tokens.length) continue;
    const directive = tokens[0].toLowerCase();
    if (directives.has(directive)) fail('duplicate CSP directive');
    directives.set(directive, tokens.slice(1).map(token => token.toLowerCase()));
  }
  // Check the effective source list, not just an explicit script-src. A remote
  // default-src with no script-src must not become a remote-code hole.
  const fallback = directives.get('default-src') || ["'self'"];
  for (const directive of ['script-src', 'script-src-elem', 'script-src-attr', 'worker-src', 'object-src']) {
    const values = directives.get(directive) || fallback;
    if (values.some(token => !["'self'", "'none'"].includes(token))) {
      fail('extension CSP permits remote or unsafe code');
    }
  }
  return source;
}
function parseManifest(source) {
  if (typeof source !== 'string' || new TextEncoder().encode(source).length > MAX_MANIFEST_BYTES) {
    fail('manifest must be a string of at most 1 MiB');
  }
  let manifest;
  try { JSON.parse(source); uniqueKeys(source); manifest = JSON.parse(source); }
  catch (error) { fail(error.message === 'duplicate manifest key' ? error.message : 'manifest is not valid JSON'); }
  object(manifest, 'manifest');
  if (manifest.manifest_version !== 3) fail('only Manifest V3 is accepted by this engine stage');
  text(manifest.name, 'name', 128);
  text(manifest.version, 'version', 64);
  if (!/^(0|[1-9]\d{0,4})(\.(0|[1-9]\d{0,4})){0,3}$/.test(manifest.version)) fail('invalid extension version');
  const permissions = strings(manifest.permissions || [], 'permissions');
  const optional = strings(manifest.optional_permissions || [], 'optional_permissions');
  const hosts = strings(manifest.host_permissions || [], 'host_permissions').map(matchPattern);
  const optionalHosts = strings(manifest.optional_host_permissions || [], 'optional_host_permissions').map(matchPattern);
  const background = object(manifest.background || {}, 'background');
  if (background.scripts !== undefined) fail('MV2 background pages are not supported by this engine stage');
  const worker = background.service_worker === undefined ? '' : safeResourcePath(background.service_worker, 'service worker');
  if (background.type !== undefined && !['classic', 'module'].includes(background.type)) fail('invalid service-worker type');
  const action = object(manifest.action || {}, 'action');
  const popup = action.default_popup === undefined ? '' : safeResourcePath(action.default_popup, 'popup');
  const options = object(manifest.options_ui || {}, 'options_ui');
  const optionsPage = options.page === undefined ? '' : safeResourcePath(options.page, 'options page');
  const contentScripts = manifest.content_scripts === undefined ? [] : manifest.content_scripts;
  if (!Array.isArray(contentScripts) || contentScripts.length > 128) fail('invalid content_scripts');
  for (const script of contentScripts) {
    object(script, 'content script');
    strings(script.matches || [], 'content-script matches').map(matchPattern);
    strings(script.js || [], 'content-script JS').forEach(path => safeResourcePath(path));
    strings(script.css || [], 'content-script CSS').forEach(path => safeResourcePath(path));
    if (script.world !== undefined && !['ISOLATED', 'MAIN'].includes(script.world)) fail('invalid content-script world');
  }
  const external = object(manifest.externally_connectable || {}, 'externally_connectable');
  strings(external.matches || [], 'externally_connectable matches').map(matchPattern);
  const policy = csp(manifest);
  freeze(manifest);
  return freeze({
    manifest,
    requirements: {
      permissions, optionalPermissions: optional,
      unknownPermissions: permissions.filter(permission => !KNOWN_PERMISSIONS.has(permission)),
      hostPermissions: hosts, optionalHostPermissions: optionalHosts,
      worker, workerType: background.type || 'classic', popup, optionsPage,
      contentScripts: contentScripts.length,
      externalMatches: external.matches || [], csp: policy
    }
  });
}

module.exports = { parseManifest, safeResourcePath, matchPattern, KNOWN_PERMISSIONS, MAX_MANIFEST_BYTES };
