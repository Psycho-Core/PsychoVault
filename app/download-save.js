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
