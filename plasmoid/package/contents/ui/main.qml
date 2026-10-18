pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import "PlasmaAudioManager" as Native

PlasmoidItem {
    id: applet
    Plasmoid.icon: "audio-card"
    toolTipMainText: qsTr("Audio Manager")
    toolTipSubText: Native.AudioBackend.status

    compactRepresentation: Kirigami.Icon {
        source: "audio-card"
        active: compactMouse.hovered
        Accessible.name: qsTr("Open Audio Manager")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: applet.expanded = !applet.expanded
        TapHandler { onTapped: applet.expanded = !applet.expanded }
        HoverHandler { id: compactMouse }
        Keys.onSpacePressed: applet.expanded = !applet.expanded
        Keys.onReturnPressed: applet.expanded = !applet.expanded
        activeFocusOnTab: true
    }
    fullRepresentation: AudioPopup {
        Layout.minimumWidth: 380
        Layout.minimumHeight: 360
        Layout.preferredWidth: 440
        Layout.preferredHeight: 560
    }
}
