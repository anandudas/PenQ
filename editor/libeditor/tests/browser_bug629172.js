add_task(function*() {
  yield new Promise(resolve => waitForFocus(resolve, window));

  const kPageURL = "http://example.org/browser/editor/libeditor/tests/bug629172.html";
  yield BrowserTestUtils.withNewTab({
    gBrowser,
    url: kPageURL
  }, function*(aBrowser) {
    yield ContentTask.spawn(aBrowser, {}, function*() {
      var window = content.window.wrappedJSObject;
      var document = window.document;

      // Note: Using the with keyword, we would have been able to write this as:
      //
      // with (window) {
      //   Screenshots = {};
      //   // so on
      // }
      //
      // However, browser-chrome tests are somehow forced to run in strict mode,
      // which doesn't permit the usage of the with keyword, turning the following
      // into the ugliest test you've ever seen.  :(
      var LTRRef = document.getElementById("ltr-ref");
      var RTLRef = document.getElementById("rtl-ref");
      window.Screenshots = {};

      // generate the reference screenshots
      LTRRef.style.display = "";
      document.body.clientWidth;
      window.Screenshots.ltr = window.snapshotWindow(window);
      LTRRef.parentNode.removeChild(LTRRef);
      RTLRef.style.display = "";
      document.body.clientWidth;
      window.Screenshots.rtl = window.snapshotWindow(window);
      RTLRef.parentNode.removeChild(RTLRef);
      window.Screenshots.get = function(dir, flip) {
        if (flip) {
          return this[dir == "rtl" ? "ltr" : "rtl"];
        } else {
          return this[dir];
        }
      };
    });

    function simulateCtrlShiftX(aBrowser) {
      // In e10s, the keypress event will be dispatched to the content process,
      // but in non-e10s it is handled by the browser UI code and hence won't
      // reach the web page.  As a result, we need to observe the event in
      // the content process only in e10s mode.
      var waitForKeypressContent = BrowserTestUtils.waitForContentEvent(aBrowser, "keypress");
      EventUtils.synthesizeKey("x", {accelKey: true, shiftKey: true});
      if (gMultiProcessBrowser) {
        return waitForKeypressContent;
      }
      return Promise.resolve();
    }

    function* testDirection(initialDir, aBrowser) {
      yield ContentTask.spawn(aBrowser, {initialDir}, function({initialDir}) {
        var window = content.window.wrappedJSObject;
        var document = window.document;

        var t = window.t = document.createElement("textarea");
        t.setAttribute("dir", initialDir);
        t.value = "test.";
        window.inputEventCount = 0;
        t.oninput = function() { window.inputEventCount++; };
        document.getElementById("content").appendChild(t);
        document.body.clientWidth;
        var s1 = window.snapshotWindow(window);
        ok(window.compareSnapshots(s1, window.Screenshots.get(initialDir, false), true)[0],
           "Textarea should appear correctly before switching the direction (" + initialDir + ")");
        t.focus();
        is(window.inputEventCount, 0, "input event count must be 0 before");
      });
      yield simulateCtrlShiftX(aBrowser);
      yield ContentTask.spawn(aBrowser, {initialDir}, function({initialDir}) {
        var window = content.window.wrappedJSObject;

        is(window.t.getAttribute("dir"), initialDir == "ltr" ? "rtl" : "ltr", "The dir attribute must be correctly updated");
        is(window.inputEventCount, 1, "input event count must be 1 after");
        window.t.blur();
        var s2 = window.snapshotWindow(window);
        ok(window.compareSnapshots(s2, window.Screenshots.get(initialDir, true), true)[0],
           "Textarea should appear correctly after switching the direction (" + initialDir + ")");
        window.t.focus();
        is(window.inputEventCount, 1, "input event count must be 1 before");
      });
      yield simulateCtrlShiftX(aBrowser);
      yield ContentTask.spawn(aBrowser, {initialDir}, function({initialDir}) {
        var window = content.window.wrappedJSObject;

        is(window.inputEventCount, 2, "input event count must be 2 after");
        is(window.t.getAttribute("dir"), initialDir == "ltr" ? "ltr" : "rtl", "The dir attribute must be correctly updated");
        window.t.blur();
        var s3 = window.snapshotWindow(window);
        ok(window.compareSnapshots(s3, window.Screenshots.get(initialDir, false), true)[0],
           "Textarea should appear correctly after switching back the direction (" + initialDir + ")");
        window.t.parentNode.removeChild(window.t);
      });
    }

    yield testDirection("ltr", aBrowser);
    yield testDirection("rtl", aBrowser);
  });
});
