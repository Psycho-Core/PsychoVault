#!/usr/bin/env node
// Rebrand the already-staged Electron executable for a Windows PsychoVault build.
// Uses pure-JavaScript PE resource editing so this also works on Linux CI runners.
import fs from 'node:fs'
import * as PELibrary from 'pe-library'
import * as ResEdit from 'resedit'

const [exePath, iconPath, version] = process.argv.slice(2)
if (!exePath || !iconPath || !version) {
  console.error('usage: node tools/patch-exe.mjs <electron.exe> <icon.ico> <major.minor.patch>')
  process.exit(2)
}
const versionParts = version.split('.').map(Number)
if (versionParts.length !== 3 || versionParts.some((n) => !Number.isInteger(n) || n < 0 || n > 65535)) {
  throw new Error(`invalid three-part application version: ${version}`)
}

const executable = PELibrary.NtExecutable.from(fs.readFileSync(exePath), { ignoreCert: true })
const resources = PELibrary.NtExecutableResource.from(executable)
const groups = ResEdit.Resource.IconGroupEntry.fromEntries(resources.entries)
const primaryGroup = groups.find((group) => Number(group.id) === 1) || groups[0]
const iconGroupId = primaryGroup ? primaryGroup.id : 1
const iconLanguage = primaryGroup ? Number(primaryGroup.lang) : 1033
const iconFile = ResEdit.Data.IconFile.from(fs.readFileSync(iconPath))
ResEdit.Resource.IconGroupEntry.replaceIconsForResource(
  resources.entries,
  iconGroupId,
  iconLanguage,
  iconFile.icons.map((item) => item.data)
)

const language = 1033 // English (United States)
const versionInfos = ResEdit.Resource.VersionInfo.fromEntries(resources.entries)
const infos = versionInfos.length ? versionInfos : [ResEdit.Resource.VersionInfo.createEmpty()]
for (const info of infos) {
  info.setFileVersion(versionParts[0], versionParts[1], versionParts[2], 0, language)
  info.setProductVersion(versionParts[0], versionParts[1], versionParts[2], 0, language)
  info.setStringValues({ lang: language, codepage: 1200 }, {
    CompanyName: 'Psycho-Core',
    FileDescription: 'PsychoVault Browser',
    FileVersion: `${version}.0`,
    InternalName: 'PsychoVault',
    LegalCopyright: '(c) 2026 Psycho-Core',
    OriginalFilename: 'PsychoVault.exe',
    ProductName: 'PsychoVault Browser',
    ProductVersion: version
  })
  info.outputToResourceEntries(resources.entries)
}
resources.outputResource(executable)
fs.writeFileSync(exePath, Buffer.from(executable.generate()))
console.log(`patched ${exePath}: PsychoVault ${version}, ${iconFile.icons.length} icon sizes`)
