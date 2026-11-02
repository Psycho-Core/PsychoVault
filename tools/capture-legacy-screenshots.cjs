'use strict';
// One-shot capture of the legacy PsychoVault UI before the owned-engine reset.
// Uses a throwaway profile. No user credentials, vault entries or browsing data.
const { app, BrowserWindow, webContents } = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const outDir = path.resolve(__dirname, '../screenshots/legacy');
fs.mkdirSync(outDir, { recursive: true });
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-legacy-capture-'));
app.setPath('userData', profile);
app.commandLine.appendSwitch('disable-gpu');
app.commandLine.appendSwitch('no-sandbox');
fs.writeFileSync(path.join(profile, 'settings.json'), JSON.stringify({
  settingsVersion: 3,
  askSaveLocation: false,
  updateCheckOnStartup: false
}));

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function wait(label, fn, timeout = 30000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    try { const value = await fn(); if (value) return value; } catch {}
    await sleep(150);
  }
  throw new Error(label + ' timed out');
}
function find(page) {
  return webContents.getAllWebContents().find((wc) => !wc.isDestroyed() && wc.getURL().includes(page));
}
async function captureWc(wc, name) {
  await wc.executeJavaScript('document.fonts ? document.fonts.ready : Promise.resolve()');
  await sleep(350);
  const image = await wc.capturePage();
  fs.writeFileSync(path.join(outDir, name + '.png'), image.toPNG());
  console.log('captured', name);
}
async function captureWindow(win, name) {
  await sleep(500);
  try { win.focus(); } catch {}
  // BrowserWindow.capturePage() does not include child WebContentsViews, so it
  // produces a blank content area. Capture the actual X11 window instead.
  execFileSync('import', ['-window', 'root', path.join(outDir, name + '.png')], { stdio: 'ignore' });
  console.log('captured', name);
}

require('../app/main.js');

async function run() {
  const win = await wait('main window', () => BrowserWindow.getAllWindows().find((w) => !w.isDestroyed()));
  win.setBounds({ width: 1440, height: 940 });
  const chrome = await wait('browser chrome', () => find('/ui/index.html'));
  await wait('new tab', () => find('/ui/newtab.html'));
  await sleep(1200);
  await captureWindow(win, '01-browser-new-tab-home');
  await captureWc(chrome, '02-browser-chrome');
  const newtab = find('/ui/newtab.html');
  if (newtab) await captureWc(newtab, '03-new-tab-page');

  await chrome.executeJavaScript("document.getElementById('btn-settings').click()");
  const settings = await wait('settings page', () => find('/ui/settings.html'));
  await sleep(800);
  const categories = await settings.executeJavaScript(`Array.from(document.querySelectorAll('[data-category]')).map(b => ({ id: b.dataset.category, label: b.textContent.trim() }))`);
  let index = 4;
  for (const category of categories) {
    await settings.executeJavaScript(`(() => {
      const button = document.querySelector('[data-category=' + ${JSON.stringify(category.id)} + ']');
      if (button) button.click();
      const panel = document.querySelector('[data-category-panel=' + ${JSON.stringify(category.id)} + ']');
      if (panel) panel.scrollTop = 0;
      window.scrollTo(0, 0);
    })()`);
    await sleep(450);
    const safe = String(index).padStart(2, '0') + '-settings-' + category.id;
    await captureWindow(win, safe + '-window');
    await captureWc(settings, safe);
    index += 1;
  }

  // Return to the new tab and capture the Performance panel if it is present.
  try {
    await chrome.executeJavaScript("window.pv.tab.new('psychovault://newtab')");
    await sleep(600);
  } catch (error) { console.error('new-tab return failed:', error.message); }
  try {
    const perfButton = await chrome.executeJavaScript(`(() => {
      const button = document.getElementById('btn-performance') || document.querySelector('[data-cmd="performance"]');
      if (button) { button.click(); return true; }
      return false;
    })()`);
    if (perfButton) {
      await sleep(600);
      await captureWindow(win, '90-performance-panel');
    }
  } catch (error) { console.error('performance capture failed:', error.message); }
  fs.writeFileSync(path.join(outDir, 'CAPTURE.json'), JSON.stringify({
    capturedAt: new Date().toISOString(),
    sourceCommit: process.env.GITHUB_SHA || 'local',
    profile: 'throwaway',
    categories
  }, null, 2));
  console.log('LEGACY_CAPTURE_OK');
}

run().then(() => app.exit(0)).catch((error) => { console.error(error.stack || error); app.exit(1); });
