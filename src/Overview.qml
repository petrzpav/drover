import QtQuick
import QtQuick.Controls

// The selected agent's conversation in points, in place of its terminal:
// for each of your prompts what the agent did, said and asked, each point in
// two lines at most. A model sums up the points as the turn goes on; the
// summary stands in for the points it covers, and the newest ones show as
// they come. A click opens a point where it is: the whole text, the error,
// every command of a run, or under a summary the points it sums up. Below,
// a field sends the agent a message.
Rectangle {
    id: overview
    readonly property color line: Qt.alpha(theme.foreground, 0.12)
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    readonly property bool claude: backend.selectedKind === "claude"
    // Opened points: "prompt3", "point12" (by id), "batch3_0" (a summary's points).
    property var opened: ({})
    // What you were writing to each agent, by pane, while you look at another.
    property var drafts: ({})
    property string draftPane: ""
    // What went out last, so only that is cleared once it went.
    property string sending: ""
    readonly property bool asking: backend.selectedStatus === "blocked"
    // The agent asks something: its terminal is where to answer.
    signal answerWanted()
    readonly property int readable: 900
    // How many of a turn's newest points show before the rest is asked for:
    // a long turn's hundreds would take a while to draw, at every change.
    readonly property int newest: 6

    color: theme.background

    function focusInput() {
        input.forceActiveFocus()
    }
    function toggle(key) {
        // What opens stays where it is, rather than the list following its end.
        list.follow = false
        const next = Object.assign({}, opened)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        opened = next
    }
    function send() {
        if (input.text.trim() !== "" && backend.busy === "" && !asking) {
            sending = input.text.trim()
            backend.sendPrompt(backend.selected, sending)
        }
    }
    // The agent's words as Markdown, safely: a zero-width space after every
    // "<" and every "!" before "[" breaks raw HTML and pictures wherever they
    // are (in code too, where it can't be seen), so nothing is ever fetched.
    function markdown(text) {
        return text.replace(/</g, "<\u200b").replace(/!(?=\[)/g, "!\u200b")
    }
    function openLink(link) {
        if (/^https?:\/\//i.test(link))
            Qt.openUrlExternally(link)
    }
    // The question a reply ends with: its last paragraph, as plain words.
    function endQuestion(point) {
        if (!point || point.kind !== "said" || !/\?\s*$/.test(point.text))
            return ""
        const paragraphs = point.text.split(/\n\s*\n/).filter(p => p.trim() !== "")
        return paragraphs[paragraphs.length - 1].replace(/[*`]/g, "").replace(/\s+/g, " ").trim()
    }
    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    // A summary's drawing as styled text: its spaces kept, its marks
    // ([ok] [fail] [warn] [dim] … [/]) in the theme's colors.
    function drawing(text) {
        const colors = { ok: theme.success, fail: theme.urgent, warn: theme.accent, dim: Qt.tint(theme.background, dim) }
        return text.split("\n").map(line => {
            let out = "", open = 0
            for (const part of line.split(/(\[(?:ok|fail|warn|dim|\/)\])/)) {
                const mark = part.match(/^\[(ok|fail|warn|dim|\/)\]$/)
                if (!mark) {
                    out += escaped(part).replace(/ /g, "&nbsp;")
                } else if (mark[1] !== "/") {
                    out += "<font color=\"" + colors[mark[1]] + "\">"
                    ++open
                } else if (open > 0) {
                    out += "</font>"
                    --open
                }
            }
            return out + "</font>".repeat(open)
        }).join("<br>")
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
        if (point.state === "running" || point.state === "queued")
            return theme.accent
        if (point.kind === "milestone")
            return theme.success
        if (point.kind === "recap" || point.kind === "nudge")
            return theme.foreground
        return dim
    }
    // A summary point's mark and color.
    function summaryGlyph(kind) {
        switch (kind) {
        case "done": return ""
        case "ship": return ""
        case "change": return ""
        case "fail": return ""
        case "ask": return ""
        case "open": return ""
        }
        return ""
    }
    function summaryTint(kind) {
        return kind === "fail" ? theme.urgent : kind === "done" || kind === "ship" ? theme.success
             : kind === "ask" || kind === "open" ? theme.accent : dim
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
    Connections {
        target: backend
        function onPromptSent(pane, sent) {
            if (!sent)
                return
            // What was sent goes; what you typed on since stays.
            if (pane === overview.draftPane && input.text.trim().startsWith(overview.sending)) {
                input.text = input.text.trim().slice(overview.sending.length).trim()
                input.cursorPosition = input.length
            }
            const next = Object.assign({}, overview.drafts)
            delete next[pane]
            overview.drafts = next
        }
        // Each agent keeps its own draft.
        function onSelectedChanged() {
            const next = Object.assign({}, overview.drafts)
            if (overview.draftPane !== "")
                next[overview.draftPane] = input.text
            overview.drafts = next
            overview.draftPane = backend.selected
            input.text = next[backend.selected] || ""
            input.cursorPosition = input.length
        }
    }
    Component.onCompleted: draftPane = backend.selected

    // Clicks and the wheel stay here, never reaching the terminal beneath;
    // a click on nothing in particular goes to the message field.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
        onClicked: overview.focusInput()
    }

    // One point: its mark, its text in two lines at most, and opened, all of
    // it: the whole text (the agent's words as Markdown), its answer or
    // error, every command of a run.
    component PointRow: Rectangle {
        id: row
        required property var point
        readonly property string key: "point" + point.id
        readonly property bool open: overview.opened[key] === true
        readonly property bool failed: point.state === "failed" || point.state === "denied"
        readonly property int more: point.steps.length - 1
        readonly property bool opens: open || more > 0 || pointText.truncated || detailText.truncated
        objectName: key
        height: body.implicitHeight + 6
        radius: theme.radius
        color: hoverRow.hovered && opens ? Qt.alpha(theme.foreground, 0.05) : "transparent"
        HoverHandler { id: hoverRow }

        Text {
            x: 8
            y: 3
            width: 18
            text: overview.glyph(row.point.kind)
            color: overview.tint(row.point)
        }
        Column {
            id: body
            x: 32
            y: 3
            width: parent.width - 32 - 8
            spacing: 2
            Text {
                id: pointText
                objectName: "pointText" + row.point.id
                // The agent's words, opened, are Markdown in an item of their own.
                visible: !(row.open && row.point.kind === "said")
                width: parent.width
                textFormat: Text.StyledText
                text: overview.escaped(row.point.text)
                      + (row.more > 0 && row.point.kind !== "edit" && !row.open
                         ? "  <font color=\"" + Qt.tint(theme.background, overview.dim) + "\">+" + row.more + "</font>" : "")
                color: row.failed ? theme.urgent
                     : row.point.kind === "said" ? Qt.alpha(theme.foreground, 0.8)
                     : theme.foreground
                wrapMode: Text.Wrap
                maximumLineCount: row.open ? 100000 : row.point.detail !== "" ? 1 : 2
                elide: Text.ElideRight
            }
            Loader {
                active: row.open && row.point.kind === "said"
                visible: active
                width: parent.width
                sourceComponent: Text {
                    objectName: "pointWords" + row.point.id
                    width: body.width
                    textFormat: Text.MarkdownText
                    text: overview.markdown(row.point.text)
                    color: theme.foreground
                    linkColor: theme.accent
                    wrapMode: Text.Wrap
                    onLinkActivated: (link) => overview.openLink(link)
                }
            }
            Text {
                id: detailText
                visible: row.point.detail !== ""
                width: parent.width
                text: "→ " + row.point.detail
                textFormat: Text.PlainText
                color: row.failed ? theme.urgent : overview.dim
                wrapMode: Text.Wrap
                maximumLineCount: row.open ? 100000 : 1
                elide: Text.ElideRight
            }
            Repeater {
                model: row.open && row.more > 0 ? row.point.steps : []
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
        TapHandler {
            enabled: row.opens
            cursorShape: Qt.PointingHandCursor
            onTapped: overview.toggle(row.key)
        }
    }

    ListView {
        id: list
        objectName: "overviewList"
        width: parent.width
        anchors.top: parent.top
        anchors.bottom: composer.top
        clip: true
        visible: overview.claude
        model: overview.claude ? timeline : null
        spacing: 18
        // Room above the first turn and below the last; as header and footer,
        // so positionViewAtEnd() reaches the very end.
        header: Item { width: 1; height: 16 }
        footer: Item { width: 1; height: 16 }
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            onPressedChanged: if (!pressed) list.follow = list.atYEnd
        }
        HoverHandler { id: listHover }

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

        delegate: Item {
            id: turn
            required property int index
            required property var serial
            required property string prompt
            required property var time
            required property var items
            required property var batches
            required property int covered
            required property bool summarizing
            width: ListView.view.width
            height: column.implicitHeight
            // The summary as shown: as it is when the turn comes into view,
            // then held while the pointer is over the list and the turn in
            // view, so nothing moves under the pointer.
            property var shownBatches: []
            property int shownCovered: 0
            readonly property bool held: listHover.hovered && y + height > list.contentY && y < list.contentY + list.height
            Binding on shownBatches { value: turn.batches; when: !turn.held; restoreMode: Binding.RestoreNone }
            Binding on shownCovered { value: turn.covered; when: !turn.held; restoreMode: Binding.RestoreNone }
            Component.onCompleted: {
                shownBatches = batches
                shownCovered = covered
                timeline.want(turn.serial)
            }
            // The latest summary of the turn, if any.
            readonly property var summary: shownBatches.length > 0 ? shownBatches[shownBatches.length - 1].points : []
            // Older turns show their headline only, until opened.
            readonly property bool open: overview.opened["turn" + serial] === true
            readonly property bool compact: index < list.count - 2 && !open
            // The points under the summary, opened from it.
            readonly property bool detailed: overview.opened["details" + serial] === true
            // A reply ending with a question for you: always in sight.
            readonly property string question: overview.endQuestion(items.length > 0 ? items[items.length - 1] : null)

            Column {
                id: column
                // A readable measure, in the middle of a wide panel.
                width: Math.min(parent.width - 32, overview.readable)
                x: (parent.width - width) / 2
                spacing: 2

                Rectangle {
                    id: promptBox
                    objectName: "prompt" + turn.serial
                    visible: turn.prompt !== ""
                    width: parent.width
                    height: promptText.implicitHeight + 14
                    radius: theme.radius
                    color: Qt.alpha(theme.foreground, promptHover.hovered ? 0.1 : 0.07)
                    HoverHandler { id: promptHover }

                    Text {
                        id: promptText
                        x: 10
                        y: 7
                        width: parent.width - 20 - promptTime.width - 10
                        text: turn.prompt
                        textFormat: Text.PlainText
                        color: theme.foreground
                        wrapMode: Text.Wrap
                        maximumLineCount: turn.open ? 100000 : turn.compact ? 1 : 2
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
                    // Opens the turn: the whole prompt, all of its summary.
                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: overview.toggle("turn" + turn.serial)
                    }
                }

                Item { width: 1; height: 4; visible: turn.prompt !== "" }

                // The summary: its headline, then what else matters, quieter.
                // A click opens the points it sums up.
                Repeater {
                    model: turn.compact ? turn.summary.slice(0, 1) : turn.summary
                    delegate: Rectangle {
                        id: summaryRow
                        required property var modelData
                        required property int index
                        readonly property bool headline: index === 0
                        readonly property string visual: modelData.visual || ""
                        objectName: "summary" + turn.serial + "_0_" + index
                        width: column.width
                        height: summaryBody.implicitHeight + 6
                        radius: theme.radius
                        color: turn.detailed || summaryHover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                        HoverHandler { id: summaryHover }
                        Text {
                            x: 8
                            y: 3
                            width: 18
                            text: overview.summaryGlyph(summaryRow.modelData.kind)
                            color: overview.summaryTint(summaryRow.modelData.kind)
                            opacity: summaryRow.headline ? 1 : 0.7
                        }
                        Column {
                            id: summaryBody
                            x: 32
                            y: 3
                            width: parent.width - 32 - 8
                            spacing: 6
                            Text {
                                width: parent.width
                                text: summaryRow.modelData.text
                                textFormat: Text.PlainText
                                color: summaryRow.modelData.kind === "fail" ? theme.urgent
                                     : summaryRow.headline ? theme.foreground : overview.dim
                                font.bold: summaryRow.headline
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            // The model's drawing, when it made one: as drawn,
                            // sideways scrolling when wider than the overview.
                            Rectangle {
                                objectName: "visual" + turn.serial + "_0_" + summaryRow.index
                                visible: summaryRow.visual !== "" && !turn.compact
                                width: parent.width
                                height: visualText.implicitHeight + 16
                                radius: theme.radius
                                color: Qt.alpha(theme.foreground, 0.04)
                                border.width: 1
                                border.color: overview.line
                                Flickable {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    clip: true
                                    contentWidth: visualText.implicitWidth
                                    contentHeight: visualText.implicitHeight
                                    flickableDirection: Flickable.HorizontalFlick
                                    boundsBehavior: Flickable.StopAtBounds
                                    interactive: contentWidth > width
                                    Text {
                                        id: visualText
                                        objectName: "visualText" + turn.serial + "_0_" + summaryRow.index
                                        text: summaryRow.visual !== "" ? overview.drawing(summaryRow.visual) : ""
                                        textFormat: Text.StyledText
                                        color: theme.foreground
                                        wrapMode: Text.NoWrap
                                    }
                                }
                            }
                        }
                        TapHandler {
                            cursorShape: Qt.PointingHandCursor
                            onTapped: overview.toggle("details" + turn.serial)
                        }
                    }
                }

                // The points: those the summary covers once opened from it,
                // and those it doesn't cover yet (with a summary only the
                // newest, as what goes on now; without, the newest few).
                // Never all at once: "… N earlier points" shows 20 more a click.
                readonly property int pointsFrom: turn.detailed ? 0 : turn.shownCovered
                readonly property int pointsShown: (turn.detailed ? 12
                                                    : turn.compact ? (turn.summary.length > 0 ? 0 : 1)
                                                    : turn.summary.length > 0 ? 1 : overview.newest)
                                                   + 20 * (overview.opened["more" + turn.serial] || 0)
                readonly property int shownFrom: Math.max(pointsFrom, turn.items.length - pointsShown)
                Rectangle {
                    objectName: "earlier" + turn.serial
                    visible: column.shownFrom > column.pointsFrom && !turn.compact
                    x: turn.detailed ? 24 : 0
                    width: column.width - x
                    height: earlierText.implicitHeight + 6
                    radius: theme.radius
                    color: earlierHover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                    HoverHandler { id: earlierHover }
                    Text {
                        id: earlierText
                        x: 32
                        y: 3
                        text: "… " + qsTr("%n earlier points", "", column.shownFrom - column.pointsFrom)
                        color: overview.dim
                    }
                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: {
                            const next = Object.assign({}, overview.opened)
                            next["more" + turn.serial] = (next["more" + turn.serial] || 0) + 1
                            list.follow = false
                            overview.opened = next
                        }
                    }
                }
                // Your messages still waiting in the agent's queue, and its
                // questions waiting for you, wherever they are.
                Repeater {
                    model: turn.items.slice(column.pointsFrom, column.shownFrom).filter(point =>
                        point.state === "queued" || (point.kind === "question" && point.state === "running"))
                    delegate: PointRow {
                        required property var modelData
                        point: modelData
                        width: column.width
                    }
                }
                Repeater {
                    // The reply that asks shows as its question, below.
                    model: turn.items.slice(column.shownFrom, turn.question !== "" ? turn.items.length - 1 : turn.items.length)
                    delegate: PointRow {
                        required property var modelData
                        point: modelData
                        x: turn.detailed ? 24 : 0
                        width: column.width - x
                    }
                }
                // The question the turn ends with.
                Rectangle {
                    objectName: "question" + turn.serial
                    visible: turn.question !== ""
                    width: column.width
                    height: questionText.implicitHeight + 6
                    radius: theme.radius
                    color: "transparent"
                    Text {
                        x: 8
                        y: 3
                        width: 18
                        text: "\uf059"
                        color: theme.accent
                    }
                    Text {
                        id: questionText
                        x: 32
                        y: 3
                        width: parent.width - 32 - 8
                        text: turn.question
                        textFormat: Text.PlainText
                        color: theme.foreground
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Text {
        x: 24
        width: parent.width - 48
        y: (composer.y - height) / 2
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: text !== ""
        text: backend.selected === "" ? ""
            : !overview.claude ? qsTr("The overview follows Claude agents.")
            : timeline.state === "loading" ? ""
            : timeline.count === 0 ? qsTr("Nothing in this conversation yet.") : ""
        color: overview.dim
    }

    // A message to the agent: Enter sends it, Shift+Enter starts a new line.
    // Always the same height, so nothing above it moves as you type.
    Item {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: inputMetrics.lineSpacing * 3 + 34

        FontMetrics { id: inputMetrics }
        Rectangle { width: parent.width; height: 1; color: overview.line }
        Rectangle {
            id: field
            width: Math.min(parent.width - 32, overview.readable)
            x: (parent.width - width) / 2
            y: 12
            height: parent.height - 24
            radius: theme.radius
            color: "transparent"
            border.width: 1
            border.color: input.activeFocus ? Qt.alpha(theme.foreground, 0.35) : overview.line

            ScrollView {
                id: inputScroll
                x: 10
                y: 4
                width: parent.width - 20 - sendButton.width - 8
                height: parent.height - 8
                TextArea {
                    id: input
                    objectName: "overviewInput"
                    wrapMode: TextEdit.Wrap
                    color: theme.foreground
                    placeholderText: overview.asking ? qsTr("It asks you something: answer it in its terminal (Ctrl+Alt+B)")
                                                     : qsTr("Message to the agent · Enter sends, Shift+Enter starts a new line")
                    placeholderTextColor: overview.dim
                    selectionColor: Qt.alpha(theme.foreground, 0.35)
                    selectedTextColor: theme.foreground
                    background: null
                    padding: 0
                    topPadding: 4
                    bottomPadding: 4
                    enabled: backend.selected !== "" && !overview.asking
                    // The cursor in the text's color, blinking as in a terminal.
                    cursorDelegate: Rectangle {
                        width: 2
                        color: theme.foreground
                        visible: input.activeFocus && blink.shown
                        Timer {
                            id: blink
                            property bool shown: true
                            interval: 530
                            repeat: true
                            running: input.activeFocus
                            onRunningChanged: shown = true
                            onTriggered: shown = !shown
                        }
                        Connections {
                            target: input
                            function onCursorPositionChanged() {
                                blink.shown = true
                                blink.restart()
                            }
                        }
                    }
                    // A picture pasted goes as a file the agent can open.
                    Keys.onPressed: (event) => {
                        if (event.matches(StandardKey.Paste)) {
                            const path = backend.saveClipboardImage()
                            if (path !== "") {
                                input.insert(input.cursorPosition, path + " ")
                                event.accepted = true
                            }
                        }
                    }
                    Keys.onReturnPressed: (event) => {
                        if (event.modifiers & Qt.ShiftModifier)
                            event.accepted = false
                        else
                            overview.send()
                    }
                    Keys.onEnterPressed: (event) => overview.send()
                }
            }
            IconButton {
                id: sendButton
                objectName: "sendButton"
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                icon: ""
                hint: qsTr("Send it to the agent")
                shortcut: "Enter"
                enabled: input.text.trim() !== "" && backend.selected !== "" && !overview.asking
                         && (backend.busy === "" || backend.busy === "prompt")
                busy: backend.busy === "prompt"
                onClicked: overview.send()
            }
            // While it asks, a click here goes to its terminal.
            MouseArea {
                anchors.fill: parent
                z: 2
                enabled: overview.asking
                cursorShape: Qt.PointingHandCursor
                onClicked: overview.answerWanted()
            }
        }
    }
}
