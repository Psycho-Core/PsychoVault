// Password-protected Android vault. Storage contains only encrypted backup-format data.
(function (root) {
  'use strict';
  const codec = typeof module !== 'undefined' && module.exports ? require('./vault-backup') : root.PsychoVaultBackup;
  const KEY = 'psychovault.android.vault.v1';
  function open(storage) {
    let entries = null, password = null, observed = null, generation = 0, busy = false;
    function lock() { generation++; entries = null; password = null; observed = null; }
    function current() { return storage.getItem(KEY); }
    async function exclusive(fn) {
      if (busy) throw new Error('Vault is busy.');
      busy = true;
      try { return await fn(generation); } finally { busy = false; }
    }
    function check(token) { if (token !== generation) throw new Error('Vault was locked.'); }
    async function create(master) {
      return exclusive(async token => {
        if (current() !== null) throw new Error('A vault already exists.');
        const raw = await codec.encrypt([], master);
        check(token);
        if (current() !== null) throw new Error('A vault already exists.');
        storage.setItem(KEY, raw);
        observed = raw; entries = []; password = master;
      });
    }
    async function unlock(master) {
      return exclusive(async token => {
        const raw = current();
        if (raw === null) throw new Error('No vault exists.');
        const decoded = await codec.decrypt(raw, master);
        check(token);
        if (current() !== raw) throw new Error('Vault changed while unlocking.');
        entries = decoded; password = master; observed = raw;
      });
    }
    async function write(next, token) {
      if (!entries || password === null) throw new Error('Vault is locked.');
      const raw = await codec.encrypt(next, password);
      check(token);
      if (current() !== observed) throw new Error('Vault changed outside this session. Lock and reopen it.');
      storage.setItem(KEY, raw);
      observed = raw; entries = next;
    }
    return {
      exists: () => current() !== null,
      unlocked: () => entries !== null,
      list: () => { if (!entries) throw new Error('Vault is locked.'); return entries.map(e => ({ ...e })); },
      lock, create, unlock,
      add(record) {
        return exclusive(async token => {
          if (!entries) throw new Error('Vault is locked.');
          if (!record || typeof record.site !== 'string' || typeof record.user !== 'string' || typeof record.pass !== 'string' || typeof record.note !== 'string' || !record.pass || record.user.length > 200 || record.pass.length > 512 || record.note.length > 2000) throw new Error('Invalid vault entry.');
          let site;
          try { const url = new URL(/^https?:\/\//i.test(record.site) ? record.site : 'https://' + record.site); if (!['http:', 'https:'].includes(url.protocol) || !url.hostname || url.username || url.password) throw new Error('invalid'); site = url.hostname.toLowerCase(); } catch { throw new Error('Enter a valid website.'); }
          if (entries.some(e => String(e.site).toLowerCase() === site && e.user === record.user)) throw new Error('That website and username already exist.');
          const id = crypto.randomUUID();
          await write([...entries, { id, site, user: record.user, pass: record.pass, note: record.note, at: Date.now() }], token);
        });
      },
      remove(id) {
        return exclusive(async token => {
          if (!entries) throw new Error('Vault is locked.');
          if (!entries.some(e => e.id === id)) throw new Error('Entry not found.');
          await write(entries.filter(e => e.id !== id), token);
        });
      }
    };
  }
  if (typeof module !== 'undefined' && module.exports) module.exports = { open }; else root.PsychoVault = { open };
})(typeof window === 'undefined' ? globalThis : window);
