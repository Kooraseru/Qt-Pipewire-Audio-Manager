pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaAudioManager

Frame {
    id: row
    objectName: "appRow_" + stream.key
    required property var stream
    property bool expanded: false
    signal toggleRequested()
    signal matrixRequested()
    readonly property var routeSource: (AudioBackend.routing.sources || []).find(s => s.key === row.stream.key)
    readonly property var inputEntry: ((AudioBackend.routing.recording || {}).streams || []).find(s => s.key === row.stream.key)
    readonly property var inputOptions: [{key: "", label: qsTr("Use default input (forget assignment)")}]
        .concat(((AudioBackend.routing.recording || {}).sources || []))
        .concat(inputEntry && inputEntry.assignment && !inputEntry.selected ? [{key: "__missing__", label: qsTr("Saved input unavailable")}]: [])
    Layout.fillWidth: true
    padding: 10
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Kirigami.Icon { source: row.stream.icon; implicitWidth: 22; implicitHeight: 22 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { text: row.stream.label; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Label {
                    text: row.stream.direction + " · " + row.stream.state
                    opacity: 0.65
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }
            }
            ToolButton {
                objectName: "appRoutesToggle"
                text: qsTr("Routes")
                icon.name: row.expanded ? "arrow-up" : "arrow-down"
                onClicked: row.toggleRequested()
                Accessible.name: qsTr("Show routes for %1").arg(row.stream.label)
            }
        }
        ColumnLayout {
            visible: row.expanded
            Layout.fillWidth: true
            Label {
                visible: row.stream.direction === "Playback"
                text: qsTr("Current destinations")
                font.bold: true
            }
            Label {
                visible: row.stream.direction === "Recording"
                text: qsTr("Input source")
                font.bold: true
            }
            ComboBox {
                objectName: "recordingSourceCombo"
                visible: row.stream.direction === "Recording"
                Layout.fillWidth: true
                model: row.inputOptions
                textRole: "label"
                valueRole: "key"
                enabled: AudioBackend.connected && !!row.inputEntry && row.inputEntry.editable
                currentIndex: Math.max(0, row.inputOptions.findIndex(o => o.key === (row.inputEntry && row.inputEntry.assignment ? (row.inputEntry.selected || "__missing__") : "")))
                onActivated: function(index) {
                    const key = row.inputOptions[index].key
                    if (key !== "__missing__") AudioBackend.setRecordingSource(row.stream.key, key)
                }
                Accessible.name: qsTr("Recording source for %1").arg(row.stream.label)
            }
            Label {
                visible: row.stream.direction === "Recording"
                text: row.inputEntry ? row.inputEntry.state + (row.inputEntry.reason ? " · " + row.inputEntry.reason : "") : qsTr("Waiting for recording controls")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.65
            }
            Label {
                visible: row.stream.direction === "Recording"
                text: qsTr("Current sources")
                font.bold: true
            }
            Repeater {
                model: row.routeSource ? [] : row.stream.routes
                delegate: RowLayout {
                    id: routeEntry
                    required property string modelData
                    Layout.fillWidth: true
                    Kirigami.Icon { source: "insert-link"; implicitWidth: 16; implicitHeight: 16 }
                    Label { text: routeEntry.modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
            }
            Repeater {
                model: row.routeSource ? row.routeSource.cells : []
                delegate: CheckDelegate {
                    id: destination
                    objectName: "appDestination_" + modelData.destination
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.destinationLabel
                    checked: modelData.checked
                    enabled: modelData.editable
                    onClicked: AudioBackend.setRoute(modelData.source, modelData.destination, !modelData.checked)
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.reason || modelData.state
                }
            }
            Label { visible: !row.routeSource && row.stream.routes.length === 0; text: qsTr("No active links"); opacity: 0.7 }
            Button {
                visible: !!row.routeSource
                text: qsTr("Open routing matrix")
                icon.name: "view-grid"
                onClicked: row.matrixRequested()
            }
            Label {
                text: row.stream.direction === "Recording" ? qsTr("Input changes are handled by WirePlumber and do not change microphone mute states.") : qsTr("Existing external routes are locked. Independent route gain is not available yet.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.6
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }
    }
}
