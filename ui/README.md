# `ui/` — browser-owned pages

`index.html`, `app.js` and `fixtures/local-page.html` are the first S045 vertical
slice. The app page is intentionally small: it provides address entry and a new
-tab action through only the existing curated `psycho.tab.new` bridge method.
The shell's S045 app-page slice paints the engine page across the OS client
area instead of drawing its legacy GDI browser controls. The page is
intentionally small, and this is not a visual-parity claim.

The owned engine smoke proof is:

```sh
python3 tools/Psycho_s045_ui_smoke.py --outdir /tmp/PsychoVault-S045
```

It renders both the app page (`psychovault://newtab`) and a local web fixture
(`https://local.test/`) at 1396×834 and writes two PNGs plus a JSON report in the
selected output directory. The generated screenshots are evidence artefacts,
not checked-in files.

**Current limitation:** the Windows owned-engine workflow now builds the shell,
passes `pv-tests`, and captures the app page and a fetched local page. Those are
separate smoke launches; they do not prove that address entry or the bridge
switches from the app page to the local fixture in one running executable. That
same-app S045 flow remains unverified, and these results are not a visual-parity
claim.

The later UI remains planned: full chrome, new tab, settings, panels, dialogs,
shared tokens/components, and the complete `window.psycho` contract. Nothing in
this slice claims M2 parity.
