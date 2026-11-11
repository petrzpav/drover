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

    color: pill.expanded ? Qt.tint(theme.background, Qt.alpha(theme.foreground, 0.18))
         : mouse.containsMouse ? Qt.tint(theme.background, Qt.alpha(theme.foreground, 0.08))
         : theme.background
    radius: theme.radius
    border.width: 2
    border.color: backend.error !== "" || backend.blocked > 0 ? theme.urgent : theme.border

    function countFor(status) {
        return status === "working" ? backend.working : status === "blocked" ? backend.blocked
             : status === "done" ? backend.done : backend.idle
    }

    // A press that moves a few pixels is a drag, anything else a click.
    // Positions are taken in window coordinates, which stay put while the
    // indicator moves under the pointer.
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor

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

    Hint {
        visible: mouse.containsMouse && !mouse.pressed
        text: backend.error !== "" ? backend.error : qsTr("Click to open, drag to move")
    }

    Row {
        anchors.centerIn: parent
        visible: backend.error === ""

        Repeater {
            model: ["working", "blocked", "done", "idle"]
            delegate: Item {
                required property string modelData
                readonly property int count: pill.countFor(modelData)
                width: 40
                height: pill.height
                opacity: count > 0 ? 1 : 0.5

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    StatusDot { anchors.verticalCenter: parent.verticalCenter; status: modelData }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: count.toLocaleString(Qt.locale(translator.language), 'f', 0)
                        color: theme.foreground
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
        elide: Text.ElideRight
        width: Math.min(implicitWidth, pill.width - 16)
    }
}
