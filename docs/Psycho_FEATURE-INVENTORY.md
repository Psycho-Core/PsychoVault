# My legacy feature and settings inventory

I extracted this from my old application at commit `503a596bd89b5aaac979d4f6f86f8465e24c192d`. I am using it as a read-only contract for the new browser. I am not restoring the old implementation.

## Counts

| Item | Count |
| --- | ---: |
| Settings categories | 10 |
| Interactive settings controls | 125 |
| Controls with IDs | 109 |
| Category navigation buttons | 10 |
| Data-clear buttons | 6 |
| Default settings keys | 59 |
| Main-process IPC handlers | 105 |
| Preload namespaces | 21 |
| Saved screenshots | 23 |

## Settings categories and controls

### General

Search and startup behavior.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `search` | select | - | Search engine |
| `customSearchEngines` | textarea | - | Search engine |
| `customSearchSave` | button | - | Save shortcuts |
| `restore` | input | checkbox | Restore tabs on launch |
| `workspacesEnabled` | input | checkbox | Workspaces |
| `homepage` | input | text | Home page |

### Privacy & security

Protection, site data, browser hardening, and cleanup.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `blockAds` | input | checkbox | Block ads and trackers |
| `remember` | input | checkbox | Remember history |
| `clear` | button | - | Clear history |
| `blockUseBundled` | input | checkbox | Use bundled domain list |
| `blockAllowSites` | textarea | - | Use bundled domain list |
| `blockCustomDomains` | textarea | - | Use bundled domain list |
| `blockSaveLists` | button | - | Save blocker lists |
| `permOrigin` | input | text | Site origin |
| `permLoad` | button | - | Load site |
| `permCamera` | select | - | Camera |
| `permMicrophone` | select | - | Microphone |
| `permLocation` | select | - | Location |
| `permNotifications` | select | - | Notifications |
| `permSave` | button | - | Save site rules |
| `permRemove` | button | - | Remove site rules |
| `blockThirdPartyCookies` | input | checkbox | Block third-party cookies |
| `sendDNT` | input | checkbox | Send Do Not Track and GPC |
| `blockAutoplay` | input | checkbox | Block video autoplay |
| `relaunch` | button | - | Restart PsychoVault |
| `webRtc` | select | - | WebRTC IP handling |
| `doh` | select | - | DNS over HTTPS |
| `dohTemplate` | input | text | DoH template |
| `stripReferrer` | input | checkbox | Drop cross-site Referer |
| `denyPerms` | input | text | Permissions denied to every site |
| `contAdd` | button | - | Add container |
| `contSave` | button | - | Save containers |
| `clearOnExit` | select | - | Delete browsing data on close |
| `dataRefresh` | button | - | Refresh |

### Downloads

Choose a destination for every file before it downloads.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `downloadDir` | input | text | Download folder |
| `downloadDirPick` | button | - | Browse |
| `askSaveLocation` | input | checkbox | Ask where to save each download |
| `downloadNotify` | input | checkbox | Notify when a download finishes |

### Extensions

Install and manage compatible browser extensions.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `vpnCheckOpen` | button | button | Open VPN checks |
| `protonVpnRestore` | button | button | Restore / reinstall Proton VPN |
| `protonVpnStatus` | button | button | Check install |
| `proxySelfTest` | button | button | Test proxy support |
| `proxyStatus` | button | button | Check what proxy is in use |
| `extAddFolder` | button | - | Load unpacked... |
| `extAddCrx` | button | - | Install .crx... |
| `extReload` | button | - | Reload list |

### Performance

Resource limits, sleeping tabs, and battery saving.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `cpuLimit` | select | - | CPU limit |
| `batterySaver` | input | checkbox | Battery saver |
| `sleepTabsMin` | input | number | Sleep tabs after |
| `sleepExclude` | textarea | - | Never sleep these sites |
| `netLimit` | select | - | Bandwidth cap |
| `batteryAuto` | input | checkbox | Battery saver on unplug |

