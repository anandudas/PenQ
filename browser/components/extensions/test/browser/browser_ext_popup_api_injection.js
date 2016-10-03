/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

add_task(function* testPageActionPopup() {
  const BASE = "http://example.com/browser/browser/components/extensions/test/browser";

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      "browser_action": {
        "default_popup": `${BASE}/file_popup_api_injection_a.html`,
      },
      "page_action": {
        "default_popup": `${BASE}/file_popup_api_injection_b.html`,
      },
    },

    files: {
      "popup-a.html": `<html><head><meta charset="utf-8">
                       <script type="application/javascript" src="popup-a.js"></script></head></html>`,
      "popup-a.js": 'browser.test.sendMessage("from-popup-a");',

      "popup-b.html": `<html><head><meta charset="utf-8">
                       <script type="application/javascript" src="popup-b.js"></script></head></html>`,
      "popup-b.js": 'browser.test.sendMessage("from-popup-b");',
    },

    background: function() {
      let tabId;
      browser.tabs.query({active: true, currentWindow: true}, tabs => {
        tabId = tabs[0].id;
        browser.pageAction.show(tabId);
        browser.test.sendMessage("ready");
      });

      browser.test.onMessage.addListener(() => {
        browser.browserAction.setPopup({popup: "/popup-a.html"});
        browser.pageAction.setPopup({tabId, popup: "popup-b.html"});

        browser.test.sendMessage("ok");
      });
    },
  });

  let promiseConsoleMessage = pattern => new Promise(resolve => {
    Services.console.registerListener(function listener(msg) {
      if (pattern.test(msg.message)) {
        resolve(msg.message);
        Services.console.unregisterListener(listener);
      }
    });
  });

  yield extension.startup();
  yield extension.awaitMessage("ready");


  // Check that unprivileged documents don't get the API.
  // BrowserAction:
  let awaitMessage = promiseConsoleMessage(/WebExt Privilege Escalation: BrowserAction/);
  SimpleTest.expectUncaughtException();
  yield clickBrowserAction(extension);

  let message = yield awaitMessage;
  ok(message.includes("WebExt Privilege Escalation: BrowserAction: typeof(browser) = undefined"),
     `No BrowserAction API injection`);

  yield closeBrowserAction(extension);

  // PageAction
  awaitMessage = promiseConsoleMessage(/WebExt Privilege Escalation: PageAction/);
  SimpleTest.expectUncaughtException();
  yield clickPageAction(extension);

  message = yield awaitMessage;
  ok(message.includes("WebExt Privilege Escalation: PageAction: typeof(browser) = undefined"),
     `No PageAction API injection: ${message}`);

  yield closePageAction(extension);

  SimpleTest.expectUncaughtException(false);


  // Check that privileged documents *do* get the API.
  extension.sendMessage("next");
  yield extension.awaitMessage("ok");


  yield clickBrowserAction(extension);
  yield extension.awaitMessage("from-popup-a");
  yield closeBrowserAction(extension);

  yield clickPageAction(extension);
  yield extension.awaitMessage("from-popup-b");
  yield closePageAction(extension);

  yield extension.unload();
});
