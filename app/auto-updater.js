'use strict'

const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')
const { spawn } = require('node:child_process')
const { app, BrowserWindow, dialog, net, safeStorage } = require('electron')
const { shouldKeepAuthorization, authHeader, tokenSource, apiAssetUrl } = require('./update-auth')
const {
  REPOSITORY,
  PLATFORM,
  compareVersions,
  verifyUpdateManifest,
  installerDownloadUrl,
  classifyCheckFailure
} = require('./update-manifest')

const API_URL = `https://api.github.com/repos/${REPOSITORY}/releases/latest`
const USER_AGENT = 'PsychoVault-Updater/1.0'
const PUBLIC_KEY_PATH = path.join(__dirname, 'update-public-key.pem')
const MAX_API_BYTES = 4 * 1024 * 1024
const MAX_MANIFEST_BYTES = 64 * 1024
const REQUEST_TIMEOUT_MS = 20000
const DOWNLOAD_TIMEOUT_MS = 20 * 60 * 1000
// Bundled at build time from the UPDATE_READ_TOKEN Actions secret; the file is gitignored and
// only ever exists inside a packaged build. A user can override it from Settings, and the
// override is encrypted with the OS keychain where one exists.
const BUNDLED_TOKEN_PATH = path.join(__dirname, 'update-token.json')
const OVERRIDE_TOKEN_FILE = 'update-token.bin'
let cachedToken

function readBundledToken() {
  try {
    const parsed = JSON.parse(fs.readFileSync(BUNDLED_TOKEN_PATH, 'utf8'))
    return typeof parsed.token === 'string' ? parsed.token.trim() : ''
  } catch { return '' }
}

function readOverrideToken() {
  try {
    const file = path.join(app.getPath('userData'), OVERRIDE_TOKEN_FILE)
    const raw = fs.readFileSync(file)
    if (safeStorage.isEncryptionAvailable()) return safeStorage.decryptString(raw).trim()
    // No OS keychain: the file is ours, mode 0600, and the user was told in Settings.
    return raw.toString('utf8').trim()
  } catch { return '' }
}

function updateToken() {
  if (cachedToken !== undefined) return cachedToken
  cachedToken = readOverrideToken() || readBundledToken()
  return cachedToken
}

function forgetToken() { cachedToken = undefined }

function tokenState() {
  const bundled = readBundledToken()
  const override = readOverrideToken()
  return {
    source: tokenSource(bundled, !!override),
    hasToken: !!(override || bundled),
    bundled: !!bundled,
    override: !!override,
    encryption: safeStorage.isEncryptionAvailable() ? 'os-keychain' : 'plain'
  }
}

function setOverrideToken(token) {
  const value = String(token || '').trim()
  const file = path.join(app.getPath('userData'), OVERRIDE_TOKEN_FILE)
  if (!value) {
    try { fs.rmSync(file, { force: true }) } catch { /* already gone */ }
    forgetToken()
    return { ok: true, removed: true }
  }
  if (!authHeader(value)) return { ok: false, error: 'That does not look like a valid token (it must be printable with no spaces).' }
  try {
    const payload = safeStorage.isEncryptionAvailable() ? safeStorage.encryptString(value) : Buffer.from(value, 'utf8')
    fs.writeFileSync(file, payload, { mode: 0o600 })
    forgetToken()
    return { ok: true, encryption: safeStorage.isEncryptionAvailable() ? 'os-keychain' : 'plain' }
  } catch (e) {
    return { ok: false, error: String((e && e.message) || e) }
  }
}

function safeProgress(callback, payload) {
  try { callback(payload) } catch { /* update progress must never break the browser */ }
}

// An update check that cannot reach GitHub and one that GitHub refuses are different
// problems, and the user can only act on the second. Carry the status out of here instead
// of flattening every failure into one silent 'unavailable'.
class UpdateCheckError extends Error {
  constructor(message, status) {
    super(message)
    this.name = 'UpdateCheckError'
    this.status = Number.isFinite(status) ? status : 0
    this.isPrivateRepository = this.status === 404
    this.offline = !this.status
  }
}

