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
  function send(key, control) {
    try {
      bridge.set({ key: key, value: JSON.stringify(readValue(control)) });
      say("Saved");
    } catch (error) {
      say("Not saved: " + key);
    }
  }
  if (!bridge) return;
  var listing = null;
  try { listing = bridge.list(); } catch (error) { say("Settings are unavailable"); return; }
  var values = (listing && listing.values) || {};
  var unwired = (listing && listing.unwired) || {};
  var key;
  for (key in values) {
    (function (name) {
      var control = controlFor(name);
      if (!control) return;
      writeValue(control, values[name]);
      var send_now = function () { send(name, control); };
      control.addEventListener("change", send_now);
      control.setAttribute("data-wired", "true");
    }(key));
  }
  for (key in unwired) {
    var control = controlFor(key);
    if (!control) continue;
    control.setAttribute("data-unwired", unwired[key]);
    control.setAttribute("title", "Not connected to the profile yet (owner " + unwired[key] + ")");
  }
}());
