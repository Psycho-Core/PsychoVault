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
