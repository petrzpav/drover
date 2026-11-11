import QtQuick

// The indicator: how many agents work, wait for you, are done, and idle, in
// fixed slots so it never changes size. Clicking it opens the panel;
// dragging it moves it.
Rectangle {
    id: pill
    property bool expanded: false
    signal toggled()
    signal moveStarted()
    signal moved(real dx, real dy)
    signal moveFinished()

    color: mouse.containsMouse ? Qt.tint(theme.background, Qt.alpha(theme.foreground, 0.08)) : theme.background
    radius: theme.radius
    border.width: 1
    border.color: backend.error !== "" || backend.blocked > 0 ? theme.urgent
                : theme.border

    // A press that moves a few pixels is a drag, anything else a click.
    // Positions are taken in window coordinates, which stay put while the
    // indicator moves under the pointer.
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor

        property point start
        property bool dragging: false

        onPressed: (event) => {
            start = mapToItem(null, event.x, event.y)
            dragging = false
        }
        onPositionChanged: (event) => {
            if (!pressed)
                return
            const point = mapToItem(null, event.x, event.y)
            const dx = point.x - start.x, dy = point.y - start.y
            if (!dragging) {
                if (Math.abs(dx) + Math.abs(dy) < 6)
                    return
                dragging = true
                pill.moveStarted()
            }
            pill.moved(dx, dy)
        }
        onReleased: {
            if (dragging)
                pill.moveFinished()
            else
                pill.toggled()
            dragging = false
        }
        onCanceled: dragging = false
    }

    Row {
        anchors.centerIn: parent
        visible: backend.error === ""

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
                        color: theme.foreground
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: backend.error !== ""
        text: qsTr("herdr unavailable")
        color: theme.urgent
        font.family: "monospace"
        font.pixelSize: 12
        elide: Text.ElideRight
        width: Math.min(implicitWidth, pill.width - 16)
    }
}
