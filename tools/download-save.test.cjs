'use strict'

const test = require('node:test')
const assert = require('node:assert/strict')
const { createSaveDialogQueue } = require('../app/download-save.js')

function makeItem(log, name) {
  return {
    pause() { log.push(`${name}:pause`) },
    setSavePath(filePath) { log.push(`${name}:path:${filePath}`) },
    cancel() { log.push(`${name}:cancel`) },
    resume() { log.push(`${name}:resume`) }
  }
}

test('pauses before showing Save As and sets the selected path only after the answer', async () => {
  const log = []
  let resolvePicker
  const parent = { id: 'download-owner' }
  const request = createSaveDialogQueue({
    showSaveDialog(actualParent, options) {
      log.push('dialog')
      assert.equal(actualParent, parent)
      assert.equal(options.title, 'Save file')
      assert.equal(options.defaultPath, '/Downloads/suggested.zip')
      assert.equal(options.buttonLabel, 'Save')
      return new Promise((resolve) => { resolvePicker = resolve })
    }
  })
  const pending = request(makeItem(log, 'one'), parent, '/Downloads/suggested.zip')

  assert.deepEqual(log, ['one:pause'])
  await Promise.resolve()
  assert.deepEqual(log, ['one:pause', 'dialog'])

  resolvePicker({ canceled: false, filePath: '/chosen/custom-name.zip' })
  assert.equal(await pending, '/chosen/custom-name.zip')
  assert.deepEqual(log, ['one:pause', 'dialog', 'one:path:/chosen/custom-name.zip'])
})

test('cancelling the picker cancels the paused transfer without setting a path', async () => {
  const log = []
  const request = createSaveDialogQueue({
    showSaveDialog() {
      log.push('dialog')
      return Promise.resolve({ canceled: true })
    }
  })

  assert.equal(await request(makeItem(log, 'cancelled'), null, '/Downloads/file.bin'), null)
  assert.deepEqual(log, ['cancelled:pause', 'dialog', 'cancelled:cancel'])
})

test('a picker error cancels the transfer and rejects to the caller', async () => {
  const log = []
  const request = createSaveDialogQueue({
    showSaveDialog() {
      log.push('dialog')
      return Promise.reject(new Error('native dialog failed'))
    }
  })

  await assert.rejects(request(makeItem(log, 'failed'), null, '/Downloads/file.bin'), /native dialog failed/)
  assert.deepEqual(log, ['failed:pause', 'dialog', 'failed:cancel'])
})

test('simultaneous downloads queue their dialogs while both transfers stay paused', async () => {
  const log = []
  const answers = []
  const request = createSaveDialogQueue({
    showSaveDialog(parent) {
      log.push(`dialog:${parent}`)
      return new Promise((resolve) => answers.push(resolve))
    }
  })

  const first = request(makeItem(log, 'one'), 'window-one', '/Downloads/one.zip')
  const second = request(makeItem(log, 'two'), 'window-two', '/Downloads/two.zip')
  assert.deepEqual(log, ['one:pause', 'two:pause'])

  await Promise.resolve()
  assert.deepEqual(log, ['one:pause', 'two:pause', 'dialog:window-one'])
  assert.equal(answers.length, 1)

  answers[0]({ canceled: false, filePath: '/chosen/one.zip' })
  assert.equal(await first, '/chosen/one.zip')
  await new Promise((resolve) => setImmediate(resolve))
  assert.deepEqual(log, [
    'one:pause', 'two:pause', 'dialog:window-one', 'one:path:/chosen/one.zip', 'dialog:window-two'
  ])
  assert.equal(answers.length, 2)

  answers[1]({ canceled: false, filePath: '/chosen/two.zip' })
  assert.equal(await second, '/chosen/two.zip')
})
