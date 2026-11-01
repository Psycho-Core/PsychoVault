const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
fs.copyFileSync(path.join(root, '..', 'app', 'ui', 'icon.png'), path.join(root, 'www', 'icon.png'));
