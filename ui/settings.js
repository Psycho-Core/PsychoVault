(function () {
  // Category navigation, then the typed settings bridge: values are read with psycho.settings.list()
  // and each change is sent with psycho.settings.set({key, value}) where value is JSON text. A control
  // the broker has no profile field for is marked "unwired" and says which step will connect it.
  var buttons = document.querySelectorAll(".cat");
  var panels = document.querySelectorAll(".panel");
  function show(id) {
    for (var i = 0; i < panels.length; i++) {
      panels[i].style.display = panels[i].getAttribute("data-panel") === id ? "block" : "none";
    }
    for (var j = 0; j < buttons.length; j++) {
      var on = buttons[j].getAttribute("data-cat") === id;
      buttons[j].className = on ? "cat active" : "cat";
    }
  }
  for (var k = 0; k < buttons.length; k++) {
    (function (button) {
      button.addEventListener("click", function () { show(button.getAttribute("data-cat")); });
    }(buttons[k]));
  }

  // psychovault://settings/#privacy opens that category (also lets a capture or a link name one).
  var wanted = (typeof location !== "undefined" && location.hash ? String(location.hash) : "").replace("#", "");
  for (var w = 0; wanted && w < buttons.length; w++) {
    if (buttons[w].getAttribute("data-cat") === wanted) show(wanted);
  }


  // Appearance: the saved theme, accent and contrast become attributes on <html>; tokens.css does the rest.
  var THEMES = { midnight: 1, graphite: 1, forest: 1, plum: 1, ocean: 1 };
  var ACCENTS = { cyan: 1, red: 1, purple: 1, green: 1, orange: 1, pink: 1, gold: 1, teal: 1, indigo: 1, rose: 1 };
  function applyLook(look) {
    var root = document.documentElement;
    if (!look || !root) return;
    if (look.theme && THEMES[look.theme]) root.setAttribute("data-theme", look.theme);
    if (look.accent && ACCENTS[look.accent]) root.setAttribute("data-accent", look.accent);
    root.className = look.highContrast ? "hc" : "";
  }

  var bridge = typeof psycho !== "undefined" && psycho.settings ? psycho.settings : null;
  var statusEl = document.getElementById("save-status");
  function say(text) { if (statusEl) statusEl.textContent = text; }
  function controlFor(key) { return document.getElementById(key); }
  function readValue(control) {
    if (control.type === "checkbox") return !!control.checked;
    if (control.type === "number") return Number(control.value);
    return control.value;
  }
  function writeValue(control, value) {
    if (control.type === "checkbox") control.checked = value === true;
    else control.value = String(value);
  }
  function send(key) {
    var control = controlFor(key);
    if (!control || !bridge) return;
    try {
      bridge.set({ key: key, value: JSON.stringify(readValue(control)) });
      say("Saved");
    } catch (error) {
      say("Not saved: " + key + " (" + String(error && error.message ? error.message : error) + ")");
    }
  }
  if (!bridge) return;

  function fill() {
    var listing = null;
    try { listing = bridge.list(); } catch (error) { say("Settings are unavailable"); return false; }
    var values = (listing && listing.values) || {};
    for (var key in values) {
      var control = controlFor(key);
      if (control) writeValue(control, values[key]);
    }
    return true;
  }
  if (!fill()) return;
  var look = function () {
    return { theme: controlFor("theme") && controlFor("theme").value, accent: controlFor("accent") && controlFor("accent").value,
             highContrast: controlFor("highContrast") && controlFor("highContrast").checked };
  };
  applyLook(look());
  ["theme", "accent", "highContrast"].forEach(function (id) {
    var node = controlFor(id);
    if (node) node.addEventListener("change", function () { applyLook(look()); });
  });

  // Every control that holds a value saves when it changes; the textareas also have their own save
  // buttons, which send what is in the box now.
  var listing = bridge.list();
  var values = (listing && listing.values) || {};
  for (var name in values) {
    (function (key) {
      var control = controlFor(key);
      if (!control) return;
      control.addEventListener("change", function () { send(key); });
      control.setAttribute("data-wired", "true");
    }(name));
  }
  function onPress(buttonId, handler) {
    var button = controlFor(buttonId);
    if (button) button.addEventListener("click", handler);
  }
  onPress("spellWordsSave", function () { send("spellUserWords"); });
  onPress("spellWordsRefresh", function () { fill(); say("Reloaded"); });
  onPress("blockSaveLists", function () { send("blockAllowSites"); send("blockCustomDomains"); });
  onPress("customSearchSave", function () { send("customSearchEngines"); });
  onPress("modSave", function () { send("modCss"); });
  onPress("modClear", function () { var box = controlFor("modCss"); if (box) { box.value = ""; send("modCss"); } });

  // Clear browsing history asks twice: the first press arms the button, the second does it.
  var clearButton = controlFor("clear");
  var clearArmed = false;
  if (clearButton && psycho.history && psycho.history.clear) {
    clearButton.addEventListener("click", function () {
      if (!clearArmed) {
        clearArmed = true;
        clearButton.textContent = "Press again to clear";
        return;
      }
      clearArmed = false;
      clearButton.textContent = "Clear history";
      try { psycho.history.clear(); say("History cleared"); } catch (error) { say("History was not cleared"); }
    });
  }

  // ---- Vault. The password vault is sealed with a passphrase and lives only in the broker; this page asks
  // it to create, unlock, lock and list, and never holds a password except one the user asked to see.
  var vaultCall = typeof psycho !== "undefined" && psycho.vault ? psycho.vault : null;
  if (vaultCall) {
    var vaultNote = controlFor("vaultStatus");
    var entriesBox = controlFor("dynVaultEntries");
    var REASONS = {
      "vault.passphrase": "That passphrase does not match, or it is shorter than 8 characters.",
      "vault.exists": "A vault already exists. Unlock it instead.",
      "vault.missing": "There is no vault yet. Set a master password to create one.",
      "vault.locked": "The vault is locked. Unlock it first.",
      "vault.duplicate": "The vault already keeps a login for that site.",
      "vault.entry": "An entry needs a site and a password (no line breaks or \" | \").",
      "vault.missing-entry": "That login is not in the vault."
    };
    var vaultMessage = function (error) {
      var text = String(error && error.message ? error.message : error);
      for (var code in REASONS) if (text.indexOf(code) >= 0) return REASONS[code];
      return "The vault did not accept that.";
    };
    var shown = {};
    var paintEntries = function (entries) {
      if (!entriesBox) return;
      while (entriesBox.firstChild) entriesBox.removeChild(entriesBox.firstChild);
      shown = {};
      if (!entries.length) {
        entriesBox.appendChild(document.createTextNode("No entries yet."));
        return;
      }
      for (var i = 0; i < entries.length; i++) {
        (function (entry) {
          var row = document.createElement("div");
          row.className = "vrow";
          var label = document.createElement("span");
          label.className = "vsite";
          label.textContent = entry.site + (entry.user ? "  -  " + entry.user : "");
          row.appendChild(label);
          var secret = document.createElement("span");
          secret.className = "vsecret";
          row.appendChild(secret);
          var action = function (name, handler) {
            var button = document.createElement("button");
            button.className = "btn";
            button.type = "button";
            button.textContent = name;
            button.addEventListener("click", handler);
            row.appendChild(button);
          };
          action("Show", function () {
            try { secret.textContent = vaultCall.reveal({ site: entry.site }).password; } catch (error) { say(vaultMessage(error)); }
          });
          action("Copy", function () {
            try { vaultCall.copy({ site: entry.site }); say("Copied; the clipboard is emptied in a minute."); } catch (error) { say(vaultMessage(error)); }
          });
          action("Delete", function () {
            try { vaultCall.remove({ site: entry.site }); refreshVault(); say("Deleted"); } catch (error) { say(vaultMessage(error)); }
          });
          entriesBox.appendChild(row);
        }(entries[i]));
      }
    };
    var refreshVault = function () {
      var state = null;
      try { state = vaultCall.status(); } catch (error) { if (vaultNote) vaultNote.textContent = "The vault is not available."; return; }
      if (vaultNote) {
        vaultNote.textContent = !state.exists ? "No vault yet - set a master password to create one."
          : state.locked ? "Vault locked." : "Vault unlocked - " + state.count + (state.count === 1 ? " entry" : " entries");
      }
      if (state.exists && !state.locked) {
        try { paintEntries(vaultCall.list().entries || []); } catch (error) { say(vaultMessage(error)); }
      } else if (entriesBox) {
        while (entriesBox.firstChild) entriesBox.removeChild(entriesBox.firstChild);
        entriesBox.appendChild(document.createTextNode(state.exists ? "Unlock the vault to see its entries." : "No entries yet."));
      }
    };
    var press = function (id, handler) { onPress(id, function () { try { handler(); } catch (error) { say(vaultMessage(error)); } }); };
    press("vset", function () {
      var box = controlFor("vnew");
      vaultCall.create({ passphrase: box.value });
      box.value = "";
      say("Vault created and unlocked");
      refreshVault();
    });
    press("vgo", function () {
      var box = controlFor("vpw");
      vaultCall.unlock({ passphrase: box.value });
      box.value = "";
      say("Vault unlocked");
      refreshVault();
    });
    press("vLock", function () { vaultCall.lock(); say("Vault locked"); refreshVault(); });
    press("vRefresh", function () { refreshVault(); });
    press("eGen", function () { controlFor("ePass").value = vaultCall.generate().password; });
    press("eSave", function () {
      vaultCall.add({ site: controlFor("eSite").value, user: controlFor("eUser").value, pass: controlFor("ePass").value });
      controlFor("eSite").value = ""; controlFor("eUser").value = ""; controlFor("ePass").value = "";
      say("Saved in the vault");
      refreshVault();
    });
    // This vault is always sealed with a passphrase, so there is no "stop using a master password", and
    // backups and CSV import are not built yet; those buttons say so instead of pretending.
    ["vforget", "vBackup", "vRestore", "vImport"].forEach(function (id) {
      onPress(id, function () { say(id === "vforget" ? "This vault is always sealed with a passphrase." : "Backups and import are not built yet."); });
    });
    refreshVault();
  }

  // ---- Data, diagnostics, error log, restart and updates. Each answer comes from the broker; the page keeps
  // nothing of its own.
  var later = function (milliseconds, work) { if (typeof setTimeout === "function") setTimeout(work, milliseconds); };
  var guarded = function (work) { try { work(); } catch (error) { say("That did not work: " + String(error && error.message ? error.message : error)); } };
  if (psycho.data) {
    var usageBox = controlFor("dynDataUsage");
    var paintUsage = function () {
      if (!usageBox) return;
      var rows = [];
      try { rows = psycho.data.usage().rows || []; } catch (error) { return; }
      while (usageBox.firstChild) usageBox.removeChild(usageBox.firstChild);
      for (var i = 0; i < rows.length; i++) {
        (function (item) {
          var row = document.createElement("div");
          row.className = "vrow";
          var label = document.createElement("span");
          label.className = "vsite";
          label.textContent = item.label + "  -  " + item.count + " " + item.unit;
          row.appendChild(label);
          var button = document.createElement("button");
          button.className = "btn danger";
          button.type = "button";
          button.textContent = "Clear";
          var armed = false;
          button.addEventListener("click", function () {
            if (!armed) { armed = true; button.textContent = "Press again"; return; }
            armed = false;
            button.textContent = "Clear";
            guarded(function () { psycho.data.clear({ what: item.id }); say("Cleared " + item.label.toLowerCase()); });
            later(900, paintUsage);
          });
          row.appendChild(button);
          usageBox.appendChild(row);
        }(rows[i]));
      }
    };
    onPress("dataRefresh", paintUsage);
    paintUsage();
  }
  if (psycho.diagnostics) {
    var reportBox = controlFor("diagnosticsReport");
    var fillReport = function () {
      if (reportBox) reportBox.value = "PsychoVault troubleshooting report\n" + psycho.diagnostics.report().text;
    };
    onPress("diagnosticsRefresh", function () { guarded(fillReport); });
    onPress("diagnosticsCopy", function () { guarded(function () { fillReport(); psycho.diagnostics.copy(); say("Report copied"); }); });
    guarded(fillReport);
  }
  if (psycho.app) {
    onPress("openErrorLog", function () { guarded(function () { psycho.app.openlog(); say("Opening the error log"); }); });
    var restartButton = controlFor("relaunch");
    var restartArmed = false;
    if (restartButton) {
      restartButton.addEventListener("click", function () {
        if (!restartArmed) { restartArmed = true; restartButton.textContent = "Press again to restart"; return; }
        restartArmed = false;
        restartButton.textContent = "Restart PsychoVault";
        guarded(function () { psycho.app.relaunch(); });
      });
    }
  }
  if (psycho.updates) {
    var updateBox = controlFor("updateResult");
    var showUpdate = function (tries) {
      var text = "";
      try { text = psycho.updates.status().text; } catch (error) { return; }
      if (updateBox) updateBox.textContent = text || "No check has run yet.";
      if (tries > 0 && /Checking|Downloading/.test(text)) later(1500, function () { showUpdate(tries - 1); });
    };
    onPress("updateCheckNow", function () {
      guarded(function () { psycho.updates.check(); if (updateBox) updateBox.textContent = "Checking..."; later(1500, function () { showUpdate(8); }); });
    });
    showUpdate(0);
  }
  ["updateToken", "updateTokenSave", "updateTokenForget"].forEach(function (id) {
    var node = controlFor(id);
    if (node) { node.setAttribute("disabled", ""); node.setAttribute("title", "Not used: update checks read public releases."); }
  });

  // ---- Structured editors: speed dial tiles, sidebar services, containers and per-site permissions. Each reads
  // its list from the broker, edits it in rows, and saves the whole list when asked; the broker refuses a bad
  // row and says which kind of mistake it was.
  var field = function (placeholder, value, width) {
    var input = document.createElement("input");
    input.className = "field";
    input.type = "text";
    input.placeholder = placeholder;
    input.value = value || "";
    if (width) input.style.width = width;
    return input;
  };
  var button = function (label, handler, danger) {
    var node = document.createElement("button");
    node.className = danger ? "btn danger" : "btn";
    node.type = "button";
    node.textContent = label;
    node.addEventListener("click", handler);
    return node;
  };
  var currentValue = function (key, fallback) {
    try {
      var values = (bridge.list() || {}).values || {};
      return values[key] === undefined ? fallback : values[key];
    } catch (error) { return fallback; }
  };
  var saveStructured = function (key, value, what) {
    try {
      bridge.set({ key: key, value: JSON.stringify(value) });
      say(what + " saved");
      return true;
    } catch (error) {
      var text = String(error && error.message ? error.message : error);
      var at = text.indexOf("setting.structure");
      say("Not saved: " + (at >= 0 ? text.substr(at + 18) : text));
      return false;
    }
  };
  // rows: array of objects; fields: [{name, placeholder, width}]
  var listEditor = function (boxId, addId, saveId, key, noun, fields, blank) {
    var box = controlFor(boxId);
    if (!box) return;
    var rows = [];
    var paint = function () {
      while (box.firstChild) box.removeChild(box.firstChild);
      if (!rows.length) box.appendChild(document.createTextNode("Nothing here yet. Add one."));
      rows.forEach(function (row, index) {
        var line = document.createElement("div");
        line.className = "vrow";
        row.__inputs = fields.map(function (spec) {
          var input = field(spec.placeholder, row[spec.name], spec.width);
          input.addEventListener("change", function () { row[spec.name] = input.value; });
          line.appendChild(input);
          return input;
        });
        line.appendChild(button("Remove", function () { rows.splice(index, 1); paint(); }, true));
        box.appendChild(line);
      });
    };
    var load = function () {
      var stored = currentValue(key, []);
      rows = stored.map(function (item) {
        var copy = {};
        fields.forEach(function (spec) { copy[spec.name] = item[spec.name]; });
        return copy;
      });
      paint();
    };
    onPress(addId, function () { var row = {}; fields.forEach(function (spec) { row[spec.name] = blank[spec.name] || ""; }); rows.push(row); paint(); });
    onPress(saveId, function () {
      // Read the boxes now: a field's change event only fires when it loses focus.
      var out = rows.map(function (row) {
        var copy = {};
        fields.forEach(function (spec, n) { copy[spec.name] = row.__inputs ? row.__inputs[n].value : row[spec.name]; });
        return copy;
      });
      if (saveStructured(key, out, noun)) { rows = out; paint(); }
    });
    load();
  };
  listEditor("dynTiles", "tileAdd", "tileSave", "speedDial", "Speed dial tiles",
             [{ name: "title", placeholder: "Title", width: "200px" }, { name: "url", placeholder: "https://example.com", width: "340px" }], {});
  listEditor("dynServices", "svcAdd", "svcSave", "services", "Sidebar services",
             [{ name: "name", placeholder: "Name", width: "200px" }, { name: "url", placeholder: "https://example.com", width: "340px" }], {});
  listEditor("dynContainers", "contAdd", "contSave", "containers", "Containers",
             [{ name: "name", placeholder: "Name", width: "200px" }, { name: "color", placeholder: "#38bdf8", width: "120px" }], { color: "#38bdf8" });

  // Per-site permissions: pick a site, set what it may use, save or remove its rules.
  var permBox = controlFor("dynSitePermissions");
  var permOrigin = controlFor("permOrigin");
  var PERM_FIELDS = { camera: "permCamera", microphone: "permMicrophone", location: "permLocation", notifications: "permNotifications" };
  var originOf = function (text) {
    var match = /^(https?):\/\/([^\/\s?#]+)/i.exec(String(text || "").replace(/^\s+|\s+$/g, ""));
    return match ? match[1].toLowerCase() + "://" + match[2].toLowerCase() : "";
  };
  var allRules = function () { return currentValue("sitePermissions", {}); };
  var paintRules = function () {
    if (!permBox) return;
    while (permBox.firstChild) permBox.removeChild(permBox.firstChild);
    var rules = allRules(), any = false;
    for (var origin in rules) {
      any = true;
      (function (site) {
        var line = document.createElement("div");
        line.className = "vrow";
        var label = document.createElement("span");
        label.className = "vsite";
        var parts = [];
        for (var name in rules[site]) parts.push(name + ": " + rules[site][name]);
        label.textContent = site + "  -  " + parts.join(", ");
        line.appendChild(label);
        line.appendChild(button("Edit", function () { permOrigin.value = site; loadSite(); }));
        permBox.appendChild(line);
      }(origin));
    }
    if (!any) permBox.appendChild(document.createTextNode("No site has its own rules. Defaults apply."));
  };
  var loadSite = function () {
    var origin = originOf(permOrigin.value);
    if (!origin) { say("Enter a site such as https://example.com"); return null; }
    permOrigin.value = origin;
    var rule = allRules()[origin] || {};
    for (var name in PERM_FIELDS) { var select = controlFor(PERM_FIELDS[name]); if (select) select.value = rule[name] || "default"; }
    say("Editing " + origin);
    return origin;
  };
  if (permBox && permOrigin) {
    onPress("permLoad", function () { loadSite(); });
    onPress("permSave", function () {
      var origin = originOf(permOrigin.value);
      if (!origin) { say("Enter a site such as https://example.com"); return; }
      var rules = allRules(), rule = {};
      for (var name in PERM_FIELDS) { var value = controlFor(PERM_FIELDS[name]).value; if (value === "allow" || value === "deny") rule[name] = value; }
      if (Object.keys(rule).length) rules[origin] = rule; else delete rules[origin];
      if (saveStructured("sitePermissions", rules, "Site rules")) paintRules();
    });
    onPress("permRemove", function () {
      var origin = originOf(permOrigin.value);
      var rules = allRules();
      if (origin && rules[origin]) { delete rules[origin]; if (saveStructured("sitePermissions", rules, "Site rules")) paintRules(); }
    });
    paintRules();
  }

  // Download folder: the system's folder dialog opens over the window; this page watches for the answer.
  if (psycho.downloads && psycho.downloads.pickdir) {
    onPress("downloadDirPick", function () {
      var before = currentValue("downloadDir", "");
      guarded(function () { psycho.downloads.pickdir(); });
      var polls = 0;
      var watch = function () {
        var now = currentValue("downloadDir", "");
        if (now !== before) { controlFor("downloadDir").value = now; say("Download folder saved"); return; }
        if (++polls < 120) later(700, watch);
      };
      later(700, watch);
    });
  }

  // ---- Extensions and proxy. The window owns the extension host; this page lists what is installed, asks for
  // an unpacked folder to be added (the system's folder dialog opens), enables, disables or removes one, and
  // reports what the proxy setting is. A .crx cannot be installed yet and the page says so.
  if (psycho.extensions) {
    var extBox = controlFor("dynExtensions");
    var extResult = controlFor("extResult");
    var tell = function (text) { if (extResult) extResult.textContent = text; say(text); };
    var listing2 = function () {
      try { return psycho.extensions.list(); } catch (error) { return { extensions: [], error: "" }; }
    };
    var paintExtensions = function () {
      if (!extBox) return;
      var answer = listing2(), items = answer.extensions || [];
      while (extBox.firstChild) extBox.removeChild(extBox.firstChild);
      if (!items.length) extBox.appendChild(document.createTextNode("No extensions are installed."));
      items.forEach(function (item) {
        var line = document.createElement("div");
        line.className = "vrow";
        var label = document.createElement("span");
        label.className = "vsite";
        label.textContent = item.name + (item.version ? "  v" + item.version : "") + (item.bundled ? "  (bundled)" : "") +
          "  -  " + (item.error ? "problem: " + item.error : item.enabled ? "enabled" : "disabled");
        line.appendChild(label);
        line.appendChild(button(item.enabled ? "Disable" : "Enable", function () {
          guarded(function () { psycho.extensions.toggle({ id: item.id, enabled: !item.enabled }); });
          later(600, paintExtensions);
        }));
        if (!item.bundled) {
          line.appendChild(button("Remove", function () {
            guarded(function () { psycho.extensions.remove({ id: item.id }); tell("Removed " + item.name); });
            later(600, paintExtensions);
          }, true));
        }
        extBox.appendChild(line);
      });
      if (answer.error) tell(answer.error);
    };
    onPress("extReload", function () { paintExtensions(); tell("Reloaded the list"); });
    onPress("extAddFolder", function () {
      var before = (listing2().extensions || []).length;
      guarded(function () { psycho.extensions.addfolder(); });
      var polls = 0;
      var watch = function () {
        var now = listing2();
        if ((now.extensions || []).length !== before || now.error) { paintExtensions(); tell(now.error || "Extension added"); return; }
        if (++polls < 120) later(700, watch);
      };
      later(700, watch);
    });
    onPress("extAddCrx", function () { tell("Installing a .crx file is not built yet. Load an unpacked folder instead."); });
    onPress("protonVpnRestore", function () { guarded(function () { psycho.extensions.restore(); }); later(600, paintExtensions); tell("Looked for the bundled extensions again"); });
    onPress("protonVpnStatus", function () {
      var proton = (listing2().extensions || []).filter(function (item) { return /proton/i.test(item.name) || /proton/i.test(item.id); });
      tell(proton.length ? proton.map(function (item) { return item.name + ": " + (item.error ? "problem - " + item.error : item.enabled ? "installed and enabled" : "installed but disabled"); }).join(". ")
                         : "No Proton VPN extension is installed in this build.");
    });
    onPress("vpnCheckOpen", function () { guarded(function () { psycho.tab["new"]({ url: "psychovault://nettest" }); }); });
    var describeProxy = function () {
      var proxy = null;
      try { proxy = psycho.proxy.status(); } catch (error) { return "The proxy setting is not available."; }
      return proxy.active ? "Traffic goes through a proxy (" + proxy.mode + ")." : "No proxy is set: traffic goes direct.";
    };
    onPress("proxyStatus", function () { tell(describeProxy()); });
    onPress("proxySelfTest", function () {
      tell(describeProxy() + " Run the VPN checks to compare your public address and DNS with and without it.");
    });
    var switchBox = controlFor("dynProxySwitch");
    if (switchBox) { switchBox.textContent = describeProxy(); }
    paintExtensions();
  }
}());
