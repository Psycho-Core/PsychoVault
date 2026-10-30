'use strict'
// A real redirect walk, against a real server, with two different hostnames.
//
// This is the test that was missing when the updater failed twice: the previous tests checked
// which header *should* be sent, but nothing ever performed a redirect. Here a local server is
// reached as 127.0.0.1 and redirects to the same server addressed as localhost - a genuinely
// different host, which is the condition that decides whether the credential travels.
const test = require('node:test')
const assert = require('node:assert')
const http = require('node:http')
const { followRedirects, MAX_REDIRECTS } = require('../app/update-http.js')
const { authorizationForHop } = require('../app/update-auth.js')

// Electron-shaped ClientRequest over node:http, so the module under test sees the same
// interface it sees in the app: setHeader before send, 'redirect' for 3xx, 'response' else.
function nodeClient(seen) {
  return (url, { method = 'GET' } = {}) => {
    const listeners = { redirect: [], response: [], error: [] }
    const headers = {}
    const record = { url: String(url), headers }
    seen.push(record)
    let req = null

    const api = {
      setHeader(name, value) { headers[name] = value },
      on(event, fn) { (listeners[event] || (listeners[event] = [])).push(fn); return api },
      abort() { try { req && req.destroy() } catch { /* closed */ } },
      end() {
        req = http.request(String(url), { method }, (res) => {
          if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
            for (const fn of listeners.redirect) fn(res.statusCode, method, res.headers.location)
            res.resume()
            return
          }
          for (const fn of listeners.response) fn(res)
        })
        req.on('error', (e) => { for (const fn of listeners.error) fn(e) })
        for (const [k, v] of Object.entries(headers)) req.setHeader(k, v)
        req.end()
        return api
      }
    }
    return api
  }
}

function readBody(res) {
  return new Promise((resolve, reject) => {
    const chunks = []
    res.on('data', (c) => chunks.push(c))
    res.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    res.on('error', reject)
  })
}

async function withServer(handler, run) {
  const server = http.createServer(handler)
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
  const { port } = server.address()
  try { return await run(port) } finally { await new Promise((r) => server.close(r)) }
}

test('a cross-host redirect is followed and the credential is dropped on the second hop', async () => {
  const seen = []
  await withServer((req, res) => {
    if (req.url === '/manifest.json') {
      res.writeHead(302, { location: `http://localhost:${req.socket.localPort}/asset` })
      res.end()
      return
    }
    res.writeHead(200, { 'content-type': 'application/json' })
    res.end(JSON.stringify({ ok: true }))
  }, async (port) => {
    const start = `http://127.0.0.1:${port}/manifest.json`
    const { res, hops, finalUrl } = await followRedirects(start, {
      request: nodeClient(seen),
      headerForHop: (hop) => authorizationForHop(start, hop, 'secret-token'),
      timeoutMs: 8000
    })

    const body = await readBody(res)
    assert.strictEqual(res.statusCode, 200)
    assert.strictEqual(body, JSON.stringify({ ok: true }))

    assert.strictEqual(hops.length, 1, 'exactly one redirect should have been taken')
    assert.strictEqual(hops[0].sentToken, true, 'the first hop is the origin, so it carries the token')
    assert.match(finalUrl, /localhost/)

    assert.strictEqual(seen.length, 2, 'two requests: the original and the redirect target')
    assert.strictEqual(seen[0].headers.Authorization, 'Bearer secret-token')
    assert.strictEqual(seen[1].headers.Authorization, undefined, 'the credential must not follow the redirect')
  })
})

test('a same-host redirect keeps the credential', async () => {
  const seen = []
  await withServer((req, res) => {
    if (req.url === '/a') {
      res.writeHead(302, { location: `http://127.0.0.1:${req.socket.localPort}/b` })
      res.end()
      return
    }
    res.writeHead(200)
    res.end('done')
  }, async (port) => {
    const start = `http://127.0.0.1:${port}/a`
    const { res, hops } = await followRedirects(start, {
      request: nodeClient(seen),
      headerForHop: (hop) => authorizationForHop(start, hop, 'secret-token'),
      timeoutMs: 8000
    })
    await readBody(res)
    assert.strictEqual(hops.length, 1)
    assert.strictEqual(seen[1].headers.Authorization, 'Bearer secret-token')
  })
})

test('a redirect loop is abandoned rather than followed forever', async () => {
  await withServer((req, res) => {
    res.writeHead(302, { location: `http://127.0.0.1:${req.socket.localPort}/loop` })
    res.end()
  }, async (port) => {
    await assert.rejects(
      followRedirects(`http://127.0.0.1:${port}/loop`, {
        request: nodeClient([]),
        headerForHop: () => null,
        timeoutMs: 8000
      }),
      new RegExp(`after ${MAX_REDIRECTS} redirects`)
    )
  })
})

test('a connection error is reported, not swallowed', async () => {
  await assert.rejects(
    followRedirects('http://127.0.0.1:1/nope', {
      request: nodeClient([]),
      headerForHop: () => null,
      timeoutMs: 5000
    }),
    /ECONNREFUSED|connect|socket/i
  )
})

test('a missing request factory is a clear error, not a hang', () => {
  assert.throws(() => followRedirects('http://x', {}), /needs a request factory/)
})
