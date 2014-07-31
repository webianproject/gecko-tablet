/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function test() {
  info("Test that the split console state is persisted");

  let toolbox;
  let TEST_URI = "data:text/html;charset=utf-8,<p>Web Console test for splitting</p>";

  Task.spawn(runner).then(finish);

  function* runner() {
    info("Opening a tab while there is no user setting on split console pref");
    let {tab} = yield loadTab(TEST_URI);
    let target = TargetFactory.forTab(tab);
    toolbox = yield gDevTools.showToolbox(target, "inspector");

    ok(!toolbox.splitConsole, "Split console is hidden by default.");
    yield toggleSplitConsoleWithEscape();
    ok(toolbox.splitConsole, "Split console is now visible.");
    ok(getVisiblePrefValue(), "Visibility pref is true");

    is(getHeightPrefValue(), toolbox.webconsolePanel.height, "Panel height matches the pref");
    toolbox.webconsolePanel.height = 200;

    yield toolbox.destroy();

    info("Opening a tab while there is a true user setting on split console pref");
    let {tab} = yield loadTab(TEST_URI);
    let target = TargetFactory.forTab(tab);
    toolbox = yield gDevTools.showToolbox(target, "inspector");

    ok(toolbox.splitConsole, "Split console is visible by default.");
    is(getHeightPrefValue(), 200, "Height is set based on panel height after closing");

    toolbox.webconsolePanel.height = 1;
    ok (toolbox.webconsolePanel.clientHeight > 1,
        "The actual height of the console is bound with a min height");

    toolbox.webconsolePanel.height = 10000;
    ok (toolbox.webconsolePanel.clientHeight < 10000,
        "The actual height of the console is bound with a max height");

    yield toggleSplitConsoleWithEscape();
    ok(!toolbox.splitConsole, "Split console is now hidden.");
    ok(!getVisiblePrefValue(), "Visibility pref is false");

    yield toolbox.destroy();

    is(getHeightPrefValue(), 10000, "Height is set based on panel height after closing");


    info("Opening a tab while there is a false user setting on split console pref");
    let {tab} = yield loadTab(TEST_URI);
    let target = TargetFactory.forTab(tab);
    toolbox = yield gDevTools.showToolbox(target, "inspector");

    ok(!toolbox.splitConsole, "Split console is hidden by default.");
    ok(!getVisiblePrefValue(), "Visibility pref is false");

    yield toolbox.destroy();
  }

  function getVisiblePrefValue() {
    return Services.prefs.getBoolPref("devtools.toolbox.splitconsoleEnabled");
  }

  function getHeightPrefValue() {
    return Services.prefs.getIntPref("devtools.toolbox.splitconsoleHeight");
  }

  function toggleSplitConsoleWithEscape() {
    let onceSplitConsole = toolbox.once("split-console");
    let contentWindow = toolbox.frame.contentWindow;
    contentWindow.focus();
    EventUtils.sendKey("ESCAPE", contentWindow);
    return onceSplitConsole;
  }

  function finish() {
    toolbox = TEST_URI = null;
    Services.prefs.clearUserPref("devtools.toolbox.splitconsoleEnabled");
    Services.prefs.clearUserPref("devtools.toolbox.splitconsoleHeight");
    finishTest();
  }
}
