import QtQuick
import QtQuick.Controls
import QtTest
import PlasmaAudioManager
import "../../plasmoid/package/contents/ui" as AudioUI

TestCase {
    id: test
    name: "AudioPopupIntegration"
    width: 440
    height: 560
    visible: true
    when: windowShown

    AudioUI.AudioPopup { id: popup; anchors.fill: parent }

    function initTestCase() {
        failOnWarning(/.*/)
        tryCompare(AudioBackend, "connected", true, 5000)
    }

    function test_01_tabsAndExternalProtection() {
        compare(popup.currentTab, 0)
        mouseClick(findChild(popup, "cablesTab"))
        compare(popup.currentTab, 1)
        verify(popup.externalDevices.length > 0)
        const pairs = popup.externalDevices.filter(d => d.kind === "cable")
        compare(pairs.length, 3)
        for (const pair of pairs) {
            compare(pair.endpoints.length, 2)
            verify(!pair.owned)
            compare(pair.endpoints[0].kind, "sink")
            compare(pair.endpoints[1].kind, "source")
        }
        const before = AudioBackend.virtualDevices.length
        AudioBackend.destroyDevice(popup.externalDevices[0].key)
        verify(AudioBackend.error.length > 0)
        compare(AudioBackend.virtualDevices.length, before)
        AudioBackend.clearError()
        mouseClick(findChild(popup, "applicationsTab"))
        compare(popup.currentTab, 2)
        compare(AudioBackend.applications.length, 0)
    }

    function test_02_createAndDeleteThroughDialogs() {
        mouseClick(findChild(popup, "cablesTab"))
        const createButton = findChild(popup, "createDeviceButton")
        verify(createButton.enabled)
        mouseClick(createButton)
        const dialog = findChild(popup, "createDeviceDialog")
        tryCompare(dialog, "opened", true)
        const confirm = findChild(popup, "confirmCreateButton")
        verify(!confirm.enabled)
        findChild(popup, "deviceNameField").text = "GUI integration cable"
        verify(confirm.enabled)
        mouseClick(confirm)
        tryVerify(() => popup.ownedDevices.length === 1, 5000)
        tryCompare(dialog, "opened", false)
        tryCompare(dialog, "visible", false)
        compare(popup.ownedDevices[0].label, "GUI integration cable")
        compare(popup.ownedDevices[0].kind, "cable")
        tryVerify(() => popup.ownedDevices[0].state === "Saved", 5000)
        const token = popup.ownedDevices[0].key
        const toggle = findChild(findChild(popup, "cableRow_" + token), "toggleCableEndpoints")
        mouseClick(toggle)
        tryVerify(() => popup.expandedCables[popup.ownedDevices[0].key] === true)
        tryVerify(() => popup.ownedDevices[0].endpoints.every(e => e.volumeAvailable && e.muteAvailable), 5000)
        const serial = popup.ownedDevices[0].endpoints[0].key
        const muteButton = findChild(findChild(popup, "endpoint_" + serial), "endpointMuteButton")
        verify(muteButton.enabled)
        verify(muteButton.visible)
        waitForRendering(muteButton)
        mouseClick(muteButton)
        tryVerify(() => popup.ownedDevices[0].endpoints[0].muted, 5000)
        const slider = findChild(findChild(popup, "endpoint_" + serial), "endpointVolumeSlider")
        verify(slider.enabled)
        slider.forceActiveFocus()
        keyClick(Qt.Key_Left)
        tryVerify(() => popup.ownedDevices[0].endpoints[0].volumePercent < 100, 5000)
        AudioBackend.setVolume(serial, 50)
        tryVerify(() => popup.ownedDevices[0].endpoints[0].volumePercent === 50, 5000)
        verify(popup.expandedCables[popup.ownedDevices[0].key])
        // Collapse before opening the menu so the action stays in the viewport.
        mouseClick(findChild(findChild(popup, "cableRow_" + token), "toggleCableEndpoints"))
        mouseClick(findChild(findChild(popup, "cableRow_" + token), "ownedCableActions"))
        const removeButton = findChild(popup.Window.window.contentItem, "deleteOwnedDeviceButton")
        verify(removeButton.enabled)
        verify(removeButton.visible)
        waitForRendering(removeButton)
        mouseClick(removeButton)
        const removeDialog = findChild(popup, "removeDeviceDialog")
        tryCompare(removeDialog, "opened", true)
        mouseClick(findChild(popup, "confirmDeleteButton"))
        tryVerify(() => popup.ownedDevices.length === 0, 5000)
        tryCompare(removeDialog, "visible", false)
        compare(AudioBackend.error, "")
    }

    function test_03_resizeAndCollapse() {
        test.width = 380
        test.height = 360
        mouseClick(findChild(popup, "devicesTab"))
        compare(popup.currentTab, 0)
        waitForRendering(popup)
        const category = findChild(popup, "outputCategory")
        mouseClick(category)
        compare(category.expanded, false)
        mouseClick(category)
        compare(category.expanded, true)
        test.width = 440
        test.height = 560
    }
}
