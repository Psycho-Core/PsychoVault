(function () {
  // Category navigation only: show one panel, mark one nav item. The controls
  // themselves are present but not wired to the profile yet (see the plan, S083/S088).
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
}());
