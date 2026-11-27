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
}());
