import QtQuick

// The indicator: how many agents work, wait for you, are done, and idle, in
// fixed slots so it never changes size. Clicking it opens the panel.
Rectangle {
    id: pill
    property bool expanded: false
    signal toggled()

    color: "#0e0e10"
    radius: theme.radius
    border.width: 1
    border.color: backend.error !== "" || backend.blocked > 0 ? "#ff6b6b"
                : hover.hovered || pill.expanded ? theme.accent : "#33333a"

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: pill.toggled() }

    Row {
        anchors.centerIn: parent
        visible: backend.error === ""
        spacing: 0

        Repeater {
            model: [
                { status: "working", count: backend.working },
                { status: "blocked", count: backend.blocked },
                { status: "done", count: backend.done },
                { status: "idle", count: backend.idle }
            ]
            delegate: Item {
                required property var modelData
                width: 40
                height: pill.height
                opacity: modelData.count > 0 ? 1 : 0.35

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    StatusDot { anchors.verticalCenter: parent.verticalCenter; status: modelData.status }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.count.toLocaleString(Qt.locale(translator.language), 'f', 0)
                        color: "#e6e6ea"
                        font.pixelSize: 13
                        font.bold: modelData.count > 0
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: backend.error !== ""
        text: qsTr("herdr unavailable")
        color: "#ff6b6b"
        font.pixelSize: 12
        elide: Text.ElideRight
        width: Math.min(implicitWidth, pill.width - 16)
    }
}
