/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const TESTPAGE = `${SECURE_TESTROOT}webapi_checkavailable.html`;

Services.prefs.setBoolPref("extensions.webapi.testing", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("extensions.webapi.testing");
});

function testWithAPI(task) {
  return function*() {
    yield BrowserTestUtils.withNewTab(TESTPAGE, task);
  }
}

function API_uninstallByID(browser, id) {
  return ContentTask.spawn(browser, id, function*(id) {
    let addon = yield content.navigator.mozAddonManager.getAddonByID(id);

    let result = yield addon.uninstall();
    return result;
  });
}

add_task(testWithAPI(function*(browser) {
  const ID1 = "addon1@tests.mozilla.org";
  const ID2 = "addon2@tests.mozilla.org";

  let provider = new MockProvider();

  provider.addAddon(new MockAddon(ID1, "Test add-on 1", "extension", 0));
  provider.addAddon(new MockAddon(ID2, "Test add-on 2", "extension", 0));

  let [a1, a2] = yield promiseAddonsByIDs([ID1, ID2]);
  isnot(a1, null, "addon1 is installed");
  isnot(a2, null, "addon2 is installed");

  let result = yield API_uninstallByID(browser, ID1);
  is(result, true, "uninstall of addon1 succeeded");

  [a1, a2] = yield promiseAddonsByIDs([ID1, ID2]);
  is(a1, null, "addon1 is uninstalled");
  isnot(a2, null, "addon2 is still installed");

  result = yield API_uninstallByID(browser, ID2);
  is(result, true, "uninstall of addon2 succeeded");
  [a2] = yield promiseAddonsByIDs([ID2]);
  is(a2, null, "addon2 is uninstalled");
}));
