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
      var close = el("button", "x", "×");
      close.setAttribute("data-close", String(tab.id));
      node.appendChild(close);
      tabsEl.appendChild(node);
    }
    var after = tabs.length * (TAB_WIDTH + TAB_GAP);
    plusEl.style.left = (36 + after + 2) + "px";
    wsEl.style.left = (36 + after + 40) + "px";
    setDim("nav-back", !state.canBack);
    setDim("nav-forward", !state.canForward);
    if (document.activeElement !== addressEl && typeof state.address === "string") addressEl.value = state.address;
    var count = document.querySelector(".shield .count");
    if (count) count.textContent = String(state.blocked || 0);
  }
  window.psychoChromeState = render;

  // Actions
  function ancestorWith(node, attribute) {
    // The engine reports the text node under the pointer as the target, so walk up through it.
    while (node) {
      if (node.getAttribute && node.getAttribute(attribute) !== null) return node;
      node = node.parentNode;
    }
    return null;
  }
  tabsEl.addEventListener("click", function (event) {
    var closer = ancestorWith(event.target, "data-close");
    if (closer) {
      bridge("tab", "close", { id: Number(closer.getAttribute("data-close")) });
      return;
    }
    var tab = ancestorWith(event.target, "data-tab");
    if (tab) bridge("tab", "activate", { id: Number(tab.getAttribute("data-tab")) });
  });
  function on(id, group, method, args) {
    var node = document.getElementById(id);
    if (node) node.addEventListener("click", function () { bridge(group, method, args); });
  }
  on("new-tab", "tab", "new", { url: "psychovault://newtab" });
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
