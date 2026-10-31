'use strict'

const fs = require('node:fs')
const path = require('node:path')

// The name comes from a verified manifest, but keep the destination constrained even if a
// caller makes a mistake. Never replace an existing download, including a symlink.
async function saveVerifiedInstaller(temporary, downloads, assetName) {
  if (!/^PsychoVault-Setup-\d+\.\d+\.\d+\.exe$/.test(assetName)) {
    throw new Error('The update installer name is invalid.')
  }
  const stem = assetName.slice(0, -4)
  for (let n = 0; n < 1000; n++) {
    const destination = path.join(downloads, n ? `${stem} (${n}).exe` : assetName)
    try {
      await fs.promises.copyFile(temporary, destination, fs.constants.COPYFILE_EXCL)
      await fs.promises.unlink(temporary).catch(() => {})
      return destination
    } catch (error) {
      if (error.code !== 'EEXIST') throw error
    }
  }
  throw new Error('Could not find an unused installer name in Downloads.')
}

module.exports = { saveVerifiedInstaller }
