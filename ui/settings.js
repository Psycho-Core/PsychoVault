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
}());
