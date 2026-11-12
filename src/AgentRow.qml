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
    property string statusText: ""
    signal picked()
    signal opened()

    // The workspace is shown only when it says more than the folder does.
    readonly property bool workspaceIsFolder: cwd === "~/" + workspace || cwd.endsWith("/" + workspace)
                                              || cwd === "~" || workspace === ""
    readonly property string place: workspaceIsFolder ? cwd : workspace + " · " + cwd

    height: rowContent.implicitHeight + 18
    radius: theme.radius
    color: selected || hover.hovered ? Qt.alpha(theme.foreground, 0.08) : Qt.alpha(theme.foreground, 0)
    Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
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
                color: theme.foreground
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.statusText + " · " + row.place
                color: Qt.alpha(theme.foreground, 0.55)
                elide: Text.ElideMiddle
            }
        }
    }
}
