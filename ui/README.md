# ui/ - my interface as web pages

Nothing here yet. This directory is created by P3 of `docs/Psycho_MASTER-PLAN.md` and holds the browser's own interface, written as plain HTML/CSS/JS and rendered by my own engine - the same technique my old Electron app used, without Electron.

Planned content:

- `chrome.html` - rail, tab strip, omnibox, status bar, update banner, panel container.
- `newtab.html` - logo, search, speed dial, service rail.
- `settings.html` - the ten legacy categories and all 125 controls.
- `panel-*.html` - downloads, extensions, performance, vault, history, bookmarks, workspaces.
- `dialog-*.html` - prompts, confirmations, permission requests, errors.
- `tokens.css` - colours, spacing, radii and type scale transcribed from `docs/reference/legacy-ui/`.
- `components.css`, `app.js` - shared controls and the UI's JS, talking to the runtime through `window.psycho`.

Geometry and visual rules come from `docs/Psycho_REFERENCE.md`. Nothing may be drawn by the native shell: if it is part of the interface, it is a page.