### Appearance

Personalize the browser and manage sidebar services and speed dial.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `theme` | select | - | Browser theme |
| `accent` | select | - | Accent colour |
| `hideSidebar` | input | checkbox | Hide the sidebar |
| `modCss` | textarea | - | Hide the sidebar |
| `modSave` | button | - | Apply mod |
| `modClear` | button | - | Clear |
| `enhanceVideo` | input | checkbox | Enhance videos |
| `enhanceImages` | input | checkbox | Enhance images |
| `uiSounds` | input | checkbox | Keyboard sounds |
| `soundVol` | input | text | Sound volume |
| `mediaKeys` | input | checkbox | Media keys drive the sidebar app |
| `mediaAutoPause` | input | checkbox | Pause music when other media starts |
| `panicEnabled` | input | checkbox | Panic button |
| `panicUrl` | input | text | Escape page |
| `svcAdd` | button | - | Add service |
| `svcSave` | button | - | Save services |
| `railIcons` | input | checkbox | Favicons in the rail |
| `tileAdd` | button | - | Add tile |
| `tileSave` | button | - | Save tiles |

### Spelling

Configure spell checking and your personal dictionary.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `spellCheck` | input | checkbox | Spell-check text you type in pages |
| `spellBuiltin` | input | checkbox | PsychoVault's own checker |
| `spellLangs` | input | text | Languages |
| `spellDictUrl` | input | text | Dictionary source |
| `spellUserWords` | textarea | - | Dictionary source |
| `spellWordsSave` | button | - | Save words |
| `spellWordsRefresh` | button | - | Reload status |

### Passwords

Manage your local encrypted password vault.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `vaultEnabled` | input | checkbox | Vault enabled |
| `vaultLockMin` | select | - | Auto-lock after |
| `vpw` | input | password | Auto-lock after |
| `vgo` | button | - | Unlock vault |
| `vforget` | button | - | Stop using a master password |
| `vnew` | input | password | Auto-lock after |
| `vset` | button | - | Set master password |
| `vbackupPassword` | input | password | Auto-lock after |
| `vbackupConfirm` | input | password | Auto-lock after |
| `vBackup` | button | - | Save encrypted backup |
| `vRestore` | button | - | Restore encrypted backup |
| `vImport` | button | - | Import CSV export |
| `eSite` | input | text | Auto-lock after |
| `eUser` | input | text | Auto-lock after |
| `ePass` | input | password | Auto-lock after |
| `eGen` | button | - | Generate |
| `eSave` | button | - | Save |
| `vLock` | button | - | Lock now |
| `vRefresh` | button | - | Refresh |

### Accessibility & language

Translate pages and adjust the browser interface for comfort.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `translateTarget` | select | - | Translate pages to |
| `highContrast` | input | checkbox | High-contrast interface |
| `reduceMotion` | input | checkbox | Reduce motion |
| `showTabCloseButtons` | input | checkbox | Always show tab close buttons |

### About

Version and project information.

| ID | Kind | Type | Reference label |
| --- | --- | --- | --- |
| `openErrorLog` | button | - | Open error log |
| `diagnosticsRefresh` | button | - | Refresh report |
| `diagnosticsCopy` | button | - | Copy report |
| `diagnosticsReport` | textarea | - | Troubleshooting |
| `updateCheckNow` | button | button | Check now |
| `updateToken` | input | text | Release access token |
| `updateTokenSave` | button | button | Save token |
| `updateTokenForget` | button | button | Forget token |

### Data-clear buttons

| Action |
| --- |
| `cache` |
| `cookies` |
| `storage` |
| `history` |
| `downloads` |
| `session` |

## Dynamic settings editors

- site permission editor
- container list
- data usage table
- extension list
- proxy switch list
- sidebar service catalogue and list
- speed dial tiles
- vault entry list

## Default settings keys

