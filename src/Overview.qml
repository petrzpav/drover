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
    // What went out to each agent, so only that is cleared once it went.
    property var sending: ({})
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
            const next = Object.assign({}, sending)
            next[backend.selected] = input.text.trim()
            sending = next
            backend.sendPrompt(backend.selected, next[backend.selected])
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
    // While the agent works, its robot bobs on what it does now: a step a
    // third of a second, not an animation redrawing every frame.
    readonly property bool working: backend.selectedStatus === "working"
    readonly property string robot: "\udb81\udea9"
    property bool beat: false
    Timer {
        interval: 330
        repeat: true
        running: overview.visible && (overview.working || timeline.summarizing)
        onTriggered: overview.beat = !overview.beat
        onRunningChanged: overview.beat = false
    }

    // Commands a reply gives you to run: its code blocks of one to three lines.
    function commandsIn(point) {
        if (!point || point.kind !== "said")
            return []
        const found = []
        const block = /```[^\n]*\n([\s\S]*?)```/g
        let match
        while ((match = block.exec(point.text)) !== null) {
            const lines = match[1].split("\n").filter(l => l.trim() !== "")
            if (lines.length > 0 && lines.length <= 3)
                found.push(match[1].replace(/\n$/, ""))
        }
        return found
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
        if (point.state === "running" || point.state === "queued" || point.state === "background")
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
        function onPromptSent(pane, went) {
            if (!went)
                return
            // What was sent goes; what you typed on since stays, here or kept.
            const sent = overview.sending[pane] || ""
            const rest = (text) => text.trim().startsWith(sent) ? text.trim().slice(sent.length).trim() : text
            if (pane === overview.draftPane) {
                input.text = rest(input.text)
                input.cursorPosition = input.length
            } else if (overview.drafts[pane] !== undefined) {
                const next = Object.assign({}, overview.drafts)
                next[pane] = rest(next[pane])
                overview.drafts = next
            }
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
        // Open whatever was clicked (the newest turn's last reply is).
        property bool forceOpen: false
        // The model sums this point up now (the first of those in view).
        property bool summing: false
        readonly property string key: "point" + point.id
        readonly property bool open: forceOpen || overview.opened[key] === true
        readonly property bool failed: point.state === "failed" || point.state === "denied"
        readonly property int more: point.steps.length - 1
        readonly property bool opens: !forceOpen && (open || more > 0 || pointText.truncated || detailText.truncated)
        objectName: key
        height: body.implicitHeight + 6
        radius: theme.radius
        color: hoverRow.hovered && opens ? Qt.alpha(theme.foreground, 0.05) : "transparent"
        HoverHandler { id: hoverRow }

        Text {
            readonly property bool busy: (row.point.state === "running" && overview.working) || row.summing
            x: 8
            y: 3 - (busy && overview.beat ? 2 : 0)
            width: 18
            text: busy ? overview.robot : overview.glyph(row.point.kind)
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
        HoverHandler {
            id: listHover
            onHoveredChanged: if (!hovered) Qt.callLater(list.settle)
        }

        // At the end, the list stays there as points come; scrolled up, it
        // stays where you are.
        property bool follow: true
        // Not while the pointer is over it: nothing moves under the pointer.
        function settle() {
            if (follow && !listHover.hovered)
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
            required property int summarizingFrom
            required property int summarizingTo
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
            // The newest turn: what it asks or gives you to run stays in sight.
            readonly property bool newest: index === list.count - 1
            required property bool ended
            // Its last point, Claude's own notes (a recap) aside.
            readonly property var lastPoint: {
                for (let i = items.length - 1; i >= 0; --i)
                    if (items[i].kind !== "recap" && items[i].kind !== "compact")
                        return items[i]
                return null
            }
            // The agent's last words once it stops, in full: what it found,
            // asks or offers is usually there, question mark or not.
            readonly property bool finalReply: newest && lastPoint !== null && lastPoint.kind === "said"
                                               && (ended || backend.selectedStatus !== "working")
            // Commands its last words give you to run: always in sight too.
            readonly property var commands: {
                if (!newest)
                    return []
                for (let i = items.length - 1; i >= 0; --i)
                    if (items[i].kind === "said")
                        return overview.commandsIn(items[i])
                return []
            }

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
                // The first point in view the model sums up now.
                readonly property int summingId: {
                    const at = Math.max(turn.summarizingFrom, shownFrom)
                    return turn.summarizing && turn.summarizingFrom >= 0 && at < turn.summarizingTo && at < turn.items.length
                           ? turn.items[at].id : -1
                }
                Rectangle {
                    objectName: "earlier" + turn.serial
                    visible: column.shownFrom > column.pointsFrom && (!turn.compact || turn.detailed)
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
                    model: turn.items.slice(0, column.shownFrom).filter(point =>
                        point.state === "queued" || point.state === "background"
                        || (point.background && point.state === "failed")
                        || (point.kind === "question" && point.state === "running"))
                    delegate: PointRow {
                        required property var modelData
                        point: modelData
                        width: column.width
                    }
                }
                Repeater {
                    model: turn.items.slice(turn.finalReply ? Math.min(column.shownFrom, turn.items.indexOf(turn.lastPoint))
                                                            : column.shownFrom)
                    delegate: PointRow {
                        required property var modelData
                        required property int index
                        point: modelData
                        forceOpen: turn.finalReply && modelData.id === turn.lastPoint.id
                        summing: modelData.id === column.summingId
                        x: turn.detailed ? 24 : 0
                        width: column.width - x
                    }
                }
                // At work with nothing running: it thinks.
                Rectangle {
                    objectName: "thinking" + turn.serial
                    visible: turn.newest && overview.working
                             && !turn.items.slice(column.shownFrom).some(point => point.state === "running")
                    width: column.width
                    height: thinkingText.implicitHeight + 6
                    color: "transparent"
                    Text {
                        x: 8
                        y: 3 - (overview.beat ? 2 : 0)
                        width: 18
                        text: overview.robot
                        color: theme.accent
                    }
                    Text {
                        id: thinkingText
                        x: 32
                        y: 3
                        text: qsTr("Thinking…")
                        color: overview.dim
                    }
                }

                // What it gives you to run: copied with a click.
                Repeater {
                    model: turn.compact ? [] : turn.commands
                    delegate: Rectangle {
                        id: command
                        required property string modelData
                        required property int index
                        objectName: "command" + turn.serial + "_" + index
                        property bool copied: false
                        width: column.width
                        height: Math.max(commandText.implicitHeight, copyButton.height) + 12
                        radius: theme.radius
                        color: Qt.alpha(theme.foreground, 0.04)
                        border.width: 1
                        border.color: overview.line
                        Text {
                            x: 8
                            y: 6
                            width: 18
                            text: "\uf120"
                            color: theme.accent
                        }
                        Text {
                            id: commandText
                            x: 32
                            y: 6
                            width: parent.width - 32 - copyButton.width - 12
                            text: command.modelData
                            textFormat: Text.PlainText
                            color: theme.foreground
                            wrapMode: Text.WrapAnywhere
                        }
                        IconButton {
                            id: copyButton
                            objectName: "copy" + turn.serial + "_" + command.index
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            icon: command.copied ? "\uf00c" : "\uf0c5"
                            tint: command.copied ? theme.success : theme.foreground
                            hint: qsTr("Copy")
                            onClicked: {
                                backend.copyText(command.modelData)
                                command.copied = true
                                copiedFade.restart()
                            }
                        }
                        Timer { id: copiedFade; interval: 1500; onTriggered: command.copied = false }
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
