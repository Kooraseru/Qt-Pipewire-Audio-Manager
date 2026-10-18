import QtQuick
import QtQuick.Controls
import "qrc:/ui" as AudioUI

ApplicationWindow {
    id: window
    visible: true
    title: qsTr("Plasma Audio Manager")
    width: 440
    height: 560
    minimumWidth: 380
    minimumHeight: 360
    AudioUI.AudioPopup { anchors.fill: parent }
}
