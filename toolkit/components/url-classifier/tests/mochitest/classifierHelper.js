if (typeof(classifierHelper) == "undefined") {
  var classifierHelper = {};
}

const CLASSIFIER_COMMON_URL = SimpleTest.getTestFileURL("classifierCommon.js");
var gScript = SpecialPowers.loadChromeScript(CLASSIFIER_COMMON_URL);

const ADD_CHUNKNUM = 524;
const SUB_CHUNKNUM = 523;
const HASHLEN = 32;

// addUrlToDB & removeUrlFromDB are asynchronous, queue the task to ensure
// the callback follow correct order.
classifierHelper._updates = [];

// Keep urls added to database, those urls should be automatically
// removed after test complete.
classifierHelper._updatesToCleanup = [];

// Pass { url: ..., db: ... } to add url to database,
// onsuccess/onerror will be called when update complete.
classifierHelper.addUrlToDB = function(updateData) {
  return new Promise(function(resolve, reject) {
    var testUpdate = "";
    for (var update of updateData) {
      var LISTNAME = update.db;
      var CHUNKDATA = update.url;
      var CHUNKLEN = CHUNKDATA.length;

      classifierHelper._updatesToCleanup.push(update);
      testUpdate +=
        "n:1000\n" +
        "i:" + LISTNAME + "\n" +
        "ad:1\n" +
        "a:" + ADD_CHUNKNUM + ":" + HASHLEN + ":" + CHUNKLEN + "\n" +
        CHUNKDATA;
    }

    classifierHelper._update(testUpdate, resolve, reject);
  });
}

// Pass { url: ..., db: ... } to remove url from database,
// onsuccess/onerror will be called when update complete.
classifierHelper.removeUrlFromDB = function(updateData) {
  return new Promise(function(resolve, reject) {
    var testUpdate = "";
    for (var update of updateData) {
      var LISTNAME = update.db;
      var CHUNKDATA = ADD_CHUNKNUM + ":" + update.url;
      var CHUNKLEN = CHUNKDATA.length;

      testUpdate +=
        "n:1000\n" +
        "i:" + LISTNAME + "\n" +
        "s:" + SUB_CHUNKNUM + ":" + HASHLEN + ":" + CHUNKLEN + "\n" +
        CHUNKDATA;
    }

    classifierHelper._updatesToCleanup =
      classifierHelper._updatesToCleanup.filter((v) => {
        return updateData.indexOf(v) == -1;
      });

    classifierHelper._update(testUpdate, resolve, reject);
  });
};

// This API is used to expire all add/sub chunks we have updated
// by using addUrlToDB and removeUrlFromDB.
classifierHelper.resetDB = function() {
  return new Promise(function(resolve, reject) {
    var testUpdate = "";
    for (var update of classifierHelper._updatesToCleanup) {
      if (testUpdate.includes(update.db))
        continue;

      testUpdate +=
        "n:1000\n" +
        "i:" + update.db + "\n" +
        "ad:" + ADD_CHUNKNUM + "\n" +
        "sd:" + SUB_CHUNKNUM + "\n"
    }

    classifierHelper._update(testUpdate, resolve, reject);
  });
};

classifierHelper._update = function(testUpdate, onsuccess, onerror) {
  // Queue the task if there is still an on-going update
  classifierHelper._updates.push({"data": testUpdate,
                                  "onsuccess": onsuccess,
                                  "onerror": onerror});
  if (classifierHelper._updates.length != 1) {
    return;
  }

  gScript.sendAsyncMessage("doUpdate", { testUpdate });
};

classifierHelper._updateSuccess = function() {
  var update = classifierHelper._updates.shift();
  update.onsuccess();

  if (classifierHelper._updates.length) {
    var testUpdate = classifierHelper._updates[0].data;
    gScript.sendAsyncMessage("doUpdate", { testUpdate });
  }
};

classifierHelper._updateError = function(errorCode) {
  var update = classifierHelper._updates.shift();
  update.onerror(errorCode);

  if (classifierHelper._updates.length) {
    var testUpdate = classifierHelper._updates[0].data;
    gScript.sendAsyncMessage("doUpdate", { testUpdate });
  }
};

classifierHelper._setup = function() {
  gScript.addMessageListener("updateSuccess", classifierHelper._updateSuccess);
  gScript.addMessageListener("updateError", classifierHelper._updateError);

  // cleanup will be called at end of each testcase to remove all the urls added to database.
  SimpleTest.registerCleanupFunction(classifierHelper._cleanup);
};

classifierHelper._cleanup = function() {
  if (!classifierHelper._updatesToCleanup) {
    return Promise.resolve();
  }

  return classifierHelper.resetDB();
};

classifierHelper._setup();
