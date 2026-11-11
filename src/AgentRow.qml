import QtQuick
import QtQuick.Layouts

// One agent in the sidebar, drawn like an Omarchy menu entry: the selected
// one gets a faint fill, a faint border, and accent text.
Rectangle {
    id: row
    required property int index
    required property string paneId
    required property string title
    required property string status
    required property string workspace
    required property string cwd
    property bool selected: false
    signal picked()
    signal opened()

    height: rowContent.implicitHeight + 18
    radius: Math.min(4, theme.radius)
    color: selected || hover.hovered ? Qt.alpha(theme.foreground, 0.08) : "transparent"
    border.width: selected ? 1 : 0
    border.color: Qt.alpha(theme.border, 0.25)

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: row.picked()
        onDoubleTapped: row.opened()
    }

    RowLayout {
        id: rowContent
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 10

        StatusDot { status: row.status; size: 8 }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: row.title
                color: row.selected ? theme.accent : theme.foreground
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.workspace + " · " + row.cwd
                color: theme.muted
                elide: Text.ElideMiddle
            }
        }
    }
}
