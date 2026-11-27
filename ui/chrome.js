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