| Key | Old default source |
| --- | --- |
| `search` | `'duckduckgo'` |
| `customSearchEngines` | `[]` |
| `sitePermissions` | `{}` |
| `blockUseBundled` | `true` |
| `blockCustomDomains` | `''` |
| `blockAllowSites` | `''` |
| `blockAds` | `true` |
| `homepage` | `'psychovault:` |
| `workspacesEnabled` | `true` |
| `restore` | `true` |
| `remember` | `true` |
| `theme` | `'midnight'` |
| `accent` | `'cyan'` |
| `hideSidebar` | `false` |
| `highContrast` | `false` |
| `reduceMotion` | `false` |
| `showTabCloseButtons` | `false` |
| `translateTarget` | `'en'` |
| `blockAutoplay` | `false` |
| `blockThirdPartyCookies` | `false` |
| `sendDNT` | `true` |
| `cpuLimit` | `'off'` |
| `sleepExclude` | `''` |
| `sleepTabsMin` | `0` |
| `clearOnExit` | `'nothing'` |
| `batterySaver` | `false` |
| `vaultEnabled` | `true` |
| `vaultLockMin` | `5` |
| `enhanceVideo` | `false` |
| `enhanceImages` | `false` |
| `netLimit` | `0` |
| `settingsVersion` | `3` |
| `askSaveLocation` | `true` |
| `downloadDir` | `''` |
| `downloadNotify` | `true` |
| `panicEnabled` | `true` |
| `panicUrl` | `'https:` |
| `services` | `[` |
| `railIcons` | `true` |
| `spellCheck` | `true` |
| `spellBuiltin` | `true` |
| `spellLangs` | `''` |
| `spellDictUrl` | `''` |
| `spellUserWords` | `[]` |
| `dismissedUpdate` | `''` |
| `extensions` | `[]` |
| `mediaKeys` | `true` |
| `mediaAutoPause` | `true` |
| `uiSounds` | `false` |
| `soundVol` | `0.4` |
| `webRtc` | `'publiconly'` |
| `doh` | `'off'` |
| `dohTemplate` | `''` |
| `stripReferrer` | `true` |
| `denyPerms` | `'geolocation, media, clipboard-read, midi, sync-xhr, wake-lock, notifications'` |
| `containers` | `[` |
| `modCss` | `''` |
| `batteryAuto` | `false` |
| `speedDial` | `[` |

## Chrome features

- tabs: new/close/activate/reopen/pin/mute/move/drag
- workspaces: list/switch/new/close/rename/menu
- private windows
- split view
- command palette
- omnibox suggestions and custom search shortcuts
- bookmarks
- history
- downloads: list/control/clear/open/reveal/dir/pickdir
- find in page
- zoom in/out/reset
- page translation
- sidebar services and favicons
- Performance panel: hot tabs/media/panic/services/switches/snapshot PNG/PDF
- ad blocker toggle and per-site shield
- Proton VPN toggle
- spelling status/add/remove
- vault status/unlock/lock
- update banner/check/install/later/dismiss/token state
- error log and diagnostics
- session recovery
- keyboard shortcuts and context menus

## Legacy data stores

- `settings.json`
- `bookmarks.json`
- `history.json`
- `downloads.json`
- `session.json`
- `workspaces.json`
- `vault.bin`
- `vault.key.json`
- `vault-migration.json`
- `crash-recovery.json`
- `errors.log`
- `ext-shim.log`
- `extensions/`
- `icons/`
- `running.marker`

## Preload namespaces

`tab`, `nav`, `translate`, `win`, `bookmarks`, `history`, `settings`, `find`, `zoom`, `view`, `downloads`, `ui`, `ws`, `clip`, `rail`, `spell`, `updates`, `proton`, `extensions`, `perf`, `vault`

## Main-process IPC handlers