function requestBuffer(url, { maxBytes = MAX_API_BYTES, timeoutMs = REQUEST_TIMEOUT_MS, headers = {}, allowRedirectHeaderDrop = true } = {}) {
  return new Promise((resolve, reject) => {
    let request
    let response
    let settled = false
    let size = 0
    const chunks = []
    const finish = (error, result) => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      if (error) reject(error)
      else resolve(result)
    }
    const timer = setTimeout(() => {
      try { request && request.abort() } catch { /* request may already be closed */ }
      finish(new Error('GitHub request timed out.'))
    }, timeoutMs)

    try {
      request = net.request({ url, method: 'GET', redirects: 'manual', useSessionCookies: false })
    } catch (error) {
      finish(error)
      return
    }
    // Redirects are followed by hand so the credential can be dropped the moment the target
    // host changes. net.request's 'follow' would carry Authorization to the CDN host.
    // Fail closed: if the credential cannot be dropped before the target host changes, the
    // redirect is abandoned rather than followed with the token still attached.
    const dropCredential = () => {
      try {
        request.removeHeader('Authorization')
        return !request.getHeader('Authorization')
      } catch { return false }
    }
    if (allowRedirectHeaderDrop) {
      request.on('redirect', (_status, _method, redirectUrl) => {
        if (!shouldKeepAuthorization(url, redirectUrl) && !dropCredential()) {
          finish(new Error('Refused a redirect that would have carried the release token to another host.'))
          try { request.abort() } catch { /* already closed */ }
          return
        }
        request.followRedirect()
      })
    }
    request.on('response', (res) => {
      response = res
      if (!res.statusCode || res.statusCode < 200 || res.statusCode >= 300) {
        res.resume()
        finish(new UpdateCheckError(`GitHub returned HTTP ${res.statusCode || 'unknown'}.`, res.statusCode))
        return
      }
      res.on('data', (chunk) => {
        if (settled) return
        size += chunk.length
        if (size > maxBytes) {
          try { res.destroy() } catch { /* response may already be closed */ }
          finish(new Error('GitHub response exceeded the allowed size.'))
          return
        }
        chunks.push(chunk)
      })
      res.on('end', () => finish(null, Buffer.concat(chunks)))
      res.on('error', finish)
    })
    request.on('error', finish)
    request.setHeader('User-Agent', USER_AGENT)
    const auth = authHeader(updateToken())
    if (auth) request.setHeader('Authorization', auth.Authorization)
    for (const [name, value] of Object.entries(headers)) request.setHeader(name, value)
    request.end()
  })
}

function releaseAssetUrl(asset, tag, name) {
  if (!asset || asset.name !== name || typeof asset.browser_download_url !== 'string') return null
  const expected = `https://github.com/${REPOSITORY}/releases/download/${encodeURIComponent(tag)}/${encodeURIComponent(name)}`
  return asset.browser_download_url === expected ? expected : null
}

function getParentWindow() {
  return BrowserWindow.getFocusedWindow() || BrowserWindow.getAllWindows()[0] || null
}

function showMessage(options) {
  const parent = getParentWindow()
  return parent ? dialog.showMessageBox(parent, options) : dialog.showMessageBox(options)
}

function showInstallerSaveDialog(manifest) {
  const parent = getParentWindow()
  const options = {
    title: `Save PsychoVault ${manifest.version} installer`,
    defaultPath: path.join(app.getPath('downloads'), manifest.assetName),
    buttonLabel: 'Save installer',
    filters: [{ name: 'Windows installer', extensions: ['exe'] }],
    properties: ['createDirectory', 'showOverwriteConfirmation']
  }
  return parent ? dialog.showSaveDialog(parent, options) : dialog.showSaveDialog(options)
}

function setTaskbarProgress(value) {
  for (const win of BrowserWindow.getAllWindows()) {
    try { win.setProgressBar(value) } catch { /* window closed during update */ }
  }
}

