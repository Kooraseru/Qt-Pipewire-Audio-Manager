pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Frame {
    id: row
    required property var device
    objectName: "cableRow_" + device.key
    property bool canDelete: false
    property bool expanded: false
    signal toggleRequested()
    signal deleteRequested(var device)
    Layout.fillWidth: true
    padding: 10
    ColumnLayout {
        anchors.fill: parent
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Kirigami.Icon { source: row.device.icon; implicitWidth: 22; implicitHeight: 22 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    text: row.device.label
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            Kirigami.Icon {
                visible: !row.device.owned
                source: "object-locked"
                implicitWidth: 14
                implicitHeight: 14
                opacity: 0.5
                Accessible.name: qsTr("Lifecycle managed externally")
                ToolTip.visible: lockHover.hovered
                ToolTip.text: Accessible.name
                HoverHandler { id: lockHover }
            }
            ToolButton {
                objectName: row.device.owned ? "ownedCableActions" : "externalCableActions"
                icon.name: "overflow-menu"
                Accessible.name: qsTr("Actions for %1").arg(row.device.label)
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: actions.open()
                Menu {
                    id: actions
                    MenuItem {
                        text: row.expanded ? qsTr("Hide endpoints") : qsTr("Show endpoints")
                        icon.name: row.expanded ? "arrow-up" : "arrow-down"
                        onTriggered: row.toggleRequested()
                    }
                    MenuItem {
                        objectName: row.device.owned ? "deleteOwnedDeviceButton" : "externalDeleteItem"
                        visible: row.device.owned
                        enabled: row.canDelete
                        text: qsTr("Delete device")
                        icon.name: "edit-delete"
                        onTriggered: row.deleteRequested(row.device)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: row.device.kind === "cable" ? qsTr("Output to input") : row.device.state
                opacity: 0.6
                Layout.fillWidth: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
            ToolButton {
                objectName: "toggleCableEndpoints"
                text: qsTr("Endpoints")
                icon.name: row.expanded ? "arrow-up" : "arrow-down"
                Accessible.name: qsTr("Show endpoints for %1").arg(row.device.label)
                onClicked: row.toggleRequested()
            }
        }
        ColumnLayout {
            visible: row.expanded
            Layout.fillWidth: true
            Repeater {
                model: row.device.endpoints
                delegate: DeviceRow {
                    required property var modelData
                    device: modelData
                    showNodeName: true
                }
            }
        }
    }
}
