#!/usr/bin/env node
// Inventory desktop defaults. An adapter name is not an Android implementation.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const main = fs.readFileSync(path.resolve(__dirname,'../../app/main.js'),'utf8');
const begin = main.indexOf('const DEFAULT_SETTINGS = {');
const end = main.indexOf('\nlet settings =',begin);
if (begin < 0 || end <= begin) throw new Error('Desktop defaults could not be located.');
const sandbox = {};
vm.runInNewContext(main.slice(begin,end) + '\nthis.defaults = DEFAULT_SETTINGS;',sandbox,{timeout:1000});
const implemented = new Set(['search','customSearchEngines','homepage','remember','theme','accent','speedDial','hideSidebar','highContrast','reduceMotion','showTabCloseButtons']);
const inventory = Object.keys(sandbox.defaults).map(key => ({
  key,
  status: key === 'settingsVersion' ? 'desktop-internal-migration-marker'
    : implemented.has(key) ? 'android-source-implemented-not-device-verified'
    : key === 'extensions' ? 'android-extension-manager-source-not-device-verified'
    : 'psychovault-android-host-not-implemented'
}));
if (require.main === module) console.log(JSON.stringify(inventory,null,2));
module.exports = {inventory};
