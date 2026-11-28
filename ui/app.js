(function () {
  // The new-tab page may only ask the broker for a tab, through the curated
  // psycho.tab bridge. Tiles are plain links, so the page's own navigation
  // handles them and no extra bridge method is needed.
  function openAddress(address) {
    var target = (address || "").replace(/^\s+|\s+$/g, "");
    if (!target) return;
    if (typeof psycho === "undefined" || !psycho.tab || !psycho.tab.new) return;
    psycho.tab.new({ url: target });
  }
  var field = document.getElementById("address");
  if (field) {
    field.addEventListener("keydown", function (event) {
      if (event.key === "Enter") openAddress(field.value);
    });
  }

  // The tiles under the search box come from Settings > Appearance > speed dial; the markup in the page is
  // what shows until the broker answers (or if it never does).
  if (typeof psycho !== "undefined" && psycho.ui && psycho.ui.tiles) {
    var box = document.querySelector(".tiles");
    var tiles = [];
    try { tiles = psycho.ui.tiles().tiles || []; } catch (error) { tiles = []; }
    if (box && tiles.length) {
      while (box.firstChild) box.removeChild(box.firstChild);
      for (var i = 0; i < tiles.length; i++) {
        var link = document.createElement("a");
        link.className = "tile";
        link.setAttribute("href", tiles[i].url);
        var icon = document.createElement("span");
        icon.className = "ico";
        icon.textContent = String(tiles[i].title || "?").charAt(0).toUpperCase();
        var label = document.createElement("span");
        label.className = "lbl";
        label.textContent = tiles[i].title;
        link.appendChild(icon);
        link.appendChild(label);
        box.appendChild(link);
      }
    }
  }
}());
