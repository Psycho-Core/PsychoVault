'use strict'
const crypto = require('node:crypto')
const LIMIT = 10 * 1024 * 1024
function keyFor(password, salt) { return crypto.scryptSync(password, salt, 32, { N: 16384, r: 8, p: 1, maxmem: 64 * 1024 * 1024 }) }
function encryptBackup(entries, password) {
  if (typeof password !== 'string' || password.length < 12) throw new Error('Backup password must be at least 12 characters.')
  const text = JSON.stringify(entries)
  if (Buffer.byteLength(text) > LIMIT) throw new Error('Vault is too large to export.')
  const salt = crypto.randomBytes(16), iv = crypto.randomBytes(12)
  const cipher = crypto.createCipheriv('aes-256-gcm', keyFor(password, salt), iv)
  const ciphertext = Buffer.concat([cipher.update(text, 'utf8'), cipher.final()])
  return JSON.stringify({ format: 'psychovault-backup', version: 1, kdf: 'scrypt-16384-8-1', salt: salt.toString('base64'), iv: iv.toString('base64'), tag: cipher.getAuthTag().toString('base64'), data: ciphertext.toString('base64') })
}
function decryptBackup(text, password) {
  if (Buffer.byteLength(text) > LIMIT * 2) throw new Error('Backup is too large.')
  const o = JSON.parse(text)
  if (!o || o.format !== 'psychovault-backup' || o.version !== 1 || o.kdf !== 'scrypt-16384-8-1') throw new Error('Unsupported backup format.')
  const salt = Buffer.from(o.salt || '', 'base64'), iv = Buffer.from(o.iv || '', 'base64'), tag = Buffer.from(o.tag || '', 'base64'), body = Buffer.from(o.data || '', 'base64')
  if (salt.length !== 16 || iv.length !== 12 || tag.length !== 16 || body.length > LIMIT) throw new Error('Invalid backup.')
  const decipher = crypto.createDecipheriv('aes-256-gcm', keyFor(password, salt), iv); decipher.setAuthTag(tag)
  const data = JSON.parse(Buffer.concat([decipher.update(body), decipher.final()]).toString('utf8'))
  if (!Array.isArray(data) || data.length > 100000) throw new Error('Invalid backup entries.')
  return data
}
function parseCSV(text) {
  if (Buffer.byteLength(text) > LIMIT) throw new Error('CSV file is too large.')
  text = text.replace(/^\uFEFF/, '')
  const rows = []; let row = [], field = '', quoted = false, closed = false
  for (let i = 0; i < text.length; i++) {
    const c = text[i]
    if (quoted) { if (c === '"' && text[i + 1] === '"') { field += '"'; i++ } else if (c === '"') { quoted = false; closed = true } else field += c; continue }
    if (c === '"' && !field && !closed) { quoted = true; continue }
    if (c === ',' || c === '\n' || c === '\r') { row.push(field); field = ''; closed = false; if (c !== ',') { rows.push(row); row = []; if (c === '\r' && text[i+1] === '\n') i++ } continue }
    if (c === '"' || closed) throw new Error('Malformed CSV quoting.')
    field += c
  }
  if (quoted) throw new Error('Unclosed CSV quote.')
  if (field || row.length || closed) { row.push(field); rows.push(row) }
  if (!rows.length) throw new Error('Empty CSV file.')
  const headers = rows.shift().map(x => x.trim().toLowerCase())
  const get = (r, names) => { const i = headers.findIndex(h => names.includes(h)); return i < 0 ? '' : (r[i] || '') }
  if (!headers.some(h => ['url', 'origin', 'website'].includes(h)) || !headers.includes('password')) throw new Error('CSV must contain url and password columns.')
  return rows.map(r => ({ site: get(r, ['url','origin','website']), user: get(r, ['username','user','login']), pass: get(r, ['password']), note: get(r, ['note','notes']) }))
}
function prepareEntries(entries, existing) {
  const keys = new Set(existing.map(x => String(x.site).toLowerCase() + '\0' + String(x.user)))
  let skipped = 0; const accepted = []
  for (const e of entries) {
    if (!e || typeof e.site !== 'string' || typeof e.pass !== 'string' || !e.pass || e.pass.length > 512 || typeof (e.user || '') !== 'string' || String(e.user || '').length > 200 || String(e.note || '').length > 2000) { skipped++; continue }
    let u; try { u = new URL(/^https?:\/\//i.test(e.site) ? e.site : 'https://' + e.site) } catch { skipped++; continue }
    if (!['http:', 'https:'].includes(u.protocol) || u.username || u.password || !u.hostname) { skipped++; continue }
    const site = u.hostname.toLowerCase(); const user = e.user || ''; const k = site + '\0' + user
    if (keys.has(k)) { skipped++; continue }
    keys.add(k); accepted.push({ id: crypto.randomUUID(), site, user, pass: e.pass, note: e.note || '', at: Date.now() })
  }
  return { accepted, skipped }
}
module.exports = { encryptBackup, decryptBackup, parseCSV, prepareEntries }
