'use strict';
// Restrict open/reveal to paths recorded by PsychoVault's download manager.
// Internal pages are trusted UI, but a compromised renderer should not be able
// to ask the shell to open arbitrary local files or executables.
const path = require('path');

function recordedDownloadPath(downloads, requested) {
  if (typeof requested !== 'string' || !requested) return null;
  const wanted = path.resolve(requested);
  for (const entry of Array.isArray(downloads) ? downloads : []) {
    if (!entry || typeof entry.path !== 'string' || !entry.path) continue;
    if (path.resolve(entry.path) === wanted) return entry.path;
  }
  return null;
}

module.exports = { recordedDownloadPath };
