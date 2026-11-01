#!/usr/bin/env node
// Produces an Android-adapted copy of the real desktop UI without altering it.
const fs = require('node:fs');
const path = require('node:path');
const repo = path.resolve(__dirname, '../..');
const dest = process.argv[2];
if (!dest || !path.isAbsolute(dest)) throw new Error('Provide an absolute path for a new UI output folder.');
const target = path.join(fs.realpathSync(path.dirname(path.resolve(dest))), path.basename(path.resolve(dest)));
if (target === repo || target.startsWith(repo + path.sep)) throw new Error('UI output must be outside the repository.');
fs.mkdirSync(target, {recursive:false});
try {
  const ui = path.join(repo, 'app/ui');
  let html = fs.readFileSync(path.join(ui, 'index.html'), 'utf8');
  function replaceOnce(before, after) {
    if (html.split(before).length !== 2) throw new Error('Desktop UI structure changed: ' + before);
    html = html.replace(before, after);
  }
  replaceOnce('<meta charset="utf-8">', '<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">');
  replaceOnce('<link rel="stylesheet" href="style.css">', '<link rel="stylesheet" href="style.css">\n<link rel="stylesheet" href="mobile.css">');
  replaceOnce('<div id="logo"></div>', '<button id="mobile-rail-toggle" type="button" aria-label="Show sidebar" aria-controls="rail" aria-expanded="false">☰</button>\n      <div id="logo"></div>');
  replaceOnce('<script src="renderer.js"></script>', '<script src="sidebar.js"></script>\n<script src="mobile-shell.js"></script>');
  fs.writeFileSync(path.join(target,'index.html'), html);
  for (const file of ['style.css','renderer.js','icon.png','newtab.html','settings.html','recovery.html']) fs.copyFileSync(path.join(ui,file),path.join(target,file));
  for (const file of ['mobile.css','sidebar.js','mobile-shell.js','desktop-bridge.js','internal-page-bridge.js']) fs.copyFileSync(path.join(repo,'android-port/ui',file),path.join(target,file));
  for (const file of ['library.js','indexeddb-storage.js','library-transport.js']) fs.copyFileSync(path.join(repo,'android-port/core',file),path.join(target,file));
  console.log('Desktop UI adaptation prepared at ' + target + '. No Chromium bridge or APK was built.');
} catch (error) {
  fs.rmSync(target, {recursive:true,force:true});
  throw error;
}