function downloadInstaller(url, destination, manifest, onProgress) {
  return new Promise((resolve, reject) => {
    let request
    let response
    let output
    let settled = false
    let finishedWriting = false
    let received = 0
    let lastReported = -10
    const hash = crypto.createHash('sha256')
    const timer = setTimeout(() => fail(new Error('The update download timed out.')), DOWNLOAD_TIMEOUT_MS)

    function fail(error) {
      if (settled) return
      settled = true
      clearTimeout(timer)
      try { request && request.abort() } catch { /* request may already be closed */ }
      try { response && response.destroy() } catch { /* response may already be closed */ }
      try { output && output.destroy() } catch { /* output may already be closed */ }
      fs.rm(destination, { force: true }, () => {})
      reject(error)
    }

    function finishDownload() {
      if (settled) return
      if (!finishedWriting) return fail(new Error('The update installer was not written completely.'))
      if (received !== manifest.size) return fail(new Error('The downloaded installer size does not match its signed manifest.'))
      const actualHash = hash.digest('hex')
      if (actualHash !== manifest.sha256) return fail(new Error('The downloaded installer failed its SHA-256 check.'))
      settled = true
      clearTimeout(timer)
      safeProgress(onProgress, { phase: 'verified', percent: 100, message: 'Update downloaded and verified.' })
      resolve({ bytes: received, sha256: actualHash })
    }

    try {
      request = net.request({ url, method: 'GET', redirects: 'manual', useSessionCookies: false })
    } catch (error) {
      fail(error)
      return
    }
    request.on('redirect', (_status, _method, redirectUrl) => {
      if (!shouldKeepAuthorization(url, redirectUrl)) {
        let dropped = false
        try {
          request.removeHeader('Authorization')
          dropped = !request.getHeader('Authorization')
        } catch { dropped = false }
        if (!dropped) {
          fail(new Error('Refused a redirect that would have carried the release token to another host.'))
          try { request.abort() } catch { /* already closed */ }
          return
        }
      }
      request.followRedirect()
    })
    request.on('response', (res) => {
      response = res
      if (!res.statusCode || res.statusCode < 200 || res.statusCode >= 300) {
        res.resume()
        fail(new Error(`GitHub returned HTTP ${res.statusCode || 'unknown'} for the installer.`))
        return
      }
      const contentLength = Number(res.headers['content-length'])
      if (Number.isFinite(contentLength) && contentLength > 0 && contentLength !== manifest.size) {
        res.resume()
        fail(new Error('The installer response size does not match its signed manifest.'))
        return
      }
      try { output = fs.createWriteStream(destination, { flags: 'wx', mode: 0o700 }) }
      catch (error) { fail(error); return }
      output.on('error', fail)
      output.on('finish', () => { finishedWriting = true })
      output.on('close', finishDownload)
      res.on('data', (chunk) => {
        if (settled) return
        received += chunk.length
        if (received > manifest.size) {
          fail(new Error('The installer response is larger than its signed manifest.'))
          return
        }
        hash.update(chunk)
        const percent = Math.min(100, Math.floor((received / manifest.size) * 100))
        if (percent >= lastReported + 10) {
          lastReported = percent
          safeProgress(onProgress, {
            phase: 'download',
            percent,
            message: percent >= 100 ? 'Finishing update download…' : `Downloading signed update… ${percent}%`
          })
        }
      })
      res.on('error', fail)
      res.pipe(output)
    })
    request.on('error', fail)
    request.setHeader('User-Agent', USER_AGENT)
    request.setHeader('Accept', 'application/octet-stream')
    const auth = authHeader(updateToken())
    if (auth) request.setHeader('Authorization', auth.Authorization)
    request.end()
  })
}

function launchInstaller(installerPath, onProgress, onError) {
  let child
  try {
    child = spawn(installerPath, [], { detached: true, stdio: 'ignore', windowsHide: false })
  } catch (error) {
    onError(error)
    return false
  }
  child.once('error', (error) => {
    onError(error)
  })
  child.once('spawn', () => {
    child.unref()
    safeProgress(onProgress, { phase: 'install', percent: 100, message: 'Launching the verified installer…' })
    app.quit()
  })
  return true
}

