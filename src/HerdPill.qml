import QtQuick

// The indicator. Its look comes from the config (indicator = dots, bars or
// sheep, see HerdDots, HerdBars and HerdSheep); the pill around it never
// changes size, lights up when the herd tells it to, opens the panel on a
// click and moves on a drag.
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

    // A moment the herd changed: the border lights up in that color and fades.
    function flash(tint) {
        flashFrame.border.color = tint
        flashFrame.color = Qt.alpha(tint, 0.14)
        flashFade.restart()
    }

    Rectangle {
        id: flashFrame
        anchors.fill: parent
        radius: pill.radius
        border.width: 2
        opacity: 0
    }
    NumberAnimation {
        id: flashFade
        target: flashFrame
        property: "opacity"
        from: 1
        to: 0
        duration: 1300
        easing.type: Easing.OutCubic
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

    Loader {
        id: content
        anchors.fill: parent
        visible: backend.error === ""
        source: backend.indicator === "sheep" ? "HerdSheep.qml"
              : backend.indicator === "dots" ? "HerdDots.qml" : "HerdBars.qml"
    }
    Connections {
        target: content.item
        function onFlash(tint) { pill.flash(tint) }
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
