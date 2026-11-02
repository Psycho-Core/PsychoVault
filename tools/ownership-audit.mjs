#!/usr/bin/env node
// PsychoVault ownership/provenance audit. This checks repository evidence, not
// legal title. MIT-licensed code remains non-exclusive, and third-party
// components keep their own licences. Run: node tools/ownership-audit.mjs
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';

const ROOT = path.resolve(import.meta.dirname, '..');
const read = (rel) => fs.readFileSync(path.join(ROOT, rel), 'utf8');
const sha256 = (rel) => crypto.createHash('sha256').update(fs.readFileSync(path.join(ROOT, rel))).digest('hex');
const defects = [];
const facts = [];
const check = (ok, fact, defect) => { if (ok) facts.push(fact); else defects.push(defect || fact); };

const licence = read('LICENSE');
check(/MIT License/.test(licence) && /Copyright \(c\) 2026 Psycho-Core/.test(licence), 'LICENSE is MIT and attributes copyright to Psycho-Core.', 'LICENSE must be MIT and attribute Psycho-Core.');
const pkg = JSON.parse(read('app/package.json'));
check(pkg.author === 'Psycho-Core' && pkg.license === 'MIT', `app/package.json author=${pkg.author}, license=${pkg.license}.`, 'app/package.json must attribute Psycho-Core and MIT.');

const engineFiles = ['manifest.js', 'permissions.js', 'dispatcher.js', 'index.js'];
for (const file of engineFiles) {
  const source = read(`extension-engine/${file}`);
  check(/MIT/.test(source) && /PsychoVault/.test(source), `extension-engine/${file} carries the PsychoVault MIT header.`, `extension-engine/${file} is missing the PsychoVault MIT header.`);
}
check(!/require\(['"]electron['"]\)/.test(engineFiles.map(f => read(`extension-engine/${f}`)).join('\n')), 'extension-engine core does not require Electron.', 'extension-engine core must not depend on Electron.');

const protonLicence = sha256('app/builtin/proton-vpn/LICENSE');
check(protonLicence === '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986', 'Bundled Proton GPL-3.0 licence matches the pinned upstream licence hash.', 'Bundled Proton licence hash changed; review GPL obligations.');
const darkLicence = sha256('app/builtin/dark-reader/LICENSE');
check(darkLicence === 'f0a5f835174494f8981b2cbb1a34054d4f887a5c865318650d6a17afe1c7850e', 'Bundled Dark Reader MIT licence matches the upstream licence hash.', 'Bundled Dark Reader licence hash changed; review notices.');
check(/GPL-3\.0/.test(read('THIRD-PARTY-NOTICES.md')) && /Proton AG/.test(read('THIRD-PARTY-NOTICES.md')), 'Root notices identify Proton as GPL-3.0, not PsychoVault MIT code.', 'Root notices must identify Proton GPL-3.0.');
check(/Titanium/.test(read('THIRD-PARTY-NOTICES.md')) && /Vanadium/.test(read('THIRD-PARTY-NOTICES.md')), 'Root notices identify the Android Titanium/Vanadium foundation as third-party.', 'Root notices must identify Titanium/Vanadium.');

const tracked = execFileSync('git', ['ls-files'], { cwd: ROOT, encoding: 'utf8' }).split('\n').filter(Boolean);
const privateKeys = tracked.filter(f => /(^|\/)(id_rsa|id_ed25519|.*private.*\.pem|.*private.*\.key)$/.test(f));
check(privateKeys.length === 1 && privateKeys[0] === 'tools/fixtures/proxy-test-private.pem', 'The only tracked private key is the public HTTPS-proxy test fixture.', `Unexpected tracked private key material: ${privateKeys.join(', ')}`);
check(tracked.includes('app/update-public-key.pem') && !tracked.includes('app/update-private-key.pem'), 'Updater ships a public verification key only; no private update key is tracked.', 'Updater key material must be public-only in the repository.');

const installer = read('tools/build-installer.sh');
const easy = read('PsychoVault_easy_build.bat');
check(!/wireproxy\.exe/.test(installer) && /not packaged|never starts/.test(easy), 'Legacy wireproxy.exe is excluded from the installer and easy build.', 'Legacy wireproxy.exe must not be packaged or run.');
check(!/runs it as a child process/.test(read('docs/THIRD-PARTY-NOTICES.md')), 'docs/THIRD-PARTY-NOTICES.md no longer claims wireproxy is bundled/run.', 'docs/THIRD-PARTY-NOTICES.md still claims wireproxy is bundled/run.');

console.log('PsychoVault ownership/provenance audit');
for (const fact of facts) console.log('PASS', fact);
for (const defect of defects) console.log('FAIL', defect);
console.log('Limit: this is repository evidence, not a legal opinion. MIT is non-exclusive; Electron/Chromium/Node, Proton, Dark Reader and Titanium/Vanadium remain third-party.');
process.exit(defects.length ? 1 : 0);