async function checkForUpdates({ onProgress = () => {}, onError = () => {} } = {}) {
  if (!app.isPackaged || process.platform !== 'win32' || process.arch !== 'x64') {
    return { skipped: true, reason: 'unsupported-runtime' }
  }

  let release
  try {
    const response = await requestBuffer(API_URL, {
      maxBytes: MAX_API_BYTES,
      headers: { Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' }
    })
    release = JSON.parse(response.toString('utf8'))
  } catch (e) {
    // Offline startup, rate limits, and temporary GitHub failures should not interrupt browsing -
    // but each one now has a name, and a 404 is called out for what it is. An unauthenticated
    // release lookup against a private repository always answers 404, which no retry can fix.
    const status = e && e.status ? e.status : 0
    const classified = classifyCheckFailure(status, e && e.message)
    if (classified.reason === 'repository-private') {
      const state = tokenState()
      const error = new Error(
        `The update check was refused with HTTP 404 for ${REPOSITORY}. GitHub answers that to a ` +
        'request it cannot authorise, so this repository is private and the check arrived without ' +
        'a usable token. ' +
        (state.hasToken
          ? `A token is configured (from ${state.source}) but GitHub still refused it - it may have expired or been revoked. Replace it in Settings - About.`
          : 'Add a read-only token in Settings - About, or ship one in the build with the UPDATE_READ_TOKEN secret. ') +
        'Nothing was downloaded and nothing was changed.'
      )
      try { onError(error) } catch { /* diagnostics are best effort */ }
      return { skipped: true, reason: 'repository-private', status }
    }
    if (classified.reason === 'offline') {
      try { onError(new Error(`The update check could not reach GitHub: ${(e && e.message) || e}`)) } catch { /* best effort */ }
      return { skipped: true, reason: 'offline', status }
    }
    return { skipped: true, reason: 'release-check-unavailable', status }
  }
  if (!release || release.draft || release.prerelease || typeof release.tag_name !== 'string' || !Array.isArray(release.assets)) {
    return { skipped: true, reason: 'no-stable-release' }
  }
  const currentVersion = app.getVersion()
  const comparison = compareVersions(release.tag_name, currentVersion)
  if (comparison === null || comparison <= 0) return { skipped: true, reason: 'already-current' }

  const manifestAsset = release.assets.find((asset) => asset && asset.name === 'update-manifest.json')
  const signatureAsset = release.assets.find((asset) => asset && asset.name === 'update-manifest.sig')
  const manifestUrl = releaseAssetUrl(manifestAsset, release.tag_name, 'update-manifest.json')
  const signatureUrl = releaseAssetUrl(signatureAsset, release.tag_name, 'update-manifest.sig')
  if (!manifestUrl || !signatureUrl) {
    const error = new Error(`Release ${release.tag_name} has no valid signed update manifest; the update was blocked.`)
    try { onError(error) } catch { /* diagnostics are best effort */ }
    return { skipped: true, reason: 'missing-signature' }
  }

  let manifestBytes
  let signatureBytes
  try {
    ;[manifestBytes, signatureBytes] = await Promise.all([
      requestBuffer(manifestUrl, { maxBytes: MAX_MANIFEST_BYTES, headers: { Accept: 'application/octet-stream' } }),
      requestBuffer(signatureUrl, { maxBytes: 1024, headers: { Accept: 'application/octet-stream' } })
    ])
  } catch {
    return { skipped: true, reason: 'manifest-download-failed' }
  }

  let publicKeyPem
  try { publicKeyPem = fs.readFileSync(PUBLIC_KEY_PATH, 'utf8') }
  catch (error) {
    try { onError(error) } catch { /* diagnostics are best effort */ }
    return { skipped: true, reason: 'public-key-missing' }
  }
  const verification = verifyUpdateManifest(manifestBytes, signatureBytes.toString('utf8'), publicKeyPem, {
    currentVersion,
    tag: release.tag_name,
    platform: PLATFORM
  })
  if (!verification.ok) {
    const error = new Error(`Release ${release.tag_name} failed update signature/metadata verification (${verification.reason}); no installer was launched.`)
    try { onError(error) } catch { /* diagnostics are best effort */ }
    return { skipped: true, reason: verification.reason }
  }

  const manifest = verification.manifest
  const body = String(release.body || '').replace(/\s+/g, ' ').trim().slice(0, 1400)
  let answer
  try {
    answer = await showMessage({
      type: 'info',
      title: `PsychoVault ${manifest.version} is available`,
      message: 'A signed update is ready to install.',
      detail: `Current version: ${currentVersion}\nNew version: ${manifest.version}\n\n${body || 'See the GitHub release history for details.'}\n\nThe installer will be downloaded and checked against an embedded Ed25519 verification key before it is opened.`,
      buttons: ['Later', 'Download and install'],
      defaultId: 0,
      cancelId: 0,
      noLink: true
    })
  } catch {
    return { skipped: true, reason: 'update-prompt-failed' }
  }
  if (!answer || answer.response !== 1) return { skipped: true, reason: 'user-deferred' }

  const url = installerDownloadUrl(manifest)
  if (!url) {
    const error = new Error('The signed update has an invalid installer URL; no installer was launched.')
    try { onError(error) } catch { /* diagnostics are best effort */ }
    return { skipped: true, reason: 'installer-url' }
  }

  let destination
  let temporaryDestination
  try {
    const selection = await showInstallerSaveDialog(manifest)
    if (!selection || selection.canceled || typeof selection.filePath !== 'string' || !selection.filePath) {
      return { skipped: true, reason: 'save-as-cancelled' }
    }
    destination = path.resolve(selection.filePath)
    if (!path.extname(destination)) destination += '.exe'
    if (path.extname(destination).toLowerCase() !== '.exe') throw new Error('Choose a destination ending in .exe for the Windows installer.')
    temporaryDestination = path.join(
      path.dirname(destination),
      `.${path.basename(destination)}.${crypto.randomBytes(8).toString('hex')}.download`
    )

    safeProgress(onProgress, { phase: 'download', percent: 0, message: 'Downloading signed update… 0%' })
    setTaskbarProgress(0)
    await downloadInstaller(url, temporaryDestination, manifest, onProgress)
    await fs.promises.rename(temporaryDestination, destination)
    temporaryDestination = null
    setTaskbarProgress(-1)
    safeProgress(onProgress, { phase: 'verified', percent: 100, message: 'Verified installer saved. Launching update…' })
    if (!launchInstaller(destination, onProgress, (error) => {
      try { dialog.showErrorBox('PsychoVault update could not start', `The signed installer is saved at:\n${destination}\n\nOpen it manually to install the update.\n\n${error.message}`) }
      catch { /* the main window may already be closing */ }
      try { onError(error) } catch { /* diagnostics are best effort */ }
    })) {
      return { skipped: true, reason: 'installer-launch-failed' }
    }
    return { ok: true, version: manifest.version, installerPath: destination }
  } catch (error) {
    setTaskbarProgress(-1)
    if (temporaryDestination) fs.rm(temporaryDestination, { force: true }, () => {})
    try {
      dialog.showErrorBox(
        'PsychoVault update was not installed',
        `The update download or its integrity check failed. Your current version was left unchanged.\n\n${error.message}`
      )
    } catch { /* the main window may already be closing */ }
    try { onError(error) } catch { /* diagnostics are best effort */ }
    return { ok: false, error: error.message }
  }
}

function checkOnStartup(options = {}) {
  if (!app.isPackaged || process.platform !== 'win32' || process.arch !== 'x64') return null
  const delay = Number.isFinite(options.delayMs) ? Math.max(0, options.delayMs) : 8000
  const timer = setTimeout(() => { checkForUpdates(options).catch((error) => {
    try { options.onError && options.onError(error) } catch { /* diagnostics are best effort */ }
  }) }, delay)
  if (timer.unref) timer.unref()
  return timer
}

module.exports = { checkForUpdates, checkOnStartup, tokenState, setOverrideToken, apiAssetUrl }
