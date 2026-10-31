'use strict'

// Keep a browser download paused until the user chooses a path. Calling
// showSaveDialog synchronously from `will-download` let Chromium start writing to
// its default location while the native dialog was blocked; this async handoff
// pauses first, sets the chosen path, and leaves resuming to the caller so it can
// attach progress/completion listeners before bytes move again.
function cancelQuietly(item) {
  try { item.cancel() } catch { /* the download may already have finished */ }
}

function createSaveDialogQueue(dialog) {
  let tail = Promise.resolve()

  return function requestDownloadSavePath(item, parent, defaultPath, alreadyPaused = false) {
    if (!alreadyPaused) {
      try {
        item.pause()
      } catch (error) {
        cancelQuietly(item)
        return Promise.reject(error)
      }
    }

    const show = () => dialog.showSaveDialog(parent, {
      title: 'Save file',
      defaultPath,
      buttonLabel: 'Save',
      properties: ['createDirectory', 'showOverwriteConfirmation']
    })

    const operation = tail.then(show, show).then((selection) => {
      if (!selection || selection.canceled || typeof selection.filePath !== 'string' || !selection.filePath) {
        cancelQuietly(item)
        return null
      }

      try {
        item.setSavePath(selection.filePath)
        return selection.filePath
      } catch (error) {
        cancelQuietly(item)
        throw error
      }
    }, (error) => {
      cancelQuietly(item)
      throw error
    })

    // Native dialogs are modal; serialize simultaneous downloads rather than
    // letting several save prompts compete for the same browser window.
    tail = operation.then(() => undefined, () => undefined)
    return operation
  }
}

module.exports = { createSaveDialogQueue }

// Reserve a unique destination before Chromium starts writing. Exclusive creation
// prevents simultaneous downloads from selecting the same name or replacing an
// existing file (including a symlink). A failed transfer may leave an empty file.
function reserveDownloadPath(directory, suggested) {
  const fs = require('node:fs')
  const path = require('node:path')
  let name = String(suggested || '').replace(/[\x00-\x1f<>:"/\\|?*]/g, '_').replace(/[. ]+$/, '').slice(0, 180)
  if (!name || name === '.' || name === '..') name = 'download'
  if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(name)) name = '_' + name
  const ext = path.extname(name), stem = name.slice(0, name.length - ext.length)
  for (let n = 0; n < 10000; n++) {
    const file = path.join(directory, n ? `${stem} (${n})${ext}` : name)
    try {
      const fd = fs.openSync(file, 'wx', 0o600)
      fs.closeSync(fd)
      return file
    } catch (error) { if (error.code !== 'EEXIST') throw error }
  }
  throw new Error('Could not find an unused download filename.')
}
module.exports.reserveDownloadPath = reserveDownloadPath
