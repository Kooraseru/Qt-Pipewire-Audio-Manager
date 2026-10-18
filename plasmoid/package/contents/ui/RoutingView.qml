pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaAudioManager

ColumnLayout {
    id: root
    readonly property var sources: AudioBackend.routing.sources || []
    readonly property var destinations: AudioBackend.routing.destinations || []
    spacing: 8
    Label {
        Layout.fillWidth: true
        Layout.margins: 12
        text: qsTr("Add a source to one or more outputs. Locked routes are managed externally.")
        wrapMode: Text.WordWrap
        opacity: 0.7
        font.pointSize: Kirigami.Theme.smallFont.pointSize
    }
    ScrollView {
        id: scroll
        objectName: "routingScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: Math.max(availableWidth, 170 + root.destinations.length * 100)
        contentHeight: matrix.implicitHeight
        Column {
            id: matrix
            width: scroll.contentWidth
            Row {
                height: 60
                Label {
                    width: 170; height: 60
                    text: qsTr("Source / Output")
                    font.bold: true
                    leftPadding: 12
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: root.destinations
                    delegate: Label {
                        id: header
                        required property var modelData
                        width: 100; height: 60
                        padding: 5
                        text: modelData.label
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                        ToolTip.visible: headerHover.hovered
                        ToolTip.text: header.modelData.label
                        HoverHandler { id: headerHover }
                    }
                }
            }
            Repeater {
                model: AudioBackend.routingSourceKeys
                delegate: Row {
                    id: sourceRow
                    required property string modelData
                    readonly property var entry: root.sources.find(s => s.key === modelData) || ({ label: "", icon: "audio-card", cells: [] })
                    height: 48
                    Item {
                        width: 170; height: 48
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Kirigami.Theme.textColor; opacity: 0.08 }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            Kirigami.Icon { source: sourceRow.entry.icon; implicitWidth: 18; implicitHeight: 18 }
                            Label {
                                text: sourceRow.entry.label
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                ToolTip.visible: sourceHover.hovered
                                ToolTip.text: text
                                HoverHandler { id: sourceHover }
                            }
                        }
                    }
                    Repeater {
                        model: AudioBackend.routingDestinationKeys
                        delegate: Item {
                            id: cell
                            required property string modelData
                            readonly property var route: sourceRow.entry.cells.find(c => c.destination === modelData) || ({ source: sourceRow.modelData, destination: modelData, destinationLabel: "", checked: false, editable: false, state: "Off", reason: "" })
                            width: 100; height: 48
                            objectName: "routeCell_" + sourceRow.modelData + "_" + modelData
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Kirigami.Theme.textColor; opacity: 0.08 }
                            CheckBox {
                                objectName: "routeCheckBox"
                                anchors.centerIn: parent
                                checked: cell.route.checked
                                enabled: AudioBackend.connected && cell.route.editable
                                Accessible.name: qsTr("%1 to %2").arg(sourceRow.entry.label).arg(cell.route.destinationLabel)
                                onClicked: AudioBackend.setRoute(cell.route.source, cell.route.destination, !cell.route.checked)
                            }
                            Kirigami.Icon {
                                visible: cell.route.state === "External" || cell.route.state === "Failed"
                                source: cell.route.state === "Failed" ? "dialog-warning" : "object-locked"
                                width: 12; height: 12
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                opacity: 0.6
                            }
                            ToolTip.visible: cellHover.hovered
                            ToolTip.text: cell.route.reason || cell.route.state
                            HoverHandler { id: cellHover }
                        }
                    }
                }
            }
            Label {
                visible: (AudioBackend.routing.savedRoutes || []).length > 0
                width: scroll.availableWidth
                padding: 12
                text: qsTr("Saved routes")
                font.bold: true
            }
            Repeater {
                model: AudioBackend.routing.savedRoutes || []
                delegate: RowLayout {
                    id: saved
                    required property var modelData
                    width: scroll.availableWidth
                    spacing: 6
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Label { text: saved.modelData.sourceLabel + " → " + saved.modelData.destinationLabel; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: saved.modelData.state + (saved.modelData.reason ? " · " + saved.modelData.reason : "")
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            opacity: 0.65
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }
                    ToolButton {
                        icon.name: "edit-delete"
                        objectName: "forgetRoute_" + saved.modelData.id
                        Accessible.name: qsTr("Forget route from %1 to %2").arg(saved.modelData.sourceLabel).arg(saved.modelData.destinationLabel)
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: AudioBackend.forgetRoute(saved.modelData.id)
                    }
                }
            }
            Label {
                visible: (((AudioBackend.routing.recording || {}).assignments) || []).length > 0
                width: scroll.availableWidth
                padding: 12
                text: qsTr("Saved recording inputs")
                font.bold: true
            }
            Repeater {
                model: (AudioBackend.routing.recording || {}).assignments || []
                delegate: RowLayout {
                    id: inputRule
                    required property var modelData
                    width: scroll.availableWidth
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Label { text: inputRule.modelData.applicationLabel + " ← " + inputRule.modelData.sourceLabel; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: inputRule.modelData.state + (inputRule.modelData.reason ? " · " + inputRule.modelData.reason : ""); wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.65 }
                    }
                    ToolButton {
                        icon.name: "edit-delete"
                        Accessible.name: qsTr("Forget recording input for %1").arg(inputRule.modelData.applicationLabel)
                        onClicked: AudioBackend.forgetRecordingAssignment(inputRule.modelData.id)
                    }
                }
            }
        }
    }
    Label {
        visible: root.sources.length === 0 || root.destinations.length === 0
        text: qsTr("Connect an audio source and an output to start routing.")
        Layout.fillWidth: true
        Layout.margins: 12
        wrapMode: Text.WordWrap
        opacity: 0.7
    }
    Label {
        text: qsTr("Microphone monitoring can cause acoustic feedback. Routes are saved across stream and backend restarts. Independent route gain is not available yet.")
        Layout.fillWidth: true
        Layout.leftMargin: 12; Layout.rightMargin: 12
        wrapMode: Text.WordWrap
        opacity: 0.6
        font.pointSize: Kirigami.Theme.smallFont.pointSize
    }
    Button {
        objectName: "clearCustomRoutes"
        text: qsTr("Forget all custom routes")
        icon.name: "edit-clear"
        enabled: AudioBackend.connected && (AudioBackend.routing.savedCount || 0) > 0
        Layout.alignment: Qt.AlignRight
        Layout.rightMargin: 12; Layout.bottomMargin: 12
        onClicked: AudioBackend.clearRoutes()
    }
}
