import QtQuick

// One agent state as a small mark: a pulsing accent dot while working, red
// while it waits for an answer, green when done, a hollow ring when idle.
Item {
    id: dot
    property string status: "idle"
    property int size: 8
    implicitWidth: size
    implicitHeight: size

    readonly property color tint: status === "working" ? theme.accent
                                : status === "blocked" ? "#ff6b6b"
                                : status === "done" ? "#7bd88f"
                                : "#7a7a85"

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: dot.status === "idle" || dot.status === "unknown" ? "transparent" : dot.tint
        border.width: dot.status === "idle" || dot.status === "unknown" ? 1.5 : 0
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
