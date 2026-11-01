#!/usr/bin/env node
// Static Android source audit. This is not a Chromium compile or a device test.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const defects = [];
function read(p){return fs.readFileSync(path.join(root,p),'utf8');}
function check(name, fn){try{fn();}catch(e){defects.push(name+': '+e.message);}}
function assert(ok,msg){if(!ok)throw new Error(msg);}

check('JavaScript syntax', () => {
  for (const dir of ['ui','core','scripts','test']) {
    for (const file of fs.readdirSync(path.join(root,dir))) {
      if (!/\.(cjs|js)$/.test(file)) continue;
      const text = read(path.join(dir,file));
      if (file.endsWith('.cjs')) new vm.Script(text, {filename:file});
      else new vm.Script(text, {filename:file});
    }
  }
});
check('bridge has one annotated entry point', () => {
  const bridge = read('engine/psychovault/java/org/chromium/chrome/browser/psychovault/PsychoBridge.java');
  assert((bridge.match(/@JavascriptInterface/g)||[]).length === 1, 'expected one @JavascriptInterface');
  assert(bridge.includes('ThreadUtils.postOnUiThread'), 'missing UI-thread marshal');
  assert(bridge.includes('getLastCommittedUrl()'), 'missing committed URL check');
  assert(bridge.includes('settings.html'), 'settings document not allowlisted');
});
check('history gates', () => {
  const host = read('engine/psychovault/java/org/chromium/chrome/browser/psychovault/PsychoChromeHost.java');
  for (const gate of ['hasCommitted()','isErrorPage()','isSameDocument()','isInPrimaryMainFrame()','isOffTheRecord()','optBoolean("remember", true)']) assert(host.includes(gate), 'missing '+gate);
});
check('settings allowlist and private boundary', () => {
  const store = read('engine/psychovault/java/org/chromium/chrome/browser/psychovault/PsychoProfileStore.java');
  assert(store.includes('Private windows do not save settings'), 'private write boundary missing');
  assert(store.includes('ALLOWED.contains'), 'settings allowlist missing');
  assert(store.includes('getProfileName'), 'profile-scoped storage missing');
});
check('extension identity and archive safety', () => {
  const ext = read('engine/psychovault/java/org/chromium/chrome/browser/psychovault/PsychoExtensions.java');
  assert(ext.includes('jplgfhpmjnbigmhklmmbgecoobifkmpa'), 'Proton ID missing');
  assert(ext.includes('Unsafe extension archive path'), 'zip traversal guard missing');
  assert(ext.includes('isOffTheRecord()'), 'OTR extension boundary missing');
  const native = read('engine/psychovault/native/psycho_extensions.cc');
  assert(native.includes('RegularProfile'), 'native OTR guard missing');
  assert(native.includes('UNINSTALL_REASON_USER_INITIATED'), 'uninstall reason missing');
});
check('asset parity', () => {
  const gn = read('engine/psychovault/BUILD.gn');
  const native = read('engine/psychovault/native/psycho_ui.cc');
  const staged = new Set([...gn.matchAll(/"ui\/([^"]+)"/g)].map(m=>m[1]));
  const served = new Set([...native.matchAll(/\{"([^"]+)", "[^"]+"\}/g)].map(m=>m[1]));
  assert(staged.size === served.size && [...staged].every(x=>served.has(x)), 'staged/served asset mismatch');
});
check('no committed secrets or build outputs', () => {
  const files = require('node:child_process').execSync('git ls-files', {cwd:path.resolve(root,'..'), encoding:'utf8'}).split('\n');
  for (const f of files) assert(!/(\.apk|\.aab|\.jks|\.keystore|__pycache__|\.pyc|node_modules)/.test(f), 'tracked generated/sensitive file '+f);
});
if (defects.length) { console.error(defects.join('\n')); process.exit(1); }
console.log('Android static source audit: no defects. This is not a compile or device test.');
