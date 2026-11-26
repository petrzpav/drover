import QtQuick
import QtQuick.Controls

// The selected agent's conversation in points, beside its terminal: what it
// does now, and for each of your prompts what it did, said and asked. A
// click on a point opens it: the whole text, the answer or the error, every
// command of a run. It never takes the keyboard from the terminal.
Rectangle {
    id: overview
    property var statusLabel: function (status) { return status }
    readonly property color line: Qt.alpha(theme.foreground, 0.12)
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    readonly property string status: backend.selectedStatus
    readonly property bool claude: backend.selectedKind === "claude"
    // Points opened by a click, by their id ("point12", or "3:prompt" for a turn's prompt).
    property var opened: ({})

    color: theme.background

    function toggle(key) {
        const next = Object.assign({}, opened)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        opened = next
    }
    // For styled text: the characters it would take for markup, and its lines.
    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
    }
    function glyph(kind) {
        switch (kind) {
        case "command": return ""
        case "milestone": return ""
        case "edit": return ""
        case "question": return ""
        case "agent": return ""
        case "web": return ""
        case "said": return ""
        case "nudge": return ""
        case "recap": return ""
        case "compact": return ""
        case "interrupted": return ""
        }
        return ""
    }
    function tint(point) {
        if (point.state === "failed" || point.state === "denied")
            return theme.urgent
        if (point.state === "running")
            return theme.accent
        if (point.kind === "milestone")
            return theme.success
        if (point.kind === "recap" || point.kind === "nudge")
            return theme.foreground
        return dim
    }

    // What it does now, or what it said last: always three lines high.
    readonly property string nowText: {
        if (backend.selected === "" || !claude)
            return ""
        if (status === "working")
            return timeline.activity !== "" ? timeline.activity
                 : timeline.lastSaid !== "" ? timeline.lastSaid : qsTr("Thinking")
        if (status === "blocked")
            return timeline.activity !== "" ? qsTr("Waits for you: %1").arg(timeline.activity) : qsTr("Waits for you")
        return timeline.lastSaid
    }

    Connections {
        target: timeline
        function onSessionChanged() {
            overview.opened = ({})
            list.follow = true
        }
        // Read again from the start: the points are new ones.
        function onStateChanged() {
            if (timeline.state === "loading")
                overview.opened = ({})
        }
    }

    // Clicks and the wheel stay here, never reaching the terminal beneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The line between the terminal and the overview.
    Rectangle { width: 1; height: parent.height; color: overview.line }

    Item {
        id: now
        objectName: "overviewNow"
        x: 1
        width: parent.width - 1
        height: 22 + nowMetrics.lineSpacing * 3 + 14
        FontMetrics { id: nowMetrics }

        Row {
            x: 14
            y: 12
            spacing: 8
            visible: overview.claude
            StatusDot { anchors.verticalCenter: parent.verticalCenter; status: overview.status }
            Text { text: overview.statusLabel(overview.status); color: overview.dim }
        }
        Text {
            objectName: "overviewNowText"
            x: 14
            y: 12 + nowMetrics.lineSpacing + 6
            width: parent.width - 28
            text: overview.nowText
            textFormat: Text.PlainText
            color: theme.foreground
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }
    Rectangle { x: 1; y: now.height; width: parent.width - 1; height: 1; color: overview.line }

    ListView {
        id: list
        objectName: "overviewList"
        x: 1
        y: now.height + 1
        width: parent.width - 1
        height: parent.height - y
        clip: true
        visible: overview.claude
        model: overview.claude ? timeline : null
        spacing: 16
        // Room above the first turn and below the last; as header and footer,
        // so positionViewAtEnd() reaches the very end.
        header: Item { width: 1; height: 12 }
        footer: Item { width: 1; height: 12 }
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            onPressedChanged: if (!pressed) list.follow = list.atYEnd
        }

        // At the end, the list stays there as points come; scrolled up, it
        // stays where you are.
        property bool follow: true
        function settle() {
            if (follow)
                positionViewAtEnd()
        }
        onMovementEnded: follow = atYEnd
        onContentHeightChanged: Qt.callLater(settle)
        onCountChanged: Qt.callLater(settle)
        onHeightChanged: Qt.callLater(settle)

        delegate: Column {
            id: turn
            required property var serial
            required property string prompt
            required property var time
            required property var items
            width: ListView.view.width
            spacing: 6

            Rectangle {
                visible: turn.prompt !== ""
                x: 10
                width: parent.width - 20
                height: promptText.implicitHeight + 14
                radius: theme.radius
                color: Qt.alpha(theme.foreground, 0.07)
                readonly property bool open: overview.opened[turn.serial + ":prompt"] === true

                Text {
                    id: promptText
                    x: 10
                    y: 7
                    width: parent.width - 20 - promptTime.width - 10
                    text: turn.prompt
                    textFormat: Text.PlainText
                    color: theme.foreground
                    wrapMode: Text.Wrap
                    maximumLineCount: parent.open ? 1000 : 3
                    elide: Text.ElideRight
                }
                Text {
                    id: promptTime
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    y: 7
                    text: turn.time ? Qt.formatTime(turn.time, "HH:mm") : ""
                    color: overview.dim
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: promptText.truncated || parent.open
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: overview.toggle(turn.serial + ":prompt")
                }
            }

            Repeater {
                model: turn.items
                delegate: Item {
                    id: point
                    required property var modelData
                    required property int index
                    readonly property string key: "point" + modelData.id
                    readonly property bool open: overview.opened[key] === true
                    readonly property bool failed: modelData.state === "failed" || modelData.state === "denied"
                    readonly property int more: modelData.steps.length - 1
                    // The agent's last words in a turn are its answer: more of them shows.
                    readonly property bool answer: modelData.kind === "said" && index === turn.items.length - 1
                    width: turn.width
                    height: body.implicitHeight

                    Text {
                        x: 14
                        width: 18
                        text: overview.glyph(point.modelData.kind)
                        color: overview.tint(point.modelData)
                    }
                    Column {
                        id: body
                        x: 38
                        width: parent.width - 38 - 14
                        spacing: 2
                        Text {
                            id: pointText
                            objectName: "pointText" + point.modelData.id
                            width: parent.width
                            textFormat: Text.StyledText
                            text: overview.escaped(point.modelData.text)
                                  + (point.more > 0 && point.modelData.kind !== "edit" && !point.open
                                     ? "  <font color=\"" + Qt.tint(theme.background, overview.dim) + "\">+" + point.more + "</font>" : "")
                            color: point.failed ? theme.urgent
                                 : point.modelData.kind === "said" && !point.answer ? Qt.alpha(theme.foreground, 0.7)
                                 : theme.foreground
                            wrapMode: Text.Wrap
                            maximumLineCount: point.open ? 1000 : point.answer ? 5 : point.modelData.kind === "said" ? 2
                                            : point.modelData.kind === "question" || point.modelData.kind === "recap" ? 3 : 1
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: point.modelData.detail !== ""
                            width: parent.width
                            text: "→ " + point.modelData.detail
                            textFormat: Text.PlainText
                            color: point.failed ? theme.urgent : overview.dim
                            wrapMode: Text.Wrap
                            maximumLineCount: point.open ? 1000 : 2
                            elide: Text.ElideRight
                        }
                        Repeater {
                            model: point.open && point.more > 0 ? point.modelData.steps : []
                            delegate: Text {
                                required property string modelData
                                width: body.width
                                text: "· " + modelData
                                textFormat: Text.PlainText
                                color: overview.dim
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: point.open || point.more > 0 || pointText.truncated || point.modelData.detail !== ""
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: overview.toggle(point.key)
                    }
                }
            }
        }
    }

    Text {
        x: 24
        width: parent.width - 48
        y: now.height + 1 + (parent.height - now.height - 1 - height) / 2
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: text !== ""
        text: backend.selected === "" ? ""
            : !overview.claude ? qsTr("The overview follows Claude agents.")
            : timeline.state === "loading" ? ""
            : timeline.count === 0 ? qsTr("Nothing in this conversation yet.") : ""
        color: overview.dim
    }
}
