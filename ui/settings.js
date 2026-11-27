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
}());