| Handler |
| --- |
| `app:diagnostics` |
| `app:diagnosticsCopy` |
| `app:info` |
| `app:relaunch` |
| `bookmarks:add` |
| `bookmarks:isBookmarked` |
| `bookmarks:list` |
| `bookmarks:remove` |
| `catalogue:list` |
| `catalogue:toggle` |
| `clipboard:write` |
| `data:clear` |
| `data:openErrorLog` |
| `data:report` |
| `downloads:clear` |
| `downloads:control` |
| `downloads:dir` |
| `downloads:list` |
| `downloads:open` |
| `downloads:pickdir` |
| `downloads:reveal` |
| `ext:addcrx` |
| `ext:addfolder` |
| `ext:list` |
| `ext:openpage` |
| `ext:remove` |
| `ext:restorebuiltin` |
| `ext:status` |
| `ext:toggle` |
| `find:start` |
| `find:stop` |
| `history:clear` |
| `history:list` |
| `history:search` |
| `nav:back` |
| `nav:forward` |
| `nav:go` |
| `nav:home` |
| `nav:reload` |
| `nav:stop` |
| `open:external` |
| `perf:hot` |
| `perf:media` |
| `perf:panic` |
| `perf:services` |
| `perf:snapshot` |
| `perf:switches` |
| `proton:toggle` |
| `proxy:selftest` |
| `proxy:status` |
| `rail:icons` |
| `recovery:list` |
| `recovery:restore` |
| `settings:get` |
| `settings:set` |
| `spell:add` |
| `spell:remove` |
| `spell:status` |
| `tab:activate` |
| `tab:close` |
| `tab:menu` |
| `tab:move` |
| `tab:mute` |
| `tab:new` |
| `tab:open` |
| `tab:pin` |
| `tab:reopen` |
| `translate:page` |
| `ui:layout` |
| `ui:menu` |
| `update:banners` |
| `update:check` |
| `update:dismiss` |
| `update:install` |
| `update:later` |
| `update:settoken` |
| `update:tokenstate` |
| `vault:add` |
| `vault:copy` |
| `vault:delete` |
| `vault:forgetMaster` |
| `vault:generate` |
| `vault:list` |
| `vault:lock` |
| `vault:reveal` |
| `vault:setMaster` |
| `vault:status` |
| `vault:transfer` |
| `vault:unlock` |
| `view:split` |
| `vpncheck:ip` |
| `vpncheck:open` |
| `win:close` |
| `win:maximize` |
| `win:minimize` |
| `win:private` |
| `ws:close` |
| `ws:list` |
| `ws:menu` |
| `ws:new` |
| `ws:rename` |
| `ws:switch` |
| `zoom:in` |
| `zoom:out` |
| `zoom:reset` |

## Screenshots

