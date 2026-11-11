import QtQuick

// One agent state as a small mark: a pulsing accent dot while working, the
// theme's red while it waits for an answer, green when done, a hollow ring
// when idle.
Item {
    id: dot
    property string status: "idle"
    property int size: 8
    implicitWidth: size
    implicitHeight: size

    readonly property bool hollow: status !== "working" && status !== "blocked" && status !== "done"
    readonly property color tint: status === "working" ? theme.accent
                                : status === "blocked" ? theme.urgent
                                : status === "done" ? theme.success
                                : theme.muted

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: dot.hollow ? "transparent" : dot.tint
        border.width: dot.hollow ? 1.5 : 0
        border.color: dot.tint

        SequentialAnimation on opacity {
            running: dot.status === "working"
            loops: Animation.Infinite
            alwaysRunToEnd: true
            NumberAnimation { to: 0.3; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
        }
    }
}
