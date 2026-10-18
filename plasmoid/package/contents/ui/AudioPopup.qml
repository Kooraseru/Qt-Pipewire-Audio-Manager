pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import PlasmaAudioManager

Pane {
    id: root
    objectName: "audioPopup"
    implicitWidth: 440
    implicitHeight: 560
    padding: 0
    property alias currentTab: tabs.currentIndex
    property var expandedCables: ({})
    property var expandedApps: ({})
    function toggleApp(key) {
        const updated = Object.assign({}, expandedApps)
        updated[key] = !updated[key]
        expandedApps = updated
    }
    function toggleCable(key) {
        const updated = Object.assign({}, expandedCables)
        updated[key] = !updated[key]
        expandedCables = updated
    }
    readonly property var ownedDevices: AudioBackend.virtualDevices.filter(d => d.owned)
    readonly property var externalDevices: AudioBackend.virtualDevices.filter(d => !d.owned)
    readonly property var playback: AudioBackend.applications.filter(d => d.direction === "Playback")
    readonly property var recording: AudioBackend.applications.filter(d => d.direction === "Recording")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10
            Kirigami.Icon { source: "audio-card"; implicitWidth: 24; implicitHeight: 24 }
            Label { text: qsTr("Audio Manager"); font.bold: true; font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2; Layout.fillWidth: true }
            BusyIndicator { running: AudioBackend.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
            ToolButton {
                objectName: "refreshButton"
                icon.name: "view-refresh"
                enabled: AudioBackend.connected && !AudioBackend.busy
                Accessible.name: qsTr("Refresh audio devices")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: AudioBackend.refresh()
            }
        }
        TabBar {
            id: tabs
            objectName: "audioTabs"
            Layout.fillWidth: true
            TabButton { objectName: "devicesTab"; text: qsTr("Devices"); icon.name: "audio-speakers" }
            TabButton { objectName: "cablesTab"; text: qsTr("Cables"); icon.name: "insert-link" }
            TabButton { objectName: "applicationsTab"; text: qsTr("Applications"); icon.name: "applications-multimedia" }
            TabButton { objectName: "routingTab"; text: qsTr("Routing"); icon.name: "view-grid" }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.palette.mid; opacity: 0.5 }
        Frame {
            visible: AudioBackend.error.length > 0
            Layout.fillWidth: true
            Layout.margins: 10
            RowLayout {
                anchors.fill: parent
                Kirigami.Icon { source: "dialog-error"; implicitWidth: 20; implicitHeight: 20 }
                Label { text: AudioBackend.error; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                ToolButton {
                    icon.name: "dialog-close"
                    Accessible.name: qsTr("Dismiss error")
                    onClicked: AudioBackend.clearError()
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: !AudioBackend.connected ? 4 : tabs.currentIndex
            ScrollView {
                id: deviceScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: deviceScroll.availableWidth
                    spacing: 6
                    SectionHeading { id: outputHeading; objectName: "outputCategory"; title: qsTr("Output devices"); count: AudioBackend.outputs.length; Layout.margins: 6 }
                    ColumnLayout {
                        visible: outputHeading.expanded
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Repeater { model: AudioBackend.outputs; delegate: DeviceRow { required property var modelData; device: modelData } }
                        Label { visible: AudioBackend.outputs.length === 0; text: qsTr("No physical outputs connected"); opacity: 0.6; Layout.bottomMargin: 10 }
                    }
                    SectionHeading { id: inputHeading; title: qsTr("Input devices"); count: AudioBackend.inputs.length; Layout.margins: 6 }
                    ColumnLayout {
                        visible: inputHeading.expanded
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Repeater { model: AudioBackend.inputs; delegate: DeviceRow { required property var modelData; device: modelData } }
                        Label { visible: AudioBackend.inputs.length === 0; text: qsTr("No physical inputs connected"); opacity: 0.6; Layout.bottomMargin: 10 }
                    }
                    Label {
                        text: qsTr("Idle devices are ready to use. Default-device selection is not available yet.")
                        Layout.fillWidth: true
                        Layout.margins: 14
                        wrapMode: Text.WordWrap
                        opacity: 0.6
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                    }
                }
            }
            ColumnLayout {
                spacing: 0
                ScrollView {
                    id: cableScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: cableScroll.availableWidth
                        spacing: 6
                        SectionHeading { id: ownedHeading; title: qsTr("Created here"); count: root.ownedDevices.length; visible: count > 0; Layout.margins: 6 }
                        ColumnLayout {
                            visible: ownedHeading.expanded && (root.ownedDevices.length > 0 || root.externalDevices.length === 0)
                            Layout.fillWidth: true
                            Layout.leftMargin: 12; Layout.rightMargin: 12
                            Repeater {
                                model: root.ownedDevices
                                delegate: CableRow {
                                    required property var modelData
                                    device: modelData
                                    expanded: !!root.expandedCables[modelData.key]
                                    onToggleRequested: root.toggleCable(modelData.key)
                                    canDelete: AudioBackend.connected && !AudioBackend.busy
                                    onDeleteRequested: device => { removeDialog.device = device; removeDialog.open() }
                                }
                            }
                            Label { visible: root.ownedDevices.length === 0; text: qsTr("No saved devices created"); opacity: 0.6; Layout.bottomMargin: 10 }
                        }
                        SectionHeading {
                            id: externalHeading
                            title: root.externalDevices.every(d => d.kind === "cable") ? qsTr("Virtual cables") : qsTr("Managed externally")
                            count: root.externalDevices.length
                            visible: count > 0
                            Layout.margins: 6
                        }
                        ColumnLayout {
                            visible: externalHeading.expanded && root.externalDevices.length > 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 12; Layout.rightMargin: 12
                            Repeater {
                                model: root.externalDevices
                                delegate: CableRow {
                                    required property var modelData
                                    device: modelData
                                    expanded: !!root.expandedCables[modelData.key]
                                    onToggleRequested: root.toggleCable(modelData.key)
                                }
                            }
                            Label { visible: root.externalDevices.length === 0; text: qsTr("No external virtual endpoints"); opacity: 0.6; Layout.bottomMargin: 10 }
                        }
                    }
                }
                Label {
                    text: qsTr("Created devices are saved and recreated at login or backend restart.")
                    Layout.fillWidth: true
                    Layout.margins: 12
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }
                Button {
                    objectName: "createDeviceButton"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 12
                    text: qsTr("Create virtual device")
                    icon.name: "list-add"
                    enabled: AudioBackend.connected && !AudioBackend.busy
                    onClicked: createDialog.open()
                }
            }
            ScrollView {
                id: appScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: appScroll.availableWidth
                    spacing: 6
                    EmptyState {
                        visible: AudioBackend.applications.length === 0
                        Layout.margins: 20
                        iconName: "applications-multimedia"
                        title: qsTr("No active application streams")
                        description: qsTr("Play audio or start recording in an application to see its streams here.")
                    }
                    SectionHeading { id: playbackHeading; title: qsTr("Playback"); count: root.playback.length; visible: count > 0; Layout.margins: 6 }
                    ColumnLayout {
                        visible: playbackHeading.expanded
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Repeater {
                            model: root.playback
                            delegate: AppRow {
                                required property var modelData
                                stream: modelData
                                expanded: !!root.expandedApps[modelData.key]
                                onToggleRequested: root.toggleApp(modelData.key)
                                onMatrixRequested: root.currentTab = 3
                            }
                        }
                    }
                    SectionHeading { id: recordingHeading; title: qsTr("Recording"); count: root.recording.length; visible: count > 0; Layout.margins: 6 }
                    ColumnLayout {
                        visible: recordingHeading.expanded
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 12
                        Repeater {
                            model: root.recording
                            delegate: AppRow {
                                required property var modelData
                                stream: modelData
                                expanded: !!root.expandedApps[modelData.key]
                                onToggleRequested: root.toggleApp(modelData.key)
                            }
                        }
                    }
                }
            }
            RoutingView { }
            Item {
                EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    iconName: "network-disconnect"
                    title: AudioBackend.status
                    description: qsTr("Start the audio backend to see your devices and streams. This window will connect automatically.")
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.palette.mid; opacity: 0.5 }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 7
            Kirigami.Icon {
                source: AudioBackend.connected ? "network-connect" : "network-disconnect"
                implicitWidth: 16; implicitHeight: 16
            }
            Label { text: AudioBackend.status; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true; elide: Text.ElideRight }
        }
    }

    Dialog {
        id: createDialog
        objectName: "createDeviceDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, root.width - 24)
        modal: true
        title: qsTr("Create virtual device")
        closePolicy: Popup.CloseOnEscape
        onOpened: { nameField.text = ""; nameField.forceActiveFocus() }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label { text: qsTr("Device type"); font.bold: true }
            ComboBox {
                id: typeField
                objectName: "deviceTypeField"
                Layout.fillWidth: true
                model: [qsTr("Virtual cable"), qsTr("Virtual output sink")]
            }
            Label { text: qsTr("Name"); font.bold: true }
            TextField {
                id: nameField
                objectName: "deviceNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("For example, Voice chat")
                maximumLength: 128
                Accessible.name: qsTr("Device name")
                onAccepted: if (createAction.enabled) createAction.clicked()
            }
            Label {
                text: typeField.currentIndex === 0
                    ? qsTr("Creates a saved output sink and microphone input pair.")
                    : qsTr("Creates a saved virtual output sink.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: createDialog.close(); enabled: !AudioBackend.busy }
            Button {
                id: createAction
                objectName: "confirmCreateButton"
                text: AudioBackend.busy ? qsTr("Creating…") : qsTr("Create")
                icon.name: "list-add"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                enabled: AudioBackend.connected && !AudioBackend.busy && nameField.text.trim().length > 0
                onClicked: AudioBackend.createDevice(typeField.currentIndex === 0 ? "cable" : "sink", nameField.text)
            }
        }
    }
    Dialog {
        id: removeDialog
        objectName: "removeDeviceDialog"
        property var device: ({ label: "", token: "" })
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(380, root.width - 24)
        modal: true
        title: qsTr("Delete virtual device?")
        Label {
            width: parent.width
            text: qsTr("Delete %1 and its saved routes? Applications using its endpoints may lose audio.").arg(removeDialog.device.label)
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: removeDialog.close()
            }
            Button {
                objectName: "confirmDeleteButton"
                text: qsTr("Delete")
                icon.name: "edit-delete"
                enabled: AudioBackend.connected && !AudioBackend.busy
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: { AudioBackend.destroyDevice(removeDialog.device.token); removeDialog.close() }
            }
        }
    }
    Connections {
        target: AudioBackend
        function onDeviceCreated() { createDialog.close() }
        function onStatusChanged() {
            if (!AudioBackend.connected) { createDialog.close(); removeDialog.close() }
        }
    }
}
