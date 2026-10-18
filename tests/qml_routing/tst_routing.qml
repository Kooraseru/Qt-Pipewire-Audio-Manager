import QtQuick
import QtQuick.Controls
import QtTest
import PlasmaAudioManager
import "../../plasmoid/package/contents/ui" as AudioUI

TestCase {
    id: test
    name: "RoutingMatrixIntegration"
    width: 1400
    height: 900
    visible: true
    when: windowShown
    AudioUI.AudioPopup { id: popup; anchors.fill: parent }
    function source(name) { return (AudioBackend.routing.sources || []).find(s => s.label === name) }
    function destination(name) { return (AudioBackend.routing.destinations || []).find(s => s.label === name) }
    function cell(a, b) { return source(a).cells.find(c => c.destination === destination(b).key) }
    function checkbox(a, b) {
        return findChild(findChild(popup, "routeCell_" + source(a).key + "_" + destination(b).key), "routeCheckBox")
    }
    function initTestCase() {
        failOnWarning(/.*/)
        tryCompare(AudioBackend, "connected", true, 5000)
        tryVerify(() => source("test-app") !== undefined, 5000)
    }
    function cleanupTestCase() {
        // Let native Breeze hover/check animations finish before qmltestrunner
        // tears down its QApplication and global widget style.
        mouseMove(popup, 1, 1)
        wait(300)
    }
    function test_routes() {
        mouseClick(findChild(popup, "routingTab"))
        compare(popup.currentTab, 3)
        const external = checkbox("test-app", "test-output-a")
        verify(external.checked)
        verify(!external.enabled)
        const custom = checkbox("test-app", "test-output-b")
        verify(custom.enabled)
        verify(!custom.checked)
        waitForRendering(custom)
        mouseClick(custom)
        tryVerify(() => cell("test-app", "test-output-b").state === "Connected", 5000)
        verify(checkbox("test-app", "test-output-b").checked)
        verify(!checkbox("test-cable-input", "test-cable-output").enabled)
        verify(!checkbox("test-app", "test-output-mono").enabled)
        waitForRendering(checkbox("test-app", "test-output-b"))
        mouseClick(checkbox("test-app", "test-output-b"))
        tryVerify(() => cell("test-app", "test-output-b").state === "Off", 5000)
        mouseClick(findChild(popup, "applicationsTab"))
        const app = findChild(popup, "appRow_" + source("test-app").key)
        verify(app !== null)
        mouseClick(findChild(app, "appRoutesToggle"))
        tryVerify(() => app.expanded)
        const appDestination = findChild(app, "appDestination_" + destination("test-output-b").key)
        verify(appDestination.enabled)
        waitForRendering(appDestination)
        mouseClick(appDestination)
        tryVerify(() => cell("test-app", "test-output-b").state === "Connected", 5000)
        verify(findChild(popup, "appRow_" + source("test-app").key).expanded)
        mouseClick(findChild(popup, "routingTab"))
        mouseClick(findChild(popup, "clearCustomRoutes"))
        tryVerify(() => AudioBackend.routing.ownedCount === 0, 5000)
        verify(checkbox("test-app", "test-output-a").checked)
        test.width = 380
        test.height = 360
        waitForRendering(popup)
        verify(findChild(popup, "routingScroll").contentWidth > 380)
        compare(AudioBackend.error, "")
    }
}
