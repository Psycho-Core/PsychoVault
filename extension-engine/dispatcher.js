'use strict';
// PsychoVault extension request dispatcher. This is my own MIT-licensed code.
// It is a policy boundary, not a JavaScript sandbox. The embedding browser must
// still run extension code in an isolated renderer/worker with no Node or
// privileged browser bridge. This file never evaluates extension code.

const { parseManifest, safeResourcePath, KNOWN_PERMISSIONS } = require('./manifest');
const { methodPolicy, missingPermissions, contextAllowed, hasHostAccess } = require('./permissions');

const EXTENSION_ID = /^[a-p]{32}$/;
const PROFILE_ID = /^[A-Za-z0-9_.-]{1,128}$/;
const KINDS = new Set(['worker', 'popup', 'options', 'content']);
const PUBLIC_CODES = new Set([
  'invalid-params', 'invalid-request', 'invalid-context', 'invalid-profile', 'invalid-extension',
  'invalid-grant', 'invalid-handler', 'unsupported-permission', 'not-found', 'disabled',
  'unknown-method', 'duplicate-handler', 'context-denied', 'permission-denied', 'host-denied',
  'busy', 'unimplemented', 'timeout', 'invalid-result', 'result-too-large'
]);

function fail(code, message) { const error = new Error(message); error.code = code; error.pvPublic = true; throw error; }
function jsonClone(value, label, maxBytes) {
  let text;
  try { text = JSON.stringify(value); } catch { fail(label === 'result' ? 'invalid-result' : 'invalid-params', `${label} is not JSON-serializable`); }
  if (text === undefined) fail(label === 'result' ? 'invalid-result' : 'invalid-params', `${label} is not JSON-serializable`);
  if (maxBytes && new TextEncoder().encode(text).length > maxBytes) fail(label === 'result' ? 'result-too-large' : 'invalid-params', `${label} exceeds the size limit`);
  return JSON.parse(text);
}
function key(profileId, extensionId) { return JSON.stringify([profileId, extensionId]); }

class ExtensionEngine {
  constructor(options = {}) {
    this.timeoutMs = Number.isInteger(options.timeoutMs) && options.timeoutMs > 0 ? options.timeoutMs : 30000;
    this.maxRequestBytes = Number.isInteger(options.maxRequestBytes) && options.maxRequestBytes > 0 ? options.maxRequestBytes : 1024 * 1024;
    this.maxResultBytes = Number.isInteger(options.maxResultBytes) && options.maxResultBytes > 0 ? options.maxResultBytes : 1024 * 1024;
    this.maxInflight = Number.isInteger(options.maxInflight) && options.maxInflight > 0 ? options.maxInflight : 64;
    this.resolveTabUrl = typeof options.resolveTabUrl === 'function' ? options.resolveTabUrl : null;
    this.extensions = new Map();
    this.contexts = new WeakMap();
    this.handlers = new Map();
    this.inflight = new Map();
    this._registerBuiltins();
  }

  registerExtension(input) {
    const profileId = String(input.profileId || '');
    const extensionId = String(input.extensionId || '');
    if (!PROFILE_ID.test(profileId) || profileId === '.' || profileId === '..') fail('invalid-profile', 'invalid profile id');
    if (!EXTENSION_ID.test(extensionId)) fail('invalid-extension', 'invalid extension id');
    const parsed = parseManifest(input.manifest);
    if (parsed.requirements.unknownPermissions.length) {
      fail('unsupported-permission', `unsupported permissions: ${parsed.requirements.unknownPermissions.join(', ')}`);
    }
    const optional = new Set(parsed.requirements.optionalPermissions);
    const unknownOptional = parsed.requirements.optionalPermissions.filter(permission => !KNOWN_PERMISSIONS.has(permission));
    if (unknownOptional.length) fail('unsupported-permission', `unsupported optional permissions: ${unknownOptional.join(', ')}`);
    const grantedOptional = [...new Set(input.grantedOptionalPermissions || [])];
    if (grantedOptional.some(permission => !optional.has(permission))) fail('invalid-grant', 'optional permission was not requested');
    const optionalHosts = new Set(parsed.requirements.optionalHostPermissions);
    const grantedOptionalHosts = [...new Set(input.grantedOptionalHostPermissions || [])];
    if (grantedOptionalHosts.some(host => !optionalHosts.has(host))) fail('invalid-grant', 'optional host was not requested');
    const record = {
      profileId, extensionId, parsed, grantedOptional, grantedOptionalHosts,
      enabled: input.enabled !== false
    };
    this.extensions.set(key(profileId, extensionId), record);
    return { profileId, extensionId, name: parsed.manifest.name, version: parsed.manifest.version };
  }

