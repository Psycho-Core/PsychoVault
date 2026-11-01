const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const root=path.resolve(__dirname,'..'), repo=path.resolve(root,'..');
const script=path.join(root,'scripts/prepare-ui.cjs');
test('mobile chrome is generated from the desktop UI and does not fabricate a native bridge',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'pv-mobile-ui-'));
  try {
    const dest=path.join(dir,'chrome'); const result=spawnSync(process.execPath,[script,dest],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
    assert.ok(fs.existsSync(path.join(dest,'library.js'))); assert.ok(fs.existsSync(path.join(dest,'library-transport.js'))); assert.ok(fs.existsSync(path.join(dest,'indexeddb-storage.js')));
    assert.equal(fs.readFileSync(path.join(dest,'renderer.js'),'utf8'),fs.readFileSync(path.join(repo,'app/ui/renderer.js'),'utf8'));
    assert.equal(fs.readFileSync(path.join(dest,'style.css'),'utf8'),fs.readFileSync(path.join(repo,'app/ui/style.css'),'utf8'));
    const html=fs.readFileSync(path.join(dest,'index.html'),'utf8'); assert.match(html,/id="mobile-rail-toggle"/);assert.match(html,/mobile-shell\.js/);assert.doesNotMatch(html,/<script src="renderer\.js">/);
    assert.match(fs.readFileSync(path.join(dest,'mobile-shell.js'),'utf8'),/if \(!window\.pv\)/);
    const again=spawnSync(process.execPath,[script,dest],{encoding:'utf8'});assert.notEqual(again.status,0);assert.ok(fs.existsSync(path.join(dest,'index.html')));
  } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('UI generation refuses output in the repository, including through a symlink',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'pv-mobile-link-'));
  try {fs.symlinkSync(root,path.join(dir,'repo-link'));const result=spawnSync(process.execPath,[script,path.join(dir,'repo-link','unexpected')],{encoding:'utf8'});assert.notEqual(result.status,0);assert.match(result.stderr,/outside the repository/);}
  finally {fs.rmSync(dir,{recursive:true,force:true});}
});