| File | Size | Dimensions | SHA-256 |
| --- | ---: | --- | --- |
| `01-browser-new-tab-home.png` | 151450 | 1600x1000 | `baf6a27d8f447be6fecb86f8ec744382a3c35fecf87d93cb62ac64bd11cbe64d` |
| `02-browser-chrome.png` | 24474 | 1440x940 | `9f602d9f627cdfc909976b6d4ae9700b7fc37c03a9bfebd32a20a27e38728a17` |
| `03-new-tab-page.png` | 141860 | 1396x834 | `1c33826d9a9d8d804cfb93b3b5ea1bc883675312f05d50b575b4de9272425037` |
| `04-settings-general-window.png` | 115053 | 1600x1000 | `1ce663b5d79374eb80d52f65a403ef4d8c6cbb72b62c4d09ee0c15031144a0e0` |
| `04-settings-general.png` | 92159 | 1396x834 | `73c2072d82506114d7f72e4b1b10e3c29067243567eac5098f2d43855eb075c6` |
| `05-settings-privacy-window.png` | 121734 | 1600x1000 | `27128224b363e7a6f4536d7972d266b5439b1b6a1f864913c0d2a358c55ae527` |
| `05-settings-privacy.png` | 99222 | 1396x834 | `03d81a3f36e95e9cdff5ce28523c8d8412f4de0bfc875e5a80e27685f7a9d633` |
| `06-settings-downloads-window.png` | 113868 | 1600x1000 | `f6adb2e464daf69851059d2be2b5f3de6854ff898d5337875bc2e00bd7a320ba` |
| `06-settings-downloads.png` | 92402 | 1396x834 | `380ab88609dc9a998da747d1403af1da910ac5d4e97d5017f33ac25227ed7570` |
| `07-settings-extensions-window.png` | 247984 | 1600x1000 | `756fa341bd7bd93813d284d0797716671c32cd1d2941032f3ec5bb8b7775aa81` |
| `07-settings-extensions.png` | 235208 | 1396x834 | `b512b0866fa484cde708eb18f6277ba31336167d8b1ef6ee0bf65ab44543ca24` |
| `08-settings-performance-window.png` | 121575 | 1600x1000 | `3a181cf996e534176f9373af7b0c4935aa4a529c416fc1d446f626b1d2289c26` |
| `08-settings-performance.png` | 99095 | 1396x834 | `6164fe45c243f9d70d19d530d2358dc4d2065ef404133ba2832b4d767a925426` |
| `09-settings-appearance-window.png` | 117903 | 1600x1000 | `443d73b27cdc3b6cbad78d2ce7ebbfd979592734aa70b5bff6ce7812c595b387` |
| `09-settings-appearance.png` | 95261 | 1396x834 | `da62935fde7cfff60b6e188a0f831628af39fd63733524a410404b2b88764b94` |
| `10-settings-spelling-window.png` | 170190 | 1600x1000 | `1b927a3cca4b7e072e6655604a8956a2be2372f17f807ccd330617b174e354f6` |
| `10-settings-spelling.png` | 147288 | 1396x834 | `ad46cfe9be0d53cc9627670cf4b8a20d947bc3315d38b9207ba92337a7c6dbf8` |
| `11-settings-passwords-window.png` | 152513 | 1600x1000 | `241cdb784c61914ae9b7f4dbc907e17d9e327f0d21bd6aac8974645abe5bf322` |
| `11-settings-passwords.png` | 129977 | 1396x834 | `db2b69f84831fbcab4ec62ecb82b44ed13dfeba95fce85773b43bff92914e989` |
| `12-settings-accessibility-window.png` | 133277 | 1600x1000 | `7762427fc0e703b93a27299e9fc710104236084e5f65f7417d13f44c29b40731` |
| `12-settings-accessibility.png` | 110735 | 1396x834 | `efa9e0d67e8da082d290594f360795e4249f843e8e6e8e6a6005ff39dc217580` |
| `13-settings-about-window.png` | 133661 | 1600x1000 | `e8af2286465e8d6b841e0bc55e9637d1c50efc465e84e30e67fc1ccb6288eb61` |
| `13-settings-about.png` | 111008 | 1396x834 | `f7ce4cd6a7f8ccf90a082f5f47e92435c55755c1af4d36b44f8c27d4e73d6ad1` |

## Legacy source hashes

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `app/ui/index.html` | 7335 | `c5b5f94eace94bdf3624bd7b4dc147314d24c1cdab6b3f1dcbef6ee7da316042` |
| `app/ui/newtab.html` | 6339 | `a00122da665b2c180cd48e332b53d9c2fd1d2fe8c12ca0692725b64d8fc71c96` |
| `app/ui/settings.html` | 91601 | `ac21380075d11305845245f906540932b5ddac9722620d9625d7f83dc7c09d4a` |
| `app/ui/style.css` | 23554 | `8a672feca2e47400bef3c9bc61fefbaaac5d2d2f07aa750b8947d4a67b7a37ac` |
| `app/ui/renderer.js` | 54136 | `4a49590b845f450706e506a8bf27389bf456defa1a09243f999181c3f2cc458f` |
| `app/main.js` | 192210 | `cb7932267fd7991ca4480d7f974dd557bc1477a1a0eb86637630a901226cfd6c` |
| `app/preload.js` | 4457 | `c6dd61d5195cd35c513d599c99c382dbb6c375100830a4e400858b596becb1d4` |
