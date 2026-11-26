import QtQuick
import QtQuick.Controls

// The selected agent's conversation in points, beside its terminal: for each
// of your prompts what the agent did, said and asked, each point in two lines
// at most. A model sums up the points as the turn goes on; the summary stands
// in for the points it covers, and the newest ones show as they come. A click
// on a point scrolls the terminal to it and gives the terminal the keyboard.
Rectangle {
    id: overview
    readonly property color line: Qt.alpha(theme.foreground, 0.12)
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    readonly property bool claude: backend.selectedKind === "claude"
    // The point last clicked ("prompt3", "point12"), and how finding it in
    // the terminal went: "seeking", "found", "missing" or "stopped".
    property string picked: ""
    property string pickState: ""
    // Words to find in the agent's terminal.
    signal pointPicked(string needle)

    color: theme.background

    function pick(key, serial, index) {
        pickNeedle(key, timeline.needle(serial, index))
    }
    function pickNeedle(key, needle) {
        picked = key
        pickState = "seeking"
        pointPicked(needle)
    }
    // A summary point's mark and color.
    function summaryGlyph(kind) {
        switch (kind) {
        case "done": return "\uf00c"
        case "ship": return "\uf417"
        case "change": return "\uf040"
        case "fail": return "\uf00d"
        case "ask": return "\uf059"
        case "open": return "\uf10c"
        }
        return "\uf075"
    }
    function summaryTint(kind) {
        return kind === "fail" ? theme.urgent : kind === "done" || kind === "ship" ? theme.success
             : kind === "ask" || kind === "open" ? theme.accent : dim
    }
    // The terminal's answer.
    function revealEnded(result) {
        pickState = result
        if (result === "missing")
            missingFade.restart()
    }
    Timer { id: missingFade; interval: 1500; onTriggered: if (overview.pickState === "missing") overview.pickState = "" }
    function pickColor(key) {
        if (key !== picked)
            return "transparent"
        return pickState === "missing" ? Qt.alpha(theme.urgent, 0.18) : Qt.alpha(theme.foreground, 0.08)
    }

    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
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

    Connections {
        target: timeline
        function onSessionChanged() {
            overview.picked = ""
            overview.pickState = ""
            list.follow = true
        }
        // Read again from the start: the points are new ones.
        function onStateChanged() {
            if (timeline.state === "loading")
                overview.picked = ""
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

    ListView {
        id: list
        objectName: "overviewList"
        x: 1
        width: parent.width - 1
        height: parent.height
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
            required property var points
            required property int covered
            required property bool summarizing
            width: ListView.view.width
            Component.onCompleted: timeline.want(turn.serial)
            spacing: 2

            Rectangle {
                id: promptBox
                objectName: "prompt" + turn.serial
                visible: turn.prompt !== ""
                readonly property string key: "prompt" + turn.serial
                x: 10
                width: parent.width - 20
                height: promptText.implicitHeight + 14
                radius: theme.radius
                color: Qt.alpha(theme.foreground, key === overview.picked ? 0.13 : 0.07)
                border.width: key === overview.picked && overview.pickState === "missing" ? 1 : 0
                border.color: theme.urgent

                Text {
                    id: promptText
                    x: 10
                    y: 7
                    width: parent.width - 20 - promptTime.width - 10
                    text: turn.prompt
                    textFormat: Text.PlainText
                    color: theme.foreground
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Text {
                    id: promptTime
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    y: 7
                    text: turn.time ? Qt.formatTime(turn.time, "HH:mm") : ""
                    color: overview.dim
                    // While the model sums the turn up, a spinner in its place.
                    opacity: turn.summarizing ? 0 : 1
                }
                IconButton {
                    objectName: "summarizing" + turn.serial
                    visible: turn.summarizing
                    anchors.horizontalCenter: promptTime.horizontalCenter
                    anchors.verticalCenter: promptTime.verticalCenter
                    width: 22
                    height: 22
                    busy: true
                    enabled: false
                    opacity: 1
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: overview.pick(promptBox.key, turn.serial, -1)
                }
            }

            Item { width: 1; height: 4; visible: turn.prompt !== "" }

            // The summary, for the points it covers.
            Repeater {
                model: turn.points
                delegate: Rectangle {
                    id: summary
                    required property var modelData
                    required property int index
                    objectName: "summary" + turn.serial + "_" + index
                    readonly property string key: "summary" + turn.serial + "_" + index
                    x: 6
                    width: turn.width - 12
                    height: summaryText.implicitHeight + 6
                    radius: theme.radius
                    color: overview.pickColor(key)
                    Text {
                        x: 8
                        y: 3
                        width: 18
                        text: overview.summaryGlyph(summary.modelData.kind)
                        color: overview.summaryTint(summary.modelData.kind)
                    }
                    Text {
                        id: summaryText
                        x: 32
                        y: 3
                        width: parent.width - 32 - 8
                        text: summary.modelData.text
                        textFormat: Text.PlainText
                        color: summary.modelData.kind === "fail" ? theme.urgent : theme.foreground
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: overview.pickNeedle(summary.key, summary.modelData.needle)
                    }
                }
            }

            // The points no summary covers yet.
            Repeater {
                model: turn.items
                delegate: Rectangle {
                    id: point
                    required property var modelData
                    required property int index
                    objectName: "point" + modelData.id
                    readonly property string key: "point" + modelData.id
                    readonly property bool failed: modelData.state === "failed" || modelData.state === "denied"
                    readonly property int more: modelData.steps.length - 1
                    visible: index >= turn.covered
                    x: 6
                    width: turn.width - 12
                    height: body.implicitHeight + 6
                    radius: theme.radius
                    color: overview.pickColor(key)

                    Text {
                        x: 8
                        y: 3
                        width: 18
                        text: overview.glyph(point.modelData.kind)
                        color: overview.tint(point.modelData)
                    }
                    // Two lines at most: the point, or the point and its
                    // answer or error.
                    Column {
                        id: body
                        x: 32
                        y: 3
                        width: parent.width - 32 - 8
                        Text {
                            id: pointText
                            objectName: "pointText" + point.modelData.id
                            width: parent.width
                            // Styled for the run's count; the text itself
                            // flows on as one paragraph.
                            textFormat: Text.StyledText
                            text: overview.escaped(point.modelData.text)
                                  + (point.more > 0 && point.modelData.kind !== "edit"
                                     ? "  <font color=\"" + Qt.tint(theme.background, overview.dim) + "\">+" + point.more + "</font>" : "")
                            color: point.failed ? theme.urgent
                                 : point.modelData.kind === "said" ? Qt.alpha(theme.foreground, 0.8)
                                 : theme.foreground
                            wrapMode: Text.Wrap
                            maximumLineCount: point.modelData.detail !== "" ? 1 : 2
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: point.modelData.detail !== ""
                            width: parent.width
                            text: "→ " + point.modelData.detail
                            textFormat: Text.PlainText
                            color: point.failed ? theme.urgent : overview.dim
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: overview.pick(point.key, turn.serial, point.index)
                    }
                }
            }
        }
    }

    Text {
        x: 24
        width: parent.width - 48
        anchors.verticalCenter: parent.verticalCenter
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
