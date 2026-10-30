'use strict'
// Guards on .github/workflows/build-release.yml.
//
// The build is the only way a release is published, and the schedule is the
// only thing that will retry a build without a human. A silent edit that moves
// the cron away from the minute reset, or drops the timezone, or lets a monthly
// run republish an already-released version, would not fail anything - it would
// just quietly stop working. So it is asserted here instead.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const WORKFLOW = path.join(__dirname, '..', '.github', 'workflows', 'build-release.yml')
const text = fs.readFileSync(WORKFLOW, 'utf8')
const lines = text.split('\n')

function stepBlock(name) {
  const start = lines.findIndex((line) => line.trim() === `- name: ${name}`)
  assert.ok(start !== -1, `no step called "${name}"`)
  let end = start + 1
  while (end < lines.length && !/^\s{6}- name: /.test(lines[end]) && !/^\s{0,4}\w/.test(lines[end])) {
    end += 1
  }
  return lines.slice(start, end).join('\n')
}

test('the build still has a monthly schedule, in Adelaide time', () => {
  assert.match(text, /^\s{2}schedule:/m, 'no schedule trigger')
  const cronIndex = lines.findIndex((line) => /^\s{4}- cron: '0 11 1 \* \*'$/.test(line))
  assert.notStrictEqual(cronIndex, -1, 'the monthly 11:00 cron is missing or changed')
  // the timezone must be a sibling of that cron entry, on the next line
  assert.match(
    lines[cronIndex + 1] || '',
    /^\s{6}timezone: 'Australia\/Adelaide'$/,
    'cron is not pinned to Australia/Adelaide, so it would drift by an hour twice a year'
  )
})

test('the schedule fires after the monthly minute reset, not before', () => {
  // Reset is 00:00 UTC on the 1st = 10:30 in Adelaide during daylight saving.
  // Anything at or before 10:30 local would land on the exhausted quota.
  // cron is minute hour day-of-month: the hour is the second field
  const localHour = Number(/cron: '\d+ (\d+) 1 \* \*'/.exec(text)[1])
  assert.ok(localHour >= 11, `the schedule fires at ${localHour}:xx local, which is before the reset`)
})

test('the workflow can still be started by hand', () => {
  assert.match(text, /^\s{2}workflow_dispatch:$/m)
})

test('a version that is already released is never republished', () => {
  const check = stepBlock('Check whether this version is already released')
  assert.match(check, /id: released/)
  assert.match(check, /gh release view "v\$version"/)

  for (const name of [
    'Sign update manifest',
    'Write checksum',
    'Update the branch checksum',
    'Publish the signed release'
  ]) {
    const block = stepBlock(name)
    assert.match(
      block,
      /if:.*steps\.released\.outputs\.exists == 'false'/,
      `${name} is not gated on the release not existing`
    )
  }
})

test('the release-exists check runs before anything it gates', () => {
  const nameIndex = (name) => lines.findIndex((line) => line.trim() === `- name: ${name}`)
  const check = nameIndex('Check whether this version is already released')
  const version = nameIndex('Read release version')
  assert.ok(check > version, 'the release check must come after the version is read')
  for (const name of ['Sign update manifest', 'Publish the signed release']) {
    assert.ok(nameIndex(name) > check, `${name} runs before the release check`)
  }
})

test('scheduled and push builds cannot publish at the same time', () => {
  assert.match(text, /^concurrency:$/m)
  assert.match(text, /group: build-release-\$\{\{ github\.ref \}\}/)
  assert.match(text, /cancel-in-progress: false/)
})
