import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ToolButton {
    id: heading
    property int count: 0
    property bool expanded: true
    property string title
    Layout.fillWidth: true
    font.bold: true
    Accessible.name: title + ", " + count + (expanded ? qsTr(", expanded") : qsTr(", collapsed"))
    onClicked: expanded = !expanded
    contentItem: RowLayout {
        spacing: 6
        Kirigami.Icon { source: heading.expanded ? "arrow-down" : "arrow-right"; implicitWidth: 16; implicitHeight: 16 }
        Label { text: heading.title + " (" + heading.count + ")"; font: heading.font; Layout.fillWidth: true; elide: Text.ElideRight }
    }
}
