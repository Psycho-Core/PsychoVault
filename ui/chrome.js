(function () {
  // Browser chrome behaviour. The shell pushes state through psychoChromeState(); this page draws
  // it and sends every user action back through the curated psycho bridge (tab, nav and win
  // methods only). Nothing here reads or writes the profile.
  var TAB_WIDTH = 122, TAB_GAP = 4;
  var tabsEl = document.getElementById("tabs");
  var plusEl = document.getElementById("new-tab");
  var wsEl = document.getElementById("workspaces");
  var addressEl = document.getElementById("address");
  var current = { tabs: [], activeId: 0 };

  function bridge(group, method, args) {
    if (typeof psycho === "undefined" || !psycho[group] || !psycho[group][method]) return;
    psycho[group][method](args || {});
  }
  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }
  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function setDim(id, dim) {
    var node = document.getElementById(id);
    if (node) node.className = dim ? "nav dim" : "nav";
  }

  function render(state) {
    current = state;
    clear(tabsEl);
    var tabs = state.tabs || [];
    for (var i = 0; i < tabs.length; i++) {
      var tab = tabs[i];
      var node = el("div", "tab" + (tab.id === state.activeId ? " active" : "") + (tab.loading ? " loading" : ""));
      node.setAttribute("data-tab", String(tab.id));
      node.style.left = (i * (TAB_WIDTH + TAB_GAP)) + "px";
      var title = tab.title || tab.url || "New Tab";
      node.appendChild(el("span", "fav", tab.loading ? "•" : title.charAt(0).toUpperCase()));
      node.appendChild(el("span", "title", title));
      if (tab.muted) node.appendChild(el("span", "mute", "M"));
      if (!tab.pinned) node.appendChild(el("button", "x", "×"));
      else node.className += " pinned";
      tabsEl.appendChild(node);
    }
    var after = tabs.length * (TAB_WIDTH + TAB_GAP);
    plusEl.style.left = (36 + after + 2) + "px";
    wsEl.style.left = (36 + after + 40) + "px";
    if (state.dialog) showDialog(state.dialog); else hideDialog();
    var shield = document.getElementById("rail-adblock");
    if (shield && typeof state.blockAds === "boolean") shield.className = state.blockAds ? "rail-btn good" : "rail-btn off";
    setDim("nav-back", !state.canBack);
    setDim("nav-forward", !state.canForward);
    if (document.activeElement !== addressEl && typeof state.address === "string") addressEl.value = state.address;
    var statusText = document.getElementById("status-text");
    if (statusText && typeof state.status === "string") statusText.textContent = state.status;
    var count = document.querySelector(".shield .count");
    if (count) count.textContent = String(state.blocked || 0);
  }
  // ---- Modal dialog: alert, confirm or prompt, drawn over the page; the shell sends it every pointer
  // and key event while it is open and keeps tab, address and shortcuts inert. Tab is trapped here.
  var scrim = document.getElementById("scrim");
  var dlgInput = document.getElementById("dlg-input");
  var dlgActions = document.getElementById("dlg-actions");
  var dialogId = 0, dialogButtons = [], dialogKind = "";
  function answer(button) {
    var id = dialogId;
    var value = dialogKind === "prompt" ? dlgInput.value : "";
    dialogId = 0;
    scrim.style.display = "none";
    bridge("ui", "dialog", { id: id, button: button, value: value });
  }
  function focusables() {
    var list = [];
    if (dialogKind === "prompt") list.push(dlgInput);
    for (var i = 0; i < dialogButtons.length; i++) list.push(dialogButtons[i]);
    return list;
  }
  function showDialog(dialog) {
    if (dialog.id === dialogId) return;
    dialogId = dialog.id;
    dialogKind = dialog.kind;
    document.getElementById("dlg-title").textContent = dialog.title || "";
    document.getElementById("dlg-message").textContent = dialog.message || "";
    dlgInput.style.display = dialog.kind === "prompt" ? "block" : "none";
    dlgInput.value = dialog.value || "";
    clear(dlgActions);
    dialogButtons = [];
    var labels = dialog.buttons || [];
    for (var i = 0; i < labels.length; i++) {
      var button = el("button", i === labels.length - 1 ? "primary" : "", labels[i]);
      button.setAttribute("data-button", String(i));
      dlgActions.appendChild(button);
      dialogButtons.push(button);
    }
    scrim.style.display = "block";
    var first = dialog.kind === "prompt" ? dlgInput : dialogButtons[dialogButtons.length - 1];
    if (first && first.focus) first.focus();
  }
  function hideDialog() {
    if (!dialogId) return;
    dialogId = 0;
    scrim.style.display = "none";
  }
  dlgActions.addEventListener("click", function (event) {
    var node = event.target;
    while (node) {
      if (node.getAttribute && node.getAttribute("data-button") !== null) {
        answer(Number(node.getAttribute("data-button")));
        return;
      }
      node = node.parentNode;
    }
  });
  scrim.addEventListener("keydown", function (event) {
    if (!dialogId) return;
    if (event.key === "Escape") {
      // Escape picks the first button (Cancel, No or OK for an alert).
      answer(0);
      if (event.preventDefault) event.preventDefault();
    } else if (event.key === "Tab") {
      var list = focusables(), at = -1;
      for (var i = 0; i < list.length; i++) if (list[i] === document.activeElement) at = i;
      if (list.length) list[(at + 1) % list.length].focus();
      if (event.preventDefault) event.preventDefault();
    } else if (event.key === "Enter" && dialogKind === "prompt" && document.activeElement === dlgInput) {
      answer(dialogButtons.length - 1);
      if (event.preventDefault) event.preventDefault();
    }
  });

  window.psychoChromeState = render;

  // Actions
  tabsEl.addEventListener("click", function (event) {
    // A close button is the element with class "x" inside a tab; the tab carries the id.
    var node = event.target, closing = false;
    while (node) {
      if (node.className === "x") closing = true;
      if (node.getAttribute && node.getAttribute("data-tab") !== null) {
        var id = Number(node.getAttribute("data-tab"));
        if (closing) bridge("tab", "close", { id: id });
        else bridge("tab", "activate", { id: id });
        return;
      }
      node = node.parentNode;
    }
  });
  // The tab menu. It opens over the page, so the shell is told (ui.overlay) to send every pointer
  // event to this page while it is open.
  var menuEl = document.getElementById("tabmenu");
  var menuTab = 0;
  function menuOpen() { return menuEl && menuEl.style.display !== "none"; }
  function hideMenu() {
    if (!menuOpen()) return;
    menuEl.style.display = "none";
    bridge("ui", "overlay", { open: false });
  }
  function tabIdOf(node) {
    while (node) {
      if (node.getAttribute && node.getAttribute("data-tab") !== null) return Number(node.getAttribute("data-tab"));
      node = node.parentNode;
    }
    return 0;
  }
  function tabState(id) {
    var tabs = current.tabs || [];
    for (var i = 0; i < tabs.length; i++) if (tabs[i].id === id) return tabs[i];
    return null;
  }
  tabsEl.addEventListener("contextmenu", function (event) {
    var id = tabIdOf(event.target);
    if (!id || !menuEl) return;
    var tab = tabState(id) || {};
    menuTab = id;
    menuEl.querySelector('[data-act="pin"]').textContent = tab.pinned ? "Unpin tab" : "Pin tab";
    menuEl.querySelector('[data-act="mute"]').textContent = tab.muted ? "Unmute tab" : "Mute tab";
    menuEl.style.left = Math.round(event.clientX) + "px";
    menuEl.style.top = Math.round(event.clientY) + "px";
    var wasOpen = menuOpen();
    menuEl.style.display = "block";
    if (!wasOpen) bridge("ui", "overlay", { open: true });
    if (event.preventDefault) event.preventDefault();
  });
  if (menuEl) {
    menuEl.addEventListener("click", function (event) {
      var node = event.target, act = "";
      while (node && !act) {
        if (node.getAttribute && node.getAttribute("data-act")) act = node.getAttribute("data-act");
        node = node.parentNode;
      }
      var id = menuTab;
      hideMenu();
      if (act === "pin") bridge("tab", "pin", { id: id });
      else if (act === "mute") bridge("tab", "mute", { id: id });
      else if (act === "close") bridge("tab", "close", { id: id });
      else if (act === "new") bridge("tab", "new", { url: "psychovault://newtab" });
      event.stopPropagation();
    });
  }
  // A press anywhere else closes the menu (the shell sends every press here while it is open).
  document.addEventListener("click", function () { hideMenu(); });

  function on(id, group, method, args) {
    var node = document.getElementById(id);
    if (node) node.addEventListener("click", function () { bridge(group, method, args); });
  }
  on("new-tab", "tab", "new", { url: "psychovault://newtab" });
  on("rail-adblock", "rail", "adblock");
  on("nav-back", "nav", "back");
  on("nav-forward", "nav", "forward");
  on("nav-reload", "nav", "reload");
  on("nav-home", "nav", "home");
  on("rail-settings", "tab", "new", { url: "psychovault://settings" });
  on("rail-home", "nav", "home");
  on("win-min", "win", "minimize");
  on("win-max", "win", "maximize");
  on("win-close", "win", "close");
  addressEl.addEventListener("keydown", function (event) {
    if (event.key !== "Enter") return;
    var target = (addressEl.value || "").replace(/^\s+|\s+$/g, "");
    if (target) bridge("nav", "go", { url: target });
  });
}());
