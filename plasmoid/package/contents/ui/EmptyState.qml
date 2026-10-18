import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: empty
    property string title
    property string description
    property string iconName: "audio-card"
    spacing: 8
    Layout.fillWidth: true
    Layout.topMargin: 24
    Layout.bottomMargin: 24
    Kirigami.Icon {
        source: empty.iconName
        implicitWidth: 36
        implicitHeight: 36
        Layout.alignment: Qt.AlignHCenter
        opacity: 0.5
    }
    Label {
        text: empty.title
        font.bold: true
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
    Label {
        text: empty.description
        opacity: 0.7
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
}
