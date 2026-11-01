// Desktop-compatible encrypted vault backup codec. No plaintext is persisted here.
(function (root) {
  'use strict';
  const scrypt = typeof module !== 'undefined' && module.exports ? require('scrypt-js').scrypt : root.scrypt.scrypt;
  const LIMIT = 10 * 1024 * 1024;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder('utf-8', { fatal: true });
  function to64(bytes) {
    let text = '';
    for (let i = 0; i < bytes.length; i += 8192) text += String.fromCharCode(...bytes.subarray(i, i + 8192));
    return btoa(text);
  }
  function from64(text, max) {
    if (typeof text !== 'string' || text.length > Math.ceil(max / 3) * 4 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text)) throw new Error('Invalid backup.');
    const decoded = atob(text);
    if (decoded.length > max) throw new Error('Invalid backup.');
    return Uint8Array.from(decoded, c => c.charCodeAt(0));
  }
  async function derive(password, salt) {
    const passwordBytes = encoder.encode(password);
    let raw;
    try {
      raw = await scrypt(passwordBytes, salt, 16384, 8, 1, 32);
      return await crypto.subtle.importKey('raw', raw, { name: 'AES-GCM' }, false, ['encrypt', 'decrypt']);
    } finally { passwordBytes.fill(0); if (raw) raw.fill(0); }
  }
  async function encrypt(entries, password) {
    if (typeof password !== 'string' || password.length < 12) throw new Error('Backup password must be at least 12 characters.');
    if (!Array.isArray(entries) || entries.length > 100000) throw new Error('Invalid backup entries.');
    const plaintext = encoder.encode(JSON.stringify(entries));
    if (plaintext.length > LIMIT) { plaintext.fill(0); throw new Error('Vault is too large to export.'); }
    try {
      const salt = crypto.getRandomValues(new Uint8Array(16));
      const iv = crypto.getRandomValues(new Uint8Array(12));
      const key = await derive(password, salt);
      const sealed = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv, tagLength: 128 }, key, plaintext));
      return JSON.stringify({ format: 'psychovault-backup', version: 1, kdf: 'scrypt-16384-8-1', salt: to64(salt), iv: to64(iv), tag: to64(sealed.subarray(sealed.length - 16)), data: to64(sealed.subarray(0, sealed.length - 16)) });
    } finally { plaintext.fill(0); }
  }
  async function decrypt(text, password) {
    if (typeof text !== 'string' || text.length > LIMIT * 2 || encoder.encode(text).length > LIMIT * 2) throw new Error('Backup is too large.');
    if (typeof password !== 'string') throw new Error('Invalid password.');
    const data = JSON.parse(text);
    if (!data || data.format !== 'psychovault-backup' || data.version !== 1 || data.kdf !== 'scrypt-16384-8-1') throw new Error('Unsupported backup format.');
    const salt = from64(data.salt, 16), iv = from64(data.iv, 12), tag = from64(data.tag, 16), body = from64(data.data, LIMIT);
    if (salt.length !== 16 || iv.length !== 12 || tag.length !== 16) throw new Error('Invalid backup.');
    const sealed = new Uint8Array(body.length + tag.length);
    sealed.set(body); sealed.set(tag, body.length);
    const key = await derive(password, salt);
    const plaintext = new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-GCM', iv, tagLength: 128 }, key, sealed));
    try {
      const entries = JSON.parse(decoder.decode(plaintext));
      if (!Array.isArray(entries) || entries.length > 100000) throw new Error('Invalid backup entries.');
      return entries;
    } finally { plaintext.fill(0); }
  }
  const api = { encrypt, decrypt };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.PsychoVaultBackup = api;
})(typeof window === 'undefined' ? globalThis : window);
