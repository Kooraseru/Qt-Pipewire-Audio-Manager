import QtQuick
import QtQuick.Controls
import QtTest
import PlasmaAudioManager
import "../../plasmoid/package/contents/ui" as AudioUI

TestCase {
    id: test
    name: "RecordingSourceSelection"
    width: 500
    height: 1000
    visible: true
    when: windowShown
    AudioUI.AudioPopup { id: popup; anchors.fill: parent }
    function stream() { return AudioBackend.applications.find(a => a.label === "recording-app") }
    function entry() { return AudioBackend.routing.recording.streams.find(s => s.key === stream().key) }
    function row() { return findChild(popup, "appRow_" + stream().key) }
    function selectSource(label) {
        const combo = findChild(row(), "recordingSourceCombo")
        verify(combo.visible && combo.enabled)
        const target = combo.model.findIndex(o => o.label === label)
        verify(target >= 0)
        const key = combo.model[target].key
        combo.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(combo.popup, "visible", true)
        const option = combo.popup.itemAt(target)
        verify(option !== null)
        waitForRendering(option)
        mouseClick(option)
        tryVerify(() => entry().selected === key && entry().state === "Connected", 5000)
    }
    function initTestCase() {
        failOnWarning(/.*/)
        tryCompare(AudioBackend, "connected", true, 5000)
        tryVerify(() => stream() !== undefined, 5000)
    }
    function test_inputSelection() {
        mouseClick(findChild(popup, "applicationsTab"))
        const toggle = findChild(row(), "appRoutesToggle")
        verify(toggle.visible)
        waitForRendering(toggle)
        mouseClick(toggle)
        tryVerify(() => row().expanded)
        selectSource("Cable B Input")
        verify(row().expanded)
        selectSource("Cable A Input")
        compare(AudioBackend.error, "")
    }
    function cleanupTestCase() { mouseMove(popup, 1, 1); wait(300) }
}
