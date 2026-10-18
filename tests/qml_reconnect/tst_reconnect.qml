import QtQuick
import QtTest
import PlasmaAudioManager

TestCase {
    name: "BackendReconnection"

    function test_restart() {
        tryCompare(AudioBackend, "connected", true, 5000)
        verify(AudioBackend.virtualDevices.length > 0)
        console.log("PAM_GUI_CONNECTED")
        tryCompare(AudioBackend, "connected", false, 10000)
        compare(AudioBackend.outputs.length, 0)
        compare(AudioBackend.inputs.length, 0)
        compare(AudioBackend.virtualDevices.length, 0)
        compare(AudioBackend.applications.length, 0)
        console.log("PAM_GUI_OFFLINE")
        tryCompare(AudioBackend, "connected", true, 10000)
        verify(AudioBackend.virtualDevices.length > 0)
    }
}