  setEnabled(profileId, extensionId, enabled) {
    const record = this.extensions.get(key(profileId, extensionId));
    if (!record) fail('not-found', 'extension is not installed in this profile');
    record.enabled = !!enabled;
  }

  removeExtension(profileId, extensionId) {
    this.extensions.delete(key(profileId, extensionId));
  }

  createToken(input) {
    const profileId = String(input.profileId || '');
    const extensionId = String(input.extensionId || '');
    const kind = String(input.kind || '');
    const origin = String(input.origin || '');
    const record = this.extensions.get(key(profileId, extensionId));
    if (!record || !record.enabled) fail('not-found', 'extension is not enabled in this profile');
    if (!KINDS.has(kind)) fail('invalid-context', 'invalid extension context');
    if (kind === 'content') {
      try { const url = new URL(origin); if (!['http:', 'https:'].includes(url.protocol)) throw new Error(); }
      catch { fail('invalid-context', 'content-script origin must be an HTTP(S) page'); }
    } else if (origin !== `chrome-extension://${extensionId}`) {
      fail('invalid-context', 'extension-page origin does not match its id');
    }
    const token = Object.freeze({});
    this.contexts.set(token, { profileId, extensionId, kind, origin, record });
    return token;
  }

  registerHandler(method, handler) {
    if (!methodPolicy(method)) fail('unknown-method', `cannot register unknown method: ${method}`);
    if (typeof handler !== 'function') fail('invalid-handler', 'handler must be a function');
    if (this.handlers.has(method)) fail('duplicate-handler', `handler already registered: ${method}`);
    this.handlers.set(method, handler);
  }

  _registerBuiltins() {
    this.registerHandler('runtime.getManifest', context => context.record.parsed.manifest);
    this.registerHandler('runtime.getURL', (context, params) => {
      const path = safeResourcePath(params[0], 'resource path');
      return `chrome-extension://${context.extensionId}/${path}`;
    });
  }

  async _authorize(context, method, params) {
    const policy = methodPolicy(method);
    if (!policy) fail('unknown-method', 'unknown extension method');
    if (!context.record.enabled) fail('disabled', 'extension is disabled');
    if (!contextAllowed(method, context.kind)) fail('context-denied', 'this context cannot call the method');
    const missing = missingPermissions(context.record.parsed.requirements, method, context.record.grantedOptional);
    if (missing.length) fail('permission-denied', `missing permissions: ${missing.join(', ')}`);
    if (policy.hostAccess) {
      if (!this.resolveTabUrl) fail('host-denied', 'no trusted tab URL resolver is available');
      const target = params[0] && params[0].target;
      if (!target || !Number.isInteger(target.tabId)) fail('invalid-params', 'scripting requires a target tabId');
      const url = await this.resolveTabUrl(context.profileId, target.tabId);
      if (typeof url !== 'string' || !hasHostAccess(context.record.parsed.requirements, url, context.record.grantedOptionalHosts)) {
        fail('host-denied', 'extension does not have access to the target page');
      }
      // The resolver can await a navigation. Re-check live installation state
      // before the handler runs so disable/remove cannot race the grant.
      if (this.extensions.get(key(context.profileId, context.extensionId)) !== context.record) fail('not-found', 'extension is no longer installed in this profile');
      if (!context.record.enabled) fail('disabled', 'extension is disabled');
    }
  }

