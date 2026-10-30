'use strict'
// Credentials for reading releases from a private repository, and the rule for carrying
// them across redirects. Kept free of Electron and the filesystem so the security-relevant
// decision here - when an Authorization header may follow a redirect - can be tested.
//
// Why a token is needed at all: GitHub answers 404 to an unauthenticated request for a
// private repository's releases. That is a permissions fact, not a bug in the updater. A
// read-only token (classic `repo`, or fine-grained Contents:Read on one repository) changes
// the answer to 200 without making the update any less trustworthy - the Ed25519 signature
// on update-manifest.json still decides whether the download is accepted. Authentication
// gates access; the signature gates trust. They are deliberately separate.

// A redirect may keep the credential only when it stays on the same host. GitHub redirects
// release downloads to a signed URL on a different host (release-assets.githubusercontent.com);
// forwarding the token there would hand the secret to the CDN and, on some setups, break the
// download when the header is stripped. Drop it and let the signed URL do the work.
function shouldKeepAuthorization(fromUrl, toUrl) {
  try {
    const from = new URL(String(fromUrl))
    const to = new URL(String(toUrl))
    return from.host === to.host && from.protocol === to.protocol
  } catch {
    return false
  }
}

function authHeader(token) {
  const value = String(token || '').trim()
  if (!value) return null
  // A token with whitespace or a newline in it would produce an invalid header, and a header
  // injection through a pasted value is not worth discovering later.
  if (!/^[\x21-\x7e]+$/.test(value)) return null
  return { Authorization: 'Bearer ' + value }
}

// Where the credential in use came from, so Settings can say it without ever showing the token.
function tokenSource(bundledToken, overridePresent) {
  if (overridePresent) return 'settings'
  if (String(bundledToken || '').trim()) return 'bundled'
  return 'none'
}

// GitHub's REST URL for an asset's bytes. Used to build a download that can be authenticated;
// the public browser_download_url is kept for the URL-shape checks the manifest relies on.
function apiAssetUrl(assetId) {
  const id = Number(assetId)
  if (!Number.isSafeInteger(id) || id <= 0) return null
  return `https://api.github.com/repos/${'Psycho-Core/PsychoVault'}/releases/assets/${id}`
}

// The header set for one hop of a redirect chain, decided from the ORIGINAL url rather than
// from the previous hop: a chain that leaves github.com and comes back must not carry the
// credential back in on the strength of the hop before it.
function authorizationForHop(startUrl, hopUrl, token) {
  if (!shouldKeepAuthorization(startUrl, hopUrl)) return null
  return authHeader(token)
}

module.exports = { shouldKeepAuthorization, authorizationForHop, authHeader, tokenSource, apiAssetUrl }
