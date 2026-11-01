const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
fs.copyFileSync(path.join(root, '..', 'app', 'ui', 'icon.png'), path.join(root, 'www', 'icon.png'));

// Use the same bundled domain list as the desktop blocker.
fs.copyFileSync(path.join(root, '..', 'app', 'blocklist.json'), path.join(root, 'www', 'blocklist.json'));

fs.copyFileSync(path.join(root, 'node_modules', 'scrypt-js', 'scrypt.js'), path.join(root, 'www', 'scrypt.js'));
fs.copyFileSync(path.join(root, 'node_modules', 'scrypt-js', 'LICENSE.txt'), path.join(root, 'www', 'scrypt-license.txt'));
