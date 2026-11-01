const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
test('preview warns that Proton and extensions do not work', () => {
  assert.match(read('www/index.html'), /Proton VPN and extensions do not work/);
  assert.match(read('www/app.js'), /Traffic is not protected by Proton/);
});
test('website views are separate and do not add a JavaScript bridge', () => {
  const native = read('android/app/src/main/java/com/psychocore/psychovault/experimental/BrowserPlugin.java');
  assert.match(native, /new WebView\(getContext\(\)\)/);
  assert.doesNotMatch(native, /addJavascriptInterface\s*\(/);
  assert.match(native, /setAllowFileAccess\(false\)/);
});
test('Windows entry point keeps notes on failure and success', () => {
  const batch = read('BUILD-ANDROID.bat');
  assert.match(batch, /Release-Notes-Android\.txt/);
  assert.match(batch, /Status: debug APK built/);
  assert.match(batch, /Failed: Android Gradle build failed/);
  assert.equal((batch.match(/\n/g) || []).length, (batch.match(/\r\n/g) || []).length);
});
test('release signing requires a private key and does not fall back to debug signing', () => {
  const gradle = read('android/app/build.gradle');
  assert.match(gradle, /Release signing is not configured/);
  assert.match(gradle, /PSYCHO_ANDROID_KEYSTORE/);
  assert.match(gradle, /signingConfig signingConfigs\.stableRelease/);
  assert.doesNotMatch(gradle, /signingConfig signingConfigs\.debug/);
  assert.match(read('SIGNING.md'), /uninstalling can delete its local app data/);
});
test('signed workflow is manual and requires signing secrets', () => {
  const workflow = fs.readFileSync(path.join(root, '../.github/workflows/android-signed-preview.yml'), 'utf8');
  assert.match(workflow, /workflow_dispatch:/);
  assert.doesNotMatch(workflow, /\n  push:/);
  for (const name of ['PSYCHO_ANDROID_KEYSTORE_BASE64', 'PSYCHO_ANDROID_STORE_PASSWORD', 'PSYCHO_ANDROID_KEY_ALIAS', 'PSYCHO_ANDROID_KEY_PASSWORD']) assert.ok(workflow.includes(name));
  assert.match(workflow, /No debug APK will be released/);
  assert.match(workflow, /trap 'rm -f/);
});
test('native upload and download paths keep their stated safeguards', () => {
  const native = read('android/app/src/main/java/com/psychocore/psychovault/experimental/BrowserPlugin.java');
  assert.match(native, /onShowFileChooser/);
  assert.match(native, /FLAG_GRANT_READ_URI_PERMISSION/);
  assert.match(native, /!"content"\.equalsIgnoreCase\(uri\.getScheme\(\)\)/);
  assert.match(native, /id\.equals\(uploadTab\)\) finishUpload\(null\)/);
  assert.match(native, /setDownloadListener/);
  assert.match(native, /getUserInfo\(\) != null/);
  assert.doesNotMatch(native, /addRequestHeader\s*\(/);
});
test('native blocker packages the desktop domain list and intercepts WebView requests', () => {
  const native = read('android/app/src/main/java/com/psychocore/psychovault/experimental/BrowserPlugin.java');
  assert.match(read('scripts/prepare-web.cjs'), /app', 'blocklist\.json'/);
  assert.match(native, /public\/blocklist\.json/);
  assert.match(native, /shouldInterceptRequest/);
  assert.match(native, /request\.isForMainFrame\(\) \? requestUrl : topUrl\.get\(\)/);
  assert.match(native, /Collections\.unmodifiableSet/);
  assert.match(native, /Could not load the bundled blocker list/);
});
