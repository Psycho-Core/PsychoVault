'use strict'
// Redirect walking for the updater, with the HTTP client injected so it can be tested against
// a real local server instead of only reasoned about.
//
// Why this exists as its own module: two releases in a row failed here. v1.0.9 dropped the
// credential inside the 'redirect' event with removeHeader(), which is illegal after the
// request has been ended, so every cross-host redirect was refused. The fix is to re-issue the
// request per hop and compute the header set from scratch - but that fix was, itself, only
// argued for rather than demonstrated. With the client injected, a test can stand up two
// hostnames, force a cross-host redirect, and assert which hop carried the credential.
//
// The client contract is Electron's: a ClientRequest-like object exposing setHeader, end,
// abort, and emitting 'redirect' (with a target URL), 'response' and 'error'.

const MAX_REDIRECTS = 5

function followRedirects(startUrl, options = {}) {
  const {
    request,
    method = 'GET',
    headers = {},
    headerForHop = () => null,
    timeoutMs = 20000,
    maxRedirects = MAX_REDIRECTS
  } = options

  if (typeof request !== 'function') throw new Error('followRedirects needs a request factory')

  return new Promise((resolve, reject) => {
    let settled = false
    let current = null
    let timer = null
    const hops = []

    const finish = (error, result) => {
      if (settled) return
      settled = true
      if (timer) clearTimeout(timer)
      if (error) reject(error)
      else resolve(result)
    }

    const attempt = (targetUrl) => {
      if (settled) return
      if (hops.length > maxRedirects) {
        finish(new Error(`Gave up after ${maxRedirects} redirects.`))
        return
      }

      let req
      try {
        req = request(targetUrl, { method })
      } catch (e) {
        finish(e)
        return
      }
      current = req

      // Recomputed from scratch for every hop. Nothing is ever removed after send, so the
      // removeHeader() trap cannot be re-entered.
      let perHop = null
      try { perHop = headerForHop(targetUrl) } catch { perHop = null }
      for (const [name, value] of Object.entries(headers)) {
        try { req.setHeader(name, value) } catch { /* restricted header */ }
      }
      for (const [name, value] of Object.entries(perHop || {})) {
        try { req.setHeader(name, value) } catch { /* restricted header */ }
      }

      req.on('redirect', (_status, _redirectMethod, redirectUrl) => {
        hops.push({ from: targetUrl, to: redirectUrl, sentToken: !!(perHop && perHop.Authorization) })
        try { req.abort() } catch { /* already closing */ }
        attempt(redirectUrl)
      })
      req.on('response', (res) => finish(null, { res, finalUrl: targetUrl, hops }))
      req.on('error', (e) => { if (!settled) finish(e) })

      try { req.end() } catch (e) { finish(e) }
    }

    timer = setTimeout(() => {
      try { current && current.abort() } catch { /* already closed */ }
      finish(new Error('The request timed out.'))
    }, timeoutMs)

    attempt(startUrl)
  })
}

module.exports = { followRedirects, MAX_REDIRECTS }
