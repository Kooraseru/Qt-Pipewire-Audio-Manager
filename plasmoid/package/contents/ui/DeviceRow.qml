import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaAudioManager

Frame {
    id: row
    required property var device
    objectName: "endpoint_" + device.key
    property bool showNodeName: false
    padding: 10
    Layout.fillWidth: true
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Kirigami.Icon {
                source: row.device.icon
                implicitWidth: 22
                implicitHeight: 22
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: row.device.label
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.bold: true
                    ToolTip.visible: titleMouse.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: titleMouse }
                }
                Label {
                    text: row.device.state
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    opacity: 0.65
                }
                Label {
                    visible: row.showNodeName
                    text: row.device.nodeName
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    opacity: 0.5
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                objectName: "endpointMuteButton"
                icon.name: row.device.muted ? "audio-volume-muted" : (row.device.kind === "source" ? "audio-input-microphone" : "audio-volume-high")
                enabled: AudioBackend.connected && row.device.muteAvailable
                Accessible.name: row.device.muted ? qsTr("Unmute %1").arg(row.device.label) : qsTr("Mute %1").arg(row.device.label)
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: AudioBackend.setMute(row.device.key, !row.device.muted)
            }
            Slider {
                id: volumeSlider
                objectName: "endpointVolumeSlider"
                property bool dragging: false
                Layout.fillWidth: true
                enabled: AudioBackend.connected && row.device.volumeAvailable
                from: 0
                to: 100
                stepSize: 1
                Kirigami.StyleHints.tickMarkStepSize: -1
                value: row.device.volumePercent
                Accessible.name: qsTr("Volume for %1").arg(row.device.label)
                onPressedChanged: {
                    if (pressed) dragging = true
                    else if (dragging) {
                        dragging = false
                        AudioBackend.setVolume(row.device.key, Math.round(value))
                    }
                }
                onMoved: if (!pressed) AudioBackend.setVolume(row.device.key, Math.round(value))
            }
            Label {
                text: row.device.volumeAvailable ? Math.round(volumeSlider.pressed ? volumeSlider.value : row.device.volumePercent) + "%" : qsTr("Unavailable")
                opacity: row.device.volumeAvailable ? 1 : 0.5
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }
}