  async dispatch(token, request) {
    const context = this.contexts.get(token);
    if (!context) return { ok: false, error: { code: 'invalid-context', message: 'untrusted context token' } };
    let requestId = '';
    try {
      if (!request || typeof request !== 'object' || Array.isArray(request)) fail('invalid-request', 'request must be an object');
      if (!['string', 'number'].includes(typeof request.id)) fail('invalid-request', 'request id must be a string or number');
      if (typeof request.id === 'number' && !Number.isFinite(request.id)) fail('invalid-request', 'request id must be finite');
      requestId = String(request.id);
      if (!requestId || requestId.length > 128) fail('invalid-request', 'request id is required');
      if (typeof request.method !== 'string') fail('invalid-request', 'method must be a string');
      const method = request.method;
      if (this.extensions.get(key(context.profileId, context.extensionId)) !== context.record) {
        fail('not-found', 'extension is no longer installed in this profile');
      }
      const params = request.params === undefined ? [] : request.params;
      if (!Array.isArray(params)) fail('invalid-params', 'params must be an array');
      let bytes;
      try { bytes = JSON.stringify(params); } catch { fail('invalid-params', 'params are not JSON-serializable'); }
      if (bytes === undefined || new TextEncoder().encode(bytes).length > this.maxRequestBytes) fail('invalid-params', 'params exceed the size limit');
      const cleanParams = jsonClone(params, 'params');
      const count = this.inflight.get(token) || 0;
      if (count >= this.maxInflight) fail('busy', 'too many in-flight extension requests');
      this.inflight.set(token, count + 1);
      let released = false;
      const release = () => {
        if (released) return;
        released = true;
        const left = (this.inflight.get(token) || 1) - 1;
        if (left <= 0) this.inflight.delete(token); else this.inflight.set(token, left);
      };
      let handlerStarted = false;
      try {
        await this._authorize(context, method, cleanParams);
        if (this.extensions.get(key(context.profileId, context.extensionId)) !== context.record) fail('not-found', 'extension is no longer installed in this profile');
        if (!context.record.enabled) fail('disabled', 'extension is disabled');
        const handler = this.handlers.get(method);
        if (!handler) fail('unimplemented', 'the browser has not implemented this method');
        const controller = new AbortController();
        handlerStarted = true;
        let timer;
        const timeout = new Promise((_, reject) => {
          timer = setTimeout(() => {
            controller.abort();
            const error = new Error('extension request timed out');
            error.code = 'timeout'; error.pvPublic = true;
            reject(error);
          }, this.timeoutMs);
        });
        const handlerPromise = Promise.resolve().then(() => handler(Object.freeze({
          profileId: context.profileId, extensionId: context.extensionId,
          kind: context.kind, origin: context.origin, record: context.record
        }), cleanParams, controller.signal));
        // A timeout rejects the caller, but the slot stays held until the
        // handler actually settles. A handler that ignores AbortSignal cannot
        // create unlimited background work by being timed out over and over.
        handlerPromise.then(() => { clearTimeout(timer); release(); }, () => { clearTimeout(timer); release(); });
        const result = await Promise.race([handlerPromise, timeout]);
        return { id: requestId, ok: true, result: jsonClone(result === undefined ? null : result, 'result', this.maxResultBytes) };
      } catch (error) {
        // Once the handler has started, its settlement owns the slot. Releasing
        // here on timeout would let a stuck handler be restarted without limit.
        if (!handlerStarted) release();
        throw error;
      }
    } catch (error) {
      const code = error && error.pvPublic === true && typeof error.code === 'string' && PUBLIC_CODES.has(error.code) ? error.code : 'internal';
      const message = code === 'internal' ? 'extension request failed' : String(error.message || 'extension request failed');
      return { id: requestId, ok: false, error: { code, message } };
    }
  }
}

module.exports = { ExtensionEngine };
