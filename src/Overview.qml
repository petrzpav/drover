import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

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
    // The outline on the right, where the panel leaves room beside the text.
    readonly property int outlineWidth: 280
    // Room for it, and whether it shows (its button hides it).
    readonly property bool outlineFits: claude && width >= outlineWidth + 32 + 640
    readonly property bool outlined: outlineFits && !surface.outlineHidden
    // The text's room: left of the outline when it shows, then in its middle.
    readonly property real textRight: outlined ? outlineWidth + 32 : 0
    readonly property real textWidth: Math.min(width - textRight - 32, readable)
    readonly property real textX: (width - textRight - textWidth) / 2
    // The turn at the top of the view, for the outline to mark.
    // At the end, the newest; else the one at the top.
    readonly property int topTurn: {
        list.contentY; list.count; list.contentHeight
        if (list.atYEnd)
            return list.count - 1
        // The first turn in view, past a gap between two.
        for (let dy = 24; dy < list.height; dy += 12) {
            const at = list.indexAt(list.width / 2, list.contentY + dy)
            if (at >= 0)
                return at
        }
        return 0
    }
    // A page up or down (PgUp, PgDn), or to the start or the end.
    function page(step) {
        const top = list.originY
        const bottom = Math.max(top, list.originY + list.contentHeight - list.height)
        list.follow = false
        list.contentY = Math.max(top, Math.min(bottom, list.contentY + step * (list.height - 40)))
        list.follow = list.contentY >= bottom - 1
    }
    // Keys in text selected to copy: Ctrl+C copies it as written (without
    // the zero-width spaces that keep its Markdown safe), PgUp/PgDn and
    // Ctrl+Home/End page, and what you type goes on in the message field.
    function outputKeys(event, edit) {
        if (event.matches(StandardKey.Copy)) {
            if (edit.selectedText !== "")
                backend.copyText(edit.selectedText.replace(/\u200b/g, ""))
            event.accepted = true
        } else if ((event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) && event.modifiers === Qt.NoModifier) {
            page(event.key === Qt.Key_PageUp ? -1 : 1)
            event.accepted = true
        } else if (event.modifiers === Qt.ControlModifier && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
            toEnd(event.key === Qt.Key_End)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            focusInput()
            event.accepted = true
        } else if (event.text !== "" && event.text >= " " && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            focusInput()
            input.insert(input.cursorPosition, event.text)
            event.accepted = true
        }
    }
    // A theme switch: Markdown keeps the link color it was laid out with, so
    // the agent's words opened are laid out again.
    property bool relayout: false
    Connections {
        target: theme
        function onChanged() {
            overview.relayout = true
            Qt.callLater(() => overview.relayout = false)
        }
    }
    function toEnd(end) {
        list.follow = end
        if (end)
            list.positionViewAtEnd()
        else
            list.positionViewAtBeginning()
    }
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
        running: overview.visible && (overview.working || timeline.summarizing || timeline.backgroundRunning)
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
        // Another conversation: to its end, pointer or not, as it loads.
        function onSessionChanged() {
            overview.opened = ({})
            list.follow = true
            list.arriving = true
            arrived.restart()
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

    // A turn's category (the model sorts each turn into one) as a line icon
    // in its family's colour from the theme: blue building, green shipping,
    // red fixing and failing, yellow ideas and design, magenta questions,
    // cyan looking into things. Always the same sign for the same kind.
    readonly property var categoryIcons: ({
        idea: "M8 2.5a3.6 3.6 0 0 1 2.2 6.5c-.6.5-.9 1-.9 1.7v.6H6.7v-.6c0-.7-.3-1.2-.9-1.7A3.6 3.6 0 0 1 8 2.5zM6.6 13.5h2.8",
        build: "M5 2.5h6a2.5 2.5 0 0 1 2.5 2.5v6a2.5 2.5 0 0 1-2.5 2.5H5a2.5 2.5 0 0 1-2.5-2.5V5A2.5 2.5 0 0 1 5 2.5zM8 5.2v5.6M5.2 8h5.6",
        ui: "M3.5 3h9a1.5 1.5 0 0 1 1.5 1.5v7a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5v-7A1.5 1.5 0 0 1 3.5 3zM2 6h12M10 6v7",
        release: "M8 2c2.4 1.6 3.4 4 3.2 7L8 11.5 4.8 9C4.6 6 5.6 3.6 8 2zM5 9.5 3.2 12l2.6-.2M11 9.5l1.8 2.5-2.6-.2M8 11.5V14M7 6.3a1 1 0 1 0 2 0a1 1 0 1 0-2 0",
        fix: "M10.5 2.5a3 3 0 0 0-2.9 4L3 11.1a1.4 1.4 0 0 0 2 2l4.6-4.6a3 3 0 0 0 4-2.9l-1.8 1.8-1.8-.4-.4-1.8z",
        failure: "M8 2.5 14 13H2zM8 6.5v3M8 11.3v.2",
        review: "M3 7a4 4 0 1 0 8 0a4 4 0 1 0-8 0M10 10l3.5 3.5",
        research: "M2.5 8a5.5 5.5 0 1 0 11 0a5.5 5.5 0 1 0-11 0M10.5 5.5 9 9 5.5 10.5 7 7z",
        design: "M3 13l.6-2.8L10.4 3.4a1.4 1.4 0 0 1 2 2L5.6 12.2zM9.4 4.4l2 2",
        question: "M2.5 8a5.5 5.5 0 1 0 11 0a5.5 5.5 0 1 0-11 0M6.3 6.3a1.8 1.8 0 1 1 2.5 1.6c-.5.2-.8.6-.8 1.1v.4M8 11.3v.2",
        test: "M6 2.5h4M6.7 2.5v4L3.5 12.3A1 1 0 0 0 4.4 13.8h7.2a1 1 0 0 0 .9-1.5L9.3 6.5v-4",
        perf: "M9 2 4 9h4l-1 5 5-7H8z"
    })
    function categoryColor(category) {
        const palette = theme.terminalPalette
        const named = index => palette.length > index ? palette[index] : theme.accent
        switch (category) {
        case "release": return theme.success
        case "fix": case "failure": case "perf": return theme.urgent
        case "idea": case "design": return named(3)
        case "question": return named(5)
        case "review": case "research": return named(6)
        }
        return named(4)
    }
    component CategoryIcon: Shape {
        id: icon
        property string category: ""
        width: 16
        height: 16
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: overview.categoryColor(icon.category)
            strokeWidth: 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: overview.categoryIcons[icon.category] || "" }
        }
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
                active: row.open && row.point.kind === "said" && !overview.relayout
                visible: active
                width: parent.width
                // Selectable, to copy from.
                sourceComponent: TextEdit {
                    id: words
                    objectName: "pointWords" + row.point.id
                    Keys.onPressed: (event) => overview.outputKeys(event, words)
                    width: body.width
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.MarkdownText
                    text: overview.markdown(row.point.text)
                    color: theme.foreground
                    selectionColor: Qt.alpha(theme.foreground, 0.35)
                    selectedTextColor: theme.foreground
                    // Links in the theme's colors: see Theme::applyLinkColor.
                    wrapMode: TextEdit.Wrap
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
        // Not while the pointer is over it: nothing moves under the pointer,
        // except while another conversation comes in.
        property bool arriving: false
        function settle() {
            if (follow && (!listHover.hovered || arriving))
                positionViewAtEnd()
        }
        // Loaded and drawn: from then on the pointer holds it again.
        Timer {
            id: arrived
            interval: 1200
            onTriggered: {
                if (timeline.state !== "ready" && timeline.state !== "none" && timeline.state !== "missing") {
                    restart()
                    return
                }
                list.settle()
                list.arriving = false
            }
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
                width: overview.textWidth
                x: overview.textX
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
                                    TextEdit {
                                        id: visualText
                                        Keys.onPressed: (event) => overview.outputKeys(event, visualText)
                                        objectName: "visualText" + turn.serial + "_0_" + summaryRow.index
                                        text: summaryRow.visual !== "" ? overview.drawing(summaryRow.visual) : ""
                                        textFormat: TextEdit.RichText
                                        readOnly: true
                                        selectByMouse: true
                                        color: theme.foreground
                                        selectionColor: Qt.alpha(theme.foreground, 0.35)
                                        selectedTextColor: theme.foreground
                                        wrapMode: TextEdit.NoWrap
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
                        TextEdit {
                            id: commandText
                            Keys.onPressed: (event) => overview.outputKeys(event, commandText)
                            x: 32
                            y: 6
                            width: parent.width - 32 - copyButton.width - 12
                            text: command.modelData
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            color: theme.foreground
                            selectionColor: Qt.alpha(theme.foreground, 0.35)
                            selectedTextColor: theme.foreground
                            wrapMode: TextEdit.WrapAnywhere
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

    // The outline as a metro line: a station per turn (its time and
    // headline), a release an interchange, a review a branch that comes
    // back, a failure a station out of service; at its end, now. The station
    // in view is marked; a click goes to it.
    FontMetrics { id: metroMetrics }
    readonly property real metroIndent: metroMetrics.advanceWidth("00:00 ")
    function metroOf(items, batches) {
        let version = "", failed = false, lastWork = null
        const reviews = [], running = []
        for (let i = items.length - 1; i >= 0; --i) {
            const point = items[i]
            // The version a release, a tag or a "Release x.y" commit made.
            if (point.release && version === "")
                version = point.release
            if (point.kind === "interrupted")
                failed = true
            // What still runs in the background, all of it; and the newest
            // two side trips besides.
            if (point.state === "background")
                running.unshift(point.text)
            else if (point.branch && reviews.length < 2 && reviews.indexOf(point.text) < 0)
                reviews.unshift(point.text)
            if (lastWork === null && (point.kind === "command" || point.kind === "milestone"
                                      || point.kind === "tool" || point.kind === "web"))
                lastWork = point
        }
        // Out of service: its last step failed (not one retried after), it was
        // interrupted, or its headline is a failure (a failure along the way,
        // in a later point, doesn't stop the line).
        const summary = batches.length > 0 ? batches[batches.length - 1].points : []
        failed = failed || (lastWork !== null && (lastWork.state === "failed" || lastWork.state === "denied"))
                 || (summary.length > 0 && summary[0].kind === "fail")
        const last = batches.length > 0 ? batches[batches.length - 1] : null
        return { version: version, failed: failed, reviews: reviews, running: running,
                 category: last && last.category && overview.categoryIcons[last.category] ? last.category : "",
                 headline: summary.length > 0 ? summary[0].text : "" }
    }

    ListView {
        id: outline
        objectName: "outline"
        visible: overview.outlined
        x: overview.width - width - 16
        width: overview.outlineWidth
        anchors.top: parent.top
        anchors.topMargin: 52
        anchors.bottom: composer.top
        anchors.bottomMargin: 8
        clip: true
        model: overview.outlined ? timeline : null
        spacing: 0
        HoverHandler { id: outlineHover }
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        // The turn in view stays in the outline's view.
        Connections {
            target: overview
            function onTopTurnChanged() {
                // Not under the pointer, or while it moves.
                if (!outlineHover.hovered && !outline.moving)
                    outline.positionViewAtIndex(overview.topTurn, ListView.Contain)
            }
        }

        delegate: Item {
            id: entry
            required property int index
            required property string prompt
            required property var time
            required property var items
            required property var batches
            readonly property bool current: index === overview.topTurn
            readonly property bool newest: index === outline.count - 1
            readonly property var metro: overview.metroOf(items, batches)
            // The station in words: a release by its version, else the
            // headline, else the prompt.
            readonly property string headline: metro.headline
            objectName: "outline" + index
            width: outline.width - 8
            height: stops.implicitHeight

            Column {
                id: stops
                width: parent.width
                Item {
                    id: station
                    objectName: "station" + entry.index
                    width: parent.width
                    height: Math.max(stationText.implicitHeight + 4, 20)
                    HoverHandler { id: stationHover }
                    // The station in view, marked.
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -4
                        anchors.rightMargin: -4
                        radius: theme.radius
                        color: entry.current ? Qt.alpha(theme.foreground, 0.08)
                             : stationHover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                    }
                    Text {
                        y: 2
                        text: entry.time ? Qt.formatTime(entry.time, "HH:mm") : "     "
                        color: overview.dim
                    }
                    // The turn's category, on a plain station: a release, a
                    // failure and the robot keep their marks.
                    CategoryIcon {
                        id: stationIcon
                        objectName: "stationIcon" + entry.index
                        category: entry.metro.category
                        visible: category !== "" && !stationMark.busy && !entry.metro.failed && entry.metro.version === ""
                        // Centred on the line, on whole pixels.
                        x: Math.round(overview.metroIndent + metroMetrics.advanceWidth("│") / 2 - width / 2)
                        y: Math.round((parent.height - height) / 2)
                    }
                    Text {
                        id: stationMark
                        objectName: "stationMark" + entry.index
                        visible: !stationIcon.visible
                        x: overview.metroIndent
                        // The robot on the station it works at.
                        readonly property bool busy: entry.newest && overview.working
                        y: 2 - (busy && overview.beat ? 2 : 0)
                        text: busy ? overview.robot : entry.metro.failed ? "✗" : entry.metro.version !== "" ? "◉" : "●"
                        color: busy ? theme.accent : entry.metro.failed ? theme.urgent
                             : entry.metro.version !== "" ? theme.success : (entry.current ? theme.foreground : overview.dim)
                    }
                    Text {
                        id: stationText
                        objectName: "stationText" + entry.index
                        x: overview.metroIndent + Math.max(metroMetrics.advanceWidth("● "), 24)
                        y: 2
                        width: parent.width - x
                        text: entry.metro.version !== "" ? "═══ " + entry.metro.version
                            : entry.headline !== "" ? entry.headline
                            : entry.prompt !== "" ? entry.prompt : qsTr("Earlier")
                        textFormat: Text.PlainText
                        font.bold: entry.metro.version !== "" || entry.current
                        color: entry.metro.version !== "" ? theme.success
                             : entry.metro.failed ? theme.urgent
                             : entry.current ? theme.foreground : Qt.alpha(theme.foreground, 0.8)
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: {
                            // The newest goes back to following the end.
                            list.follow = entry.newest
                            if (entry.newest)
                                list.positionViewAtEnd()
                            else
                                list.positionViewAtIndex(entry.index, ListView.Beginning)
                        }
                    }
                }
                // What it left running in the background, the robot at work on it.
                Repeater {
                    model: entry.metro.running
                    delegate: Item {
                        required property string modelData
                        required property int index
                        objectName: "running" + entry.index + "_" + index
                        x: overview.metroIndent
                        width: stops.width - x
                        height: runningText.implicitHeight
                        Text {
                            id: runningBranch
                            text: "│ └"
                            color: theme.accent
                        }
                        Text {
                            id: runningRobot
                            x: runningBranch.implicitWidth
                            y: overview.beat ? -2 : 0
                            text: overview.robot
                            color: theme.accent
                        }
                        Text {
                            id: runningText
                            x: runningRobot.x + runningRobot.implicitWidth + 4
                            width: parent.width - x
                            text: modelData
                            textFormat: Text.PlainText
                            color: theme.accent
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                }
                // Its reviews, branches that come back.
                Repeater {
                    model: entry.metro.reviews
                    delegate: Text {
                        required property string modelData
                        x: overview.metroIndent
                        width: stops.width - x
                        text: "│ └◇ " + modelData
                        textFormat: Text.PlainText
                        color: theme.accent
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                // The line on to the next station, or on to now.
                Text {
                    x: overview.metroIndent
                    text: entry.newest ? "┆" : "│"
                    color: overview.dim
                }
                Text {
                    visible: entry.newest
                    x: overview.metroIndent
                    text: "▼ " + qsTr("now")
                    color: theme.accent
                }
            }
        }
    }

    // The outline's own button, in the same place whether it shows or not.
    IconButton {
        objectName: "outlineButton"
        visible: overview.outlineFits
        x: overview.width - width - 16
        y: 12
        icon: "\uf03a"
        tint: overview.outlined ? theme.foreground : overview.dim
        hint: overview.outlined ? qsTr("Hide the outline") : qsTr("Show the outline")
        onClicked: surface.outlineHidden = !surface.outlineHidden
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
            width: overview.textWidth
            x: overview.textX
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
                    // A picture pasted goes as a file the agent can open; PgUp and
                    // PgDn page the conversation, Ctrl+Home and Ctrl+End go to
                    // its start and end.
                    Keys.onPressed: (event) => {
                        if ((event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) && event.modifiers === Qt.NoModifier) {
                            overview.page(event.key === Qt.Key_PageUp ? -1 : 1)
                            event.accepted = true
                            return
                        }
                        if (event.modifiers === Qt.ControlModifier && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                            overview.toEnd(event.key === Qt.Key_End)
                            event.accepted = true
                            return
                        }
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
