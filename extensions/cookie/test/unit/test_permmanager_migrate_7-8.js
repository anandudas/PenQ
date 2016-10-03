/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

XPCOMUtils.defineLazyModuleGetter(this, "PlacesTestUtils",
                                  "resource://testing-common/PlacesTestUtils.jsm");

var PERMISSIONS_FILE_NAME = "permissions.sqlite";

function GetPermissionsFile(profile)
{
  let file = profile.clone();
  file.append(PERMISSIONS_FILE_NAME);
  return file;
}

function run_test() {
  run_next_test();
}

add_task(function test() {
  /* Create and set up the permissions database */
  let profile = do_get_profile();

  let db = Services.storage.openDatabase(GetPermissionsFile(profile));
  db.schemaVersion = 7;

  /*
   * V5 table
   */
  db.executeSimpleSQL(
    "CREATE TABLE moz_perms (" +
      " id INTEGER PRIMARY KEY" +
      ",origin TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",modificationTime INTEGER" +
    ")");

  let stmt6Insert = db.createStatement(
    "INSERT INTO moz_perms (" +
      "id, origin, type, permission, expireType, expireTime, modificationTime" +
    ") VALUES (" +
      ":id, :origin, :type, :permission, :expireType, :expireTime, :modificationTime" +
    ")");

  /*
   * V4 table
   */
  db.executeSimpleSQL(
    "CREATE TABLE moz_hosts (" +
      " id INTEGER PRIMARY KEY" +
      ",host TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",modificationTime INTEGER" +
      ",appId INTEGER" +
      ",isInBrowserElement INTEGER" +
    ")");

  let stmtInsert = db.createStatement(
    "INSERT INTO moz_hosts (" +
      "id, host, type, permission, expireType, expireTime, modificationTime, appId, isInBrowserElement" +
    ") VALUES (" +
      ":id, :host, :type, :permission, :expireType, :expireTime, :modificationTime, :appId, :isInBrowserElement" +
    ")");

  /*
   * The v4 table is a backup
   */
  db.executeSimpleSQL("CREATE TABLE moz_hosts_is_backup (dummy INTEGER PRIMARY KEY)");

  let id = 0;

  function insertOrigin(origin, type, permission, expireType, expireTime, modificationTime) {
    let thisId = id++;

    stmt6Insert.bindByName("id", thisId);
    stmt6Insert.bindByName("origin", origin);
    stmt6Insert.bindByName("type", type);
    stmt6Insert.bindByName("permission", permission);
    stmt6Insert.bindByName("expireType", expireType);
    stmt6Insert.bindByName("expireTime", expireTime);
    stmt6Insert.bindByName("modificationTime", modificationTime);

    stmt6Insert.execute();

    return {
      id: thisId,
      origin: origin,
      type: type,
      permission: permission,
      expireType: expireType,
      expireTime: expireTime,
      modificationTime: modificationTime
    };
  }

  function insertHost(host, type, permission, expireType, expireTime, modificationTime, appId, isInBrowserElement) {
    let thisId = id++;

    stmtInsert.bindByName("id", thisId);
    stmtInsert.bindByName("host", host);
    stmtInsert.bindByName("type", type);
    stmtInsert.bindByName("permission", permission);
    stmtInsert.bindByName("expireType", expireType);
    stmtInsert.bindByName("expireTime", expireTime);
    stmtInsert.bindByName("modificationTime", modificationTime);
    stmtInsert.bindByName("appId", appId);
    stmtInsert.bindByName("isInBrowserElement", isInBrowserElement);

    stmtInsert.execute();

    return {
      id: thisId,
      host: host,
      type: type,
      permission: permission,
      expireType: expireType,
      expireTime: expireTime,
      modificationTime: modificationTime,
      appId: appId,
      isInBrowserElement: isInBrowserElement
    };
  }

  let created7 = [
    insertOrigin("https://foo.com", "A", 2, 0, 0, 0),
    insertOrigin("http://foo.com", "A", 2, 0, 0, 0),
    insertOrigin("http://foo.com^appId=1000&inBrowser=1", "A", 2, 0, 0, 0),
    insertOrigin("https://192.0.2.235", "A", 2, 0, 0),
  ];

  // Add some rows to the database
  let created = [
    insertHost("foo.com", "A", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "C", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 1000, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 2000, true),
    insertHost("sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("subber.sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 1000, false),
    insertHost("bar.ca", "A", 1, 0, 0, 0, 1000, true),
    insertHost("localhost", "A", 1, 0, 0, 0, 0, false),
    insertHost("127.0.0.1", "A", 1, 0, 0, 0, 0, false),
    insertHost("192.0.2.235", "A", 1, 0, 0, 0, 0, false),
    // Although ipv6 addresses are written with [] around the IP address,
    // the .host property doesn't contain these []s, which means that we
    // write it like this
    insertHost("2001:db8::ff00:42:8329", "C", 1, 0, 0, 0, 0, false),
    insertHost("file:///some/path/to/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost("file:///another/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost("moz-nullprincipal:{8695105a-adbe-4e4e-8083-851faa5ca2d7}", "A", 1, 0, 0, 0, 0, false),
    insertHost("moz-nullprincipal:{12ahjksd-akjs-asd3-8393-asdu2189asdu}", "B", 1, 0, 0, 0, 0, false),
    insertHost("<file>", "A", 1, 0, 0, 0, 0, false),
    insertHost("<file>", "B", 1, 0, 0, 0, 0, false),
  ];

  // CLose the db connection
  stmtInsert.finalize();
  db.close();
  stmtInsert = null;
  db = null;

  let expected = [
    // We should have kept the previously migrated entries
    ["https://foo.com", "A", 2, 0, 0, 0],
    ["http://foo.com", "A", 2, 0, 0, 0],
    ["http://foo.com^appId=1000&inBrowser=1", "A", 2, 0, 0, 0],

    // Make sure that we also support localhost, and IP addresses
    ["https://localhost:8080", "A", 1, 0, 0],
    ["ftp://127.0.0.1:8080", "A", 1, 0, 0],

    ["http://[2001:db8::ff00:42:8329]", "C", 1, 0, 0],
    ["https://[2001:db8::ff00:42:8329]", "C", 1, 0, 0],
    ["http://192.0.2.235", "A", 1, 0, 0],

    // There should only be one entry of this type in the database
    ["https://192.0.2.235", "A", 2, 0, 0],
  ];

  let found = expected.map((it) => 0);

  // Add some places to the places database
  yield PlacesTestUtils.addVisits(Services.io.newURI("https://foo.com/some/other/subdirectory", null, null));
  yield PlacesTestUtils.addVisits(Services.io.newURI("ftp://some.subdomain.of.foo.com:8000/some/subdirectory", null, null));
  yield PlacesTestUtils.addVisits(Services.io.newURI("ftp://127.0.0.1:8080", null, null));
  yield PlacesTestUtils.addVisits(Services.io.newURI("https://localhost:8080", null, null));

  // Force initialization of the nsPermissionManager
  let enumerator = Services.perms.enumerator;
  while (enumerator.hasMoreElements()) {
    let permission = enumerator.getNext().QueryInterface(Ci.nsIPermission);
    let isExpected = false;

    expected.forEach((it, i) => {
      if (permission.principal.origin == it[0] &&
          permission.type == it[1] &&
          permission.capability == it[2] &&
          permission.expireType == it[3] &&
          permission.expireTime == it[4]) {
        isExpected = true;
        found[i]++;
      }
    });

    do_check_true(isExpected,
                  "Permission " + (isExpected ? "should" : "shouldn't") +
                  " be in permission database: " +
                  permission.principal.origin + ", " +
                  permission.type + ", " +
                  permission.capability + ", " +
                  permission.expireType + ", " +
                  permission.expireTime);
  }

  found.forEach((count, i) => {
    do_check_true(count == 1, "Expected count = 1, got count = " + count + " for permission " + expected[i]);
  });

  // Check to make sure that all of the tables which we care about are present
  {
    let db = Services.storage.openDatabase(GetPermissionsFile(profile));
    do_check_true(db.tableExists("moz_perms"));
    do_check_true(db.tableExists("moz_hosts"));
    do_check_false(db.tableExists("moz_hosts_is_backup"));
    do_check_false(db.tableExists("moz_perms_v6"));

    // The moz_hosts table should still exist but be empty
    let mozHostsCount = db.createStatement("SELECT count(*) FROM moz_hosts");
    mozHostsCount.executeStep();
    do_check_eq(mozHostsCount.getInt64(0), 0);

    // Check that there are the right number of values in the permissions database
    let mozPermsCount = db.createStatement("SELECT count(*) FROM moz_perms");
    mozPermsCount.executeStep();
    do_check_eq(mozPermsCount.getInt64(0), expected.length);

    db.close();
  }
});
