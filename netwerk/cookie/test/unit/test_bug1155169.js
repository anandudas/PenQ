var {utils: Cu, interfaces: Ci, classes: Cc} = Components;

Cu.import("resource://gre/modules/Services.jsm");

const URI = Services.io.newURI("http://example.org/", null, null);

const cs = Cc["@mozilla.org/cookieService;1"]
             .getService(Ci.nsICookieService);

function run_test() {
  // Allow all cookies.
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);

  // Clear cookies.
  Services.cookies.removeAll();

  // Add a new cookie.
  setCookie("foo=bar", {
    type: "added", isSession: true, isSecure: false, isHttpOnly: false
  });

  // Update cookie with isHttpOnly=true.
  setCookie("foo=bar; HttpOnly", {
    type: "changed", isSession: true, isSecure: false, isHttpOnly: true
  });

  // Update cookie with isSecure=true.
  setCookie("foo=bar; Secure", {
    type: "changed", isSession: true, isSecure: true, isHttpOnly: false
  });

  // Update cookie with isSession=false.
  let expiry = new Date();
  expiry.setUTCFullYear(expiry.getUTCFullYear() + 2);
  setCookie(`foo=bar; Expires=${expiry.toGMTString()}`, {
    type: "changed", isSession: false, isSecure: false, isHttpOnly: false
  });

  // Reset cookie.
  setCookie("foo=bar", {
    type: "changed", isSession: true, isSecure: false, isHttpOnly: false
  });
}

function setCookie(value, expected) {
  function setCookieInternal(value, expected = null) {
    function observer(subject, topic, data) {
      if (!expected) {
        do_throw("no notification expected");
        return;
      }

      // Check we saw the right notification.
      do_check_eq(data, expected.type);

      // Check cookie details.
      let cookie = subject.QueryInterface(Ci.nsICookie2);
      do_check_eq(cookie.isSession, expected.isSession);
      do_check_eq(cookie.isSecure, expected.isSecure);
      do_check_eq(cookie.isHttpOnly, expected.isHttpOnly);
    }

    Services.obs.addObserver(observer, "cookie-changed", false);
    cs.setCookieStringFromHttp(URI, null, null, value, null, null);
    Services.obs.removeObserver(observer, "cookie-changed");
  }

  // Check that updating/inserting the cookie works.
  setCookieInternal(value, expected);

  // Check that we ignore identical cookies.
  setCookieInternal(value);
}
