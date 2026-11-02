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
  replaceOnce('<button data-cmd="settings">Settings</button>', '<button data-cmd="extensions">Extensions</button>\n    <button data-cmd="settings">Settings</button>');
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
  replaceRenderer("else if (cmd === 'settings') window.pv.tab.new('psychovault://settings')", "else if (cmd === 'settings') window.pv.tab.new('psychovault://settings')\n  else if (cmd === 'extensions') window.pv.tab.new('psychovault://settings#extensions')");
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
  function replaceSettings(before, after) {
    if (settingsHtml.split(before).length !== 2) throw new Error('Desktop settings prose changed: ' + before);
    settingsHtml = settingsHtml.replace(before, after);
  }
  replaceSettings('<button class="mini" id="vpnCheckOpen" type="button">Open VPN checks</button>', '<button class="mini" id="protonVpnOpen" type="button">Open Proton VPN</button>\n      <button class="mini" id="vpnCheckOpen" type="button">Open VPN checks</button>');
  replaceSettings('Use the VPN button on the sidebar to connect or disconnect; right-click it to open Proton.', 'Use the Proton button on the sidebar to open Proton; connect or disconnect inside Proton. The button does not verify protection.');
  replaceSettings("through PsychoVault's own shim", "through Chromium's extension proxy API");
  replaceSettings('Electron can only run script in a page', 'This Android build can only run script in a page');
  replaceSettings('Electron gives no byte-rate hook', 'Android gives no byte-rate hook');
  replaceSettings('Electron builds ship no dictionary and cannot download one, so without this the spellchecker is a flag with nothing behind it. Turn it off only if you have patched dictionaries in yourself.', 'Android uses Chromium spellcheck and HTTPS bdic dictionary mirrors. The built-in word list is a fallback, not the only checker.');
  replaceSettings('A folder or mirror holding <code>&lt;lang&gt;.dic</code> and <code>&lt;lang&gt;.aff</code> Hunspell files. Kept because it is the knob Chromium documents, but an Electron build has no dictionary downloader: nothing is fetched from here, and <code>&lt;profile&gt;/Dictionaries</code> is not read either - all three were tested. The built-in checker above is what makes the feature work.', 'An HTTPS mirror holding <code>&lt;lang&gt;.bdic</code> files. Chromium downloads dictionaries from this mirror; plain HTTP and file URLs are refused.');
  replaceSettings('Rendering engine: Chromium, via Electron <span id="appElectron">45</span>.', 'Rendering engine: Chromium <span id="appElectron">156</span> on Android.');
  replaceSettings('Bookmark, history and settings files live in your user data folder.', 'Bookmarks, history and settings live in Android app storage.');
  replaceSettings('If an update fails while Proton VPN is on, disconnect it and retry. PsychoVault does not bypass the VPN automatically. It removes an updater-downloaded installer only after the new version starts and the file is verified unchanged; older untracked installers are kept.', 'Android checks GitHub Releases and opens the release page. It does not self-install APKs; install updates through Android. If a download fails while Proton VPN is on, disconnect it and retry.');
  fs.writeFileSync(path.join(target,'settings.html'),settingsHtml);
  for (const file of ['settings.js','recovery.html','recovery.js','mobile.css','sidebar.js','mobile-shell.js','desktop-bridge.js','internal-page-bridge.js','navigation-resolver.js','native-transport.js','host-layout.js','chrome-bootstrap.js','page-bootstrap.js']) fs.copyFileSync(path.join(repo,'android-port/ui',file),path.join(target,file));
  for (const file of ['library.js','indexeddb-storage.js','library-transport.js']) fs.copyFileSync(path.join(repo,'android-port/core',file),path.join(target,file));
  console.log('Desktop UI adaptation prepared at ' + target + '. No APK was built.');
} catch (error) {
  fs.rmSync(target, {recursive:true,force:true});
  throw error;
}
