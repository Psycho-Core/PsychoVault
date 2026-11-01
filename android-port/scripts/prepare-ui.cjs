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
  replaceOnce('<script src="renderer.js"></script>', '<script src="sidebar.js"></script>\n<script src="navigation-resolver.js"></script>\n<script src="desktop-bridge.js"></script>\n<script src="native-transport.js"></script>\n<script src="host-layout.js"></script>\n<script src="library.js"></script>\n<script src="indexeddb-storage.js"></script>\n<script src="library-transport.js"></script>\n<script src="chrome-bootstrap.js"></script>');
  fs.writeFileSync(path.join(target,'index.html'), html);
  for (const file of ['style.css','icon.png']) fs.copyFileSync(path.join(ui,file),path.join(target,file));
  let renderer = fs.readFileSync(path.join(ui,'renderer.js'), 'utf8');
  function replaceRenderer(before, after) {
    if (renderer.split(before).length !== 2) throw new Error('Desktop renderer structure changed: ' + before);
    renderer = renderer.replace(before, after);
  }
  replaceRenderer('window.pv.downloads.open(d.path)', 'window.pv.downloads.open(d.id)');
  replaceRenderer("const show = document.createElement('button'); show.className = 'mini'; show.textContent = 'Show in folder'; show.onclick = () => window.pv.downloads.reveal(d.path)", "const show = document.createElement('button'); show.className = 'mini'; show.textContent = 'Show in folder'; show.disabled = true; show.title = 'Folder reveal is unavailable on Android';");
  replaceRenderer('row.title = d.path', 'row.title = d.file || d.name || d.id');
  fs.writeFileSync(path.join(target,'renderer.js'), renderer);
  // WebUI CSP forbids inline scripts. Adapt the copied new tab, not the desktop source.
  let newtab = fs.readFileSync(path.join(ui,'newtab.html'),'utf8');
  const script = newtab.match(/<script>([\s\S]*?)<\/script>/);
  if (!script || newtab.split('<script>').length !== 2) throw new Error('New-tab script structure changed');
  fs.writeFileSync(path.join(target,'newtab.js'),script[1] + '\ndocument.querySelector("form").addEventListener("submit", go);\n');
  newtab = newtab.replace(script[0],'').replace(' onsubmit="go(event)"','').replace('</body>','<script src="navigation-resolver.js"></script>\n<script src="internal-page-bridge.js"></script>\n<script src="native-transport.js"></script>\n<script src="page-bootstrap.js"></script>\n</body>');
  fs.writeFileSync(path.join(target,'newtab.html'),newtab);
  let settingsHtml = fs.readFileSync(path.join(ui,'settings.html'),'utf8');
  const settingsScript = settingsHtml.match(/<script>([\s\S]*?)<\/script>/);
  if (!settingsScript || settingsHtml.split('<script>').length !== 2) throw new Error('Settings script structure changed');
  settingsHtml = settingsHtml.replace(settingsScript[0],'').replace('<meta charset="utf-8">','<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">').replace('</body>','<script src="navigation-resolver.js"></script>\n<script src="internal-page-bridge.js"></script>\n<script src="native-transport.js"></script>\n<script src="settings.js"></script>\n</body>');
  fs.writeFileSync(path.join(target,'settings.html'),settingsHtml);
  for (const file of ['settings.js','recovery.html','recovery.js','mobile.css','sidebar.js','mobile-shell.js','desktop-bridge.js','internal-page-bridge.js','navigation-resolver.js','native-transport.js','host-layout.js','chrome-bootstrap.js','page-bootstrap.js']) fs.copyFileSync(path.join(repo,'android-port/ui',file),path.join(target,file));
  for (const file of ['library.js','indexeddb-storage.js','library-transport.js']) fs.copyFileSync(path.join(repo,'android-port/core',file),path.join(target,file));
  console.log('Desktop UI adaptation prepared at ' + target + '. No APK was built.');
} catch (error) {
  fs.rmSync(target, {recursive:true,force:true});
  throw error;
}
