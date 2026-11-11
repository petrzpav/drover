import QtQuick
import QtQuick.Layouts

// One agent in the panel's list: its state, title, and where it runs.
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

    height: 46
    radius: Math.min(4, theme.radius)
    color: selected ? Qt.alpha(theme.accent, 0.16) : hover.hovered ? "#1a1a1f" : "transparent"

    HoverHandler { id: hover }
    TapHandler {
        onTapped: row.picked()
        onDoubleTapped: row.opened()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10

        StatusDot { status: row.status; size: 9 }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: row.title
                color: "#e6e6ea"
                font.pixelSize: 13
                font.bold: row.selected
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.workspace + "  ·  " + row.cwd
                color: "#8a8a95"
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
        }

        Text {
            text: statusLabel(row.status)
            color: row.status === "blocked" ? "#ff6b6b" : row.status === "working" ? theme.accent : "#8a8a95"
            font.pixelSize: 11
        }
    }

    function statusLabel(status) {
        switch (status) {
        case "working": return qsTr("working")
        case "blocked": return qsTr("asks you")
        case "done": return qsTr("done")
        case "idle": return qsTr("idle")
        }
        return qsTr("unknown")
    }
}
