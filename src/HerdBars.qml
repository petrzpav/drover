import QtQuick

// The herd as bars (indicator = bars), one per agent. Working bars bounce,
// a bar that just finished shoots up and settles green, one that asks you
// blinks red, idle ones lie low. On the right, the one state that matters
// most. Bars narrow once the herd outgrows the room left of the label.
Item {
    id: herd
    objectName: "herdBars"
    signal flash(color tint)

    readonly property real barRoom: Math.max(0, width - 24 - label.implicitWidth - 10)
    readonly property real barStep: Math.min(8, barRoom / Math.max(1, backend.agents.count))
    readonly property int barWidth: Math.max(2, Math.round(barStep / 2))

    Item {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: herd.barRoom
        height: herd.height - 4
        clip: true

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.max(1, Math.round(herd.barStep) - herd.barWidth)

            Repeater {
                model: backend.agents
                delegate: Item {
                    id: slot
                    objectName: "herdBar"
                    required property string status
                    // Set once the bar exists, so a change can tell where it came from.
                    property string was
                    // Height as a share of the full bar; 1.3 overshoots on landing.
                    property real level: 0.18
                    // Each bar bounces at its own pace, so the herd never moves in step.
                    readonly property int period: 450 + Math.floor(Math.random() * 450)
                    readonly property color tint: status === "working" ? theme.accent
                                                : status === "blocked" ? theme.urgent
                                                : status === "done" ? theme.success
                                                : theme.muted

                    width: herd.barWidth
                    height: 18

                    function rest(s) {
                        return s === "blocked" ? 1 : s === "done" ? 0.45 : s === "working" ? 0.6 : 0.18
                    }

                    Component.onCompleted: {
                        was = status
                        level = rest(status)
                    }
                    onStatusChanged: {
                        const before = was
                        was = status
                        if (status === "done" && before === "working") {
                            settle.stop()
                            land.restart()
                            herd.flash(theme.success)
                            return
                        }
                        land.stop()
                        settle.to = rest(status)
                        settle.restart()
                        if (status === "blocked")
                            herd.flash(theme.urgent)
                    }

                    Rectangle {
                        id: glow
                        anchors.centerIn: bar
                        width: bar.width + 4
                        height: bar.height + 4
                        radius: 2
                        color: Qt.alpha(theme.success, 0.45)
                        opacity: 0
                    }
                    Rectangle {
                        id: bar
                        anchors.centerIn: parent
                        width: parent.width
                        height: parent.height * slot.level
                        color: slot.tint
                        opacity: slot.status === "working" || slot.status === "blocked" || slot.status === "done" ? 1 : 0.55
                        Behavior on color { ColorAnimation { duration: 300 } }
                    }

                    NumberAnimation {
                        id: settle
                        target: slot
                        property: "level"
                        duration: 350
                        easing.type: Easing.OutCubic
                    }
                    ParallelAnimation {
                        id: land
                        SequentialAnimation {
                            NumberAnimation { target: slot; property: "level"; to: 1.3; duration: 200; easing.type: Easing.OutCubic }
                            NumberAnimation { target: slot; property: "level"; to: 0.45; duration: 700; easing.type: Easing.OutCubic }
                        }
                        NumberAnimation { target: glow; property: "opacity"; from: 0.9; to: 0; duration: 900; easing.type: Easing.OutCubic }
                    }
                    SequentialAnimation {
                        running: slot.status === "working" && !settle.running && !land.running
                        loops: Animation.Infinite
                        NumberAnimation { target: slot; property: "level"; to: 1; duration: slot.period; easing.type: Easing.InOutSine }
                        NumberAnimation { target: slot; property: "level"; to: 0.22; duration: slot.period; easing.type: Easing.InOutSine }
                    }
                    // Blinks while it waits for you, and always ends fully lit.
                    SequentialAnimation on opacity {
                        running: slot.status === "blocked"
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: 550; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1; duration: 550; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
    }

    Text {
        id: label
        objectName: "herdLabel"
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: backend.blocked > 0 ? qsTr("%n asks you", "", backend.blocked)
            : backend.working > 0 ? qsTr("%n working", "", backend.working)
            : backend.done > 0 ? qsTr("%n done", "", backend.done)
            : backend.agents.count > 0 ? qsTr("all idle")
            : qsTr("%n agents", "", 0)
        color: backend.blocked > 0 ? theme.urgent
             : backend.working > 0 ? theme.accent
             : backend.done > 0 ? theme.success
             : theme.muted
    }
}
