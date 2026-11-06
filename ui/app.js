(function () {
  function byId(id) { return document.getElementById(id); }
  function showStatus(message) {
    var status = byId("status");
    if (status) status.textContent = message;
  }
  function openAddress(address) {
    var target = (address || "").replace(/^\s+|\s+$/g, "");
    if (!target) {
      showStatus("Enter a web address first.");
      return;
    }
    if (typeof psycho === "undefined" || !psycho.tab || !psycho.tab.new) {
      showStatus("Broker bridge is not connected in this preview.");
      return;
    }
    psycho.tab.new({ url: target });
    showStatus("Asked the broker to open " + target);
  }
  var openButton = byId("open-address");
  if (openButton) {
    openButton.addEventListener("click", function () {
      var field = byId("address");
      openAddress(field ? field.value : "");
    });
  }
  var field = byId("address");
  if (field) {
    field.addEventListener("keydown", function (event) {
      if (event.key === "Enter") openAddress(field.value);
    });
  }
  var newTab = byId("new-tab");
  if (newTab) {
    newTab.addEventListener("click", function () {
      if (typeof psycho === "undefined" || !psycho.tab || !psycho.tab.new) {
        showStatus("Broker bridge is not connected in this preview.");
        return;
      }
      psycho.tab.new({ url: "psychovault://newtab" });
      showStatus("Asked the broker to create a new tab.");
    });
  }
}());
