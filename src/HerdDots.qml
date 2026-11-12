import QtQuick

// The plain indicator (indicator = dots): how many agents work, wait for
// you, are done, and idle, in fixed slots so it never changes size.
Item {
    id: dots
    objectName: "herdDots"
    signal flash(color tint)

    function countFor(status) {
        return status === "working" ? backend.working : status === "blocked" ? backend.blocked
             : status === "done" ? backend.done : backend.idle
    }

    Row {
        anchors.centerIn: parent

        Repeater {
            model: ["working", "blocked", "done", "idle"]
            delegate: Item {
                required property string modelData
                readonly property int count: dots.countFor(modelData)
                width: 40
                height: dots.height
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
}
