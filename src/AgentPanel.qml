import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open panel: every agent of the herdr session on the left, and on the
// right a real terminal attached to the selected agent, where you talk to it
// as in herdr itself. Drawn like an Omarchy shell popup: the theme background
// and the Hyprland active border, in the terminal's font (the application
// font, set in main.cpp). Everything has a fixed size, and messages lie over
// the content, so nothing shifts as agents change state.
Rectangle {
    id: panel
    property alias picker: workspacePicker
    property bool helpVisible: false
    // The panel is gliding to another size: the terminals wait for its end.
    property bool morphing: false
    // The agent list on the left (Ctrl+B); hidden, the terminal takes its
    // room. With no agent picked there is nothing else to look at, so the
    // list shows anyway.
    // Remembered across starts, like the full screen.
    readonly property bool sidebarShown: !surface.listHidden
    readonly property bool listShown: sidebarShown || backend.selected === ""
    // An agent gone from under a hidden list leaves the list shown, so a
    // pick from it doesn't make it vanish under the pointer.
    Connections {
        target: backend
        function onSelectedChanged() { if (backend.selected === "") surface.listHidden = false }
    }
    // Over the whole screen (Super+Alt+H); the window decides.
    property bool fullscreen: false
    // Whether the terminal attaches: only while the panel is open, so the
    // pane has herdr's own size the rest of the time.
    property bool attached: false
    // Bumped to attach again after the terminal was detached.
    property int attachRound: 0
    // The terminal in front: the agent's, or its editor's. Both stay attached
    // while the panel is open, so switching only shows the other at once.
    readonly property Item editorTerminal: editorTerminals.count > 0 ? editorTerminals.itemAt(0) : null
    readonly property Item shellTerminal: shellTerminals.count > 0 ? shellTerminals.itemAt(0) : null
    readonly property bool shellFocused: shellTerminal !== null && shellTerminal.terminalFocused
    // The newest agent terminal: the one being switched to.
    readonly property Item agentTerminal: terminals.count > 0 ? terminals.itemAt(terminals.count - 1) : null
    readonly property Item terminal: !backend.editing ? agentTerminal
                                   : backend.shellShown && shellFocused ? shellTerminal : editorTerminal
    readonly property bool terminalFocused: terminal !== null && terminal.terminalFocused

    // The terminal claims every key it gets, Esc included, so help takes the
    // keyboard while it is shown and hands it back after.
    onHelpVisibleChanged: {
        if (helpVisible)
            panel.forceActiveFocus()
        else if (attached)
            focusTerminal()
    }

    // Ctrl and a letter for the terminal, for a key the desktop took first.
    function sendControl(letter) {
        if (terminal)
            terminal.sendControl(letter)
    }
    function focusTerminal() {
        if (terminal)
            terminal.focusTerminal()
        else
            panel.forceActiveFocus()
    }
    signal quitRequested()
    signal fullscreenToggled()
    // The user picked an agent; Main decides where the keyboard goes.
    signal agentPicked()

    readonly property color line: Qt.alpha(theme.foreground, 0.12)
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    readonly property int sidebarWidth: 280
    readonly property int overviewWidth: 360
    // A narrow panel's header keeps to the dot and icons, so it never runs
    // past the panel's edge.
    readonly property bool compactHeader: width - (listShown ? sidebarWidth : 0) < 640
    readonly property int headerHeight: 48

    color: theme.background
    radius: theme.radius
    border.width: 2
    border.color: theme.border

    // The overview follows the selected Claude agent while it shows.
    Binding { target: timeline; property: "session"; value: backend.selectedKind === "claude" ? backend.selectedSession : "" }
    Binding { target: timeline; property: "active"; value: panel.attached && surface.overviewShown && backend.selected !== "" }

    function statusLabel(status) {
        switch (status) {
        case "working": return qsTr("working")
        case "blocked": return qsTr("asks you")
        case "done": return qsTr("done")
        case "idle": return qsTr("idle")
        }
        return qsTr("unknown")
    }

    function statusColor(status) {
        return status === "blocked" ? theme.urgent : status === "working" ? theme.accent
             : status === "done" ? theme.success : panel.dim
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 0

        // Sidebar: the agents.
        ColumnLayout {
            visible: panel.listShown
            Layout.preferredWidth: panel.sidebarWidth
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0

            Text {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.headerHeight
                Layout.leftMargin: listButton.x + listButton.width + 6
                Layout.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%n agents", "", backend.agents.count)
                color: theme.foreground
                elide: Text.ElideRight
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panel.line }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 2
                    model: backend.agents
                    currentIndex: backend.selectedIndex
                    boundsBehavior: Flickable.StopAtBounds
                    highlightMoveDuration: 0
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    // A mouse drag moves an agent, never the list: the wheel
                    // and the scroll bar scroll it.
                    acceptedButtons: Qt.NoButton

                    // Dragging a row moves its agent: the row follows the
                    // pointer within the list, the rows it passes make room,
                    // the list scrolls at its edges, and on release the agent
                    // takes that place (kept for the next start) as the row
                    // glides into it. The drag follows the agent, not a row
                    // number, so a poll changing the list mid-drag can't
                    // hand it to another row.
                    property string dragPane: ""
                    readonly property int dragFrom: { backend.agents.rows; return dragPane === "" ? -1 : backend.agents.indexOf(dragPane) }
                    property int dragTo: -1
                    property real dragDy: 0
                    property real dragStep: 1
                    // Where the drag started: the handler's own translation
                    // and the list's scroll, so the row moves from where it is.
                    property real dragStartTranslation: 0
                    property real dragStartContentY: 0
                    property real dragTranslation: 0
                    property real dragPointerY: -1
                    // Set while the dropped list lands, so the rows that made
                    // room don't glide back.
                    property bool settling: false
                    // The dropped row's way into its place.
                    property string dropPane: ""
                    property real dropOffset: 0
                    NumberAnimation {
                        id: dropGlide
                        target: list; property: "dropOffset"; to: 0
                        duration: 120; easing.type: Easing.OutCubic
                        onFinished: list.dropPane = ""
                    }

                    function shiftFor(i) {
                        if (dragFrom < 0 || i === dragFrom)
                            return 0
                        if (dragFrom < dragTo && i > dragFrom && i <= dragTo)
                            return -dragStep
                        if (dragFrom > dragTo && i >= dragTo && i < dragFrom)
                            return dragStep
                        return 0
                    }
                    function updateDrag() {
                        if (dragFrom < 0)
                            return
                        const dy = dragTranslation - dragStartTranslation + contentY - dragStartContentY
                        dragDy = Math.max(-dragFrom * dragStep, Math.min((count - 1 - dragFrom) * dragStep, dy))
                        dragTo = dragFrom + Math.round(dragDy / dragStep)
                    }
                    function startDrag(paneId, translation, step) {
                        dropGlide.stop()
                        dropPane = ""
                        dragStep = step
                        dragStartTranslation = translation
                        dragTranslation = translation
                        dragStartContentY = contentY
                        dragDy = 0
                        // Its place first, so no row moves for a moment.
                        dragTo = backend.agents.indexOf(paneId)
                        dragPane = paneId
                    }
                    function cancelDrag() {
                        settling = true
                        dragPane = ""
                        dragTo = -1
                        dragDy = 0
                        dragPointerY = -1
                        Qt.callLater(() => list.settling = false)
                    }
                    function drop() {
                        if (dragPane === "")
                            return
                        const pane = dragPane, from = dragFrom, to = dragTo
                        const residual = dragDy - (to - from) * dragStep
                        settling = true
                        if (from >= 0 && to >= 0 && to !== from)
                            backend.moveAgent(pane, to)
                        dropPane = pane
                        dropOffset = residual
                        dragPane = ""
                        dragTo = -1
                        dragDy = 0
                        dragPointerY = -1
                        dropGlide.restart()
                        Qt.callLater(() => list.settling = false)
                    }
                    // The dragged agent ended, or one above it came or went:
                    // the drag carries on from the row's new place, or ends.
                    property int lastDragFrom: -1
                    onDragFromChanged: {
                        if (dragPane !== "" && dragFrom < 0) {
                            // After this change has settled, not inside it.
                            Qt.callLater(() => { if (list.dragPane !== "" && list.dragFrom < 0) list.cancelDrag() })
                        } else if (dragFrom >= 0 && lastDragFrom >= 0 && dragFrom !== lastDragFrom) {
                            dragStartTranslation -= (dragFrom - lastDragFrom) * dragStep
                            updateDrag()
                        }
                        lastDragFrom = dragFrom
                    }
                    onContentYChanged: updateDrag()
                    // Hidden mid-drag (Ctrl+B, the panel closed): nothing moves.
                    onVisibleChanged: if (!visible && dragPane !== "") cancelDrag()
                    // Near an edge, the list scrolls on, and the row with it.
                    Timer {
                        interval: 16
                        repeat: true
                        running: list.dragPane !== "" && list.dragPointerY >= 0
                                 && (list.dragPointerY < 28 || list.dragPointerY > list.height - 28)
                        onTriggered: {
                            const step = list.dragPointerY < 28 ? -8 : 8
                            list.contentY = Math.max(list.originY, Math.min(list.originY + list.contentHeight - list.height,
                                                                            list.contentY + step))
                        }
                    }

                    delegate: AgentRow {
                        id: agentRow
                        objectName: "agentRow_" + paneId
                        width: ListView.view.width
                        selected: paneId === backend.selected
                        statusText: panel.statusLabel(status)
                        onPicked: { backend.select(paneId); panel.agentPicked() }
                        onOpened: { backend.select(paneId); backend.jump() }

                        readonly property bool dragged: paneId === list.dragPane
                        readonly property bool dropping: paneId === list.dropPane
                        lifted: dragged || dropping
                        property real offset: dragged ? list.dragDy : dropping ? list.dropOffset : list.shiftFor(index)
                        Behavior on offset {
                            enabled: !list.settling && !agentRow.dragged && !agentRow.dropping && list.dragPane !== ""
                            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                        }
                        transform: Translate { y: agentRow.offset }
                        z: lifted ? 2 : 0
                        DragHandler {
                            target: null
                            xAxis.enabled: false
                            // A single agent has nowhere to go.
                            enabled: list.count > 1
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            cursorShape: Qt.ClosedHandCursor
                            onActiveChanged: {
                                if (active)
                                    list.startDrag(agentRow.paneId, activeTranslation.y, agentRow.height + list.spacing)
                                else if (agentRow.dragged)
                                    list.drop()
                            }
                            onActiveTranslationChanged: {
                                if (!active || !agentRow.dragged)
                                    return
                                list.dragTranslation = activeTranslation.y
                                list.dragPointerY = list.mapFromItem(null, centroid.scenePosition).y
                                list.updateDrag()
                            }
                            // The grab taken away (not a release): nothing moves.
                            onCanceled: if (agentRow.dragged) list.cancelDrag()
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    visible: list.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: backend.error !== "" ? backend.error : qsTr("No agents are running in herdr.")
                    color: backend.error !== "" ? theme.urgent : panel.dim
                }

                // herdr stopped answering, but the last list is still shown:
                // say so over its bottom.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    visible: backend.error !== "" && list.count > 0
                    height: staleText.implicitHeight + 16
                    radius: theme.radius
                    color: theme.background
                    border.width: 1
                    border.color: theme.urgent
                    Text {
                        id: staleText
                        anchors.centerIn: parent
                        width: parent.width - 20
                        wrapMode: Text.Wrap
                        text: qsTr("Not up to date. %1").arg(backend.error)
                        color: theme.urgent
                    }
                }
            }
        }

        Rectangle { visible: panel.listShown; Layout.fillHeight: true; Layout.preferredWidth: 1; color: panel.line }

        // The conversation with the selected agent.
        ColumnLayout {
            id: conversation
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.headerHeight
                // Clear of the list's button while the list is hidden.
                Layout.leftMargin: panel.listShown ? 16 : listButton.x + listButton.width + 10
                Layout.rightMargin: 8
                spacing: 10

                StatusDot { opacity: backend.selected !== "" ? 1 : 0; status: backend.selectedStatus }
                // The title as long as it fits, its state right after it.
                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    text: backend.selectedTitle
                    color: theme.foreground
                    elide: Text.ElideRight
                }
                // Quiet, in a fixed width, so the title never re-elides; the dot
                // carries the state's color.
                Text {
                    visible: backend.selected !== "" && !panel.compactHeader
                    Layout.preferredWidth: Math.max(...["working", "blocked", "done", "idle", "unknown"].map(
                        s => statusMetrics.advanceWidth("·  " + panel.statusLabel(s))))
                    text: "·  " + panel.statusLabel(backend.selectedStatus)
                    color: panel.dim
                    FontMetrics { id: statusMetrics }
                }
                Item { Layout.fillWidth: true }
                // The agent or the editor beside it, in its herdr tab: two
                // segments in a frame, the current one filled with the accent.
                Rectangle {
                    objectName: "viewSwitch"
                    implicitWidth: segments.implicitWidth + 4
                    implicitHeight: 30
                    radius: theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: panel.line
                    opacity: backend.selected !== "" ? 1 : 0.4

                    // Both segments are as wide as the wider one, so the
                    // highlight only glides, in step with the labels' colors.
                    readonly property real segmentWidth: segmentButtons.count === 2
                        ? Math.max(segmentButtons.itemAt(0).implicitWidth, segmentButtons.itemAt(1).implicitWidth) : 0
                    // A quiet fill: the accent stays the agents' "working".
                    Rectangle {
                        x: segments.x + (backend.editing ? parent.segmentWidth : 0)
                        y: 2
                        width: parent.segmentWidth
                        height: parent.height - 4
                        radius: Math.max(0, theme.radius - 2)
                        color: Qt.alpha(theme.foreground, 0.12)
                        Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    }
                    Row {
                        id: segments
                        x: 2
                        y: 2
                        Repeater {
                            id: segmentButtons
                            model: [{ view: "agent", icon: "󰚩", label: qsTr("agent") },
                                    { view: "editor", icon: "", label: qsTr("editor") }]
                            delegate: IconButton {
                                required property var modelData
                                readonly property bool current: (modelData.view === "editor") === backend.editing
                                width: segments.parent.segmentWidth
                                implicitHeight: 26
                                icon: modelData.icon
                                label: panel.compactHeader ? "" : modelData.label
                                tint: current ? theme.foreground : panel.dim
                                hint: modelData.view === "editor" ? qsTr("Edit files beside the agent") : qsTr("Back to the agent")
                                shortcut: "Alt+E"
                                enabled: backend.selected !== ""
                                busy: modelData.view === "editor" && backend.busy === "editor"
                                onClicked: if (!current) backend.toggleEditor()
                            }
                        }
                    }
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Open the agent in herdr")
                    shortcut: "Ctrl+Shift+O"
                    enabled: backend.selected !== ""
                    busy: backend.busy === "open"
                    onClicked: backend.jump()
                }
                IconButton {
                    objectName: "fullscreenButton"
                    icon: panel.fullscreen ? "" : ""
                    hint: qsTr("Put the panel over the whole screen, and back")
                    shortcut: "Super+Alt+H"
                    onClicked: panel.fullscreenToggled()
                }
                IconButton {
                    objectName: "overviewButton"
                    icon: "\uf0ca"
                    hint: qsTr("Show or hide the overview of the conversation")
                    shortcut: "Ctrl+Alt+B"
                    tint: surface.overviewShown ? theme.foreground : panel.dim
                    enabled: backend.selected !== ""
                    onClicked: surface.overviewShown = !surface.overviewShown
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Keyboard shortcuts")
                    shortcut: "F1"
                    tint: panel.helpVisible ? theme.foreground : panel.dim
                    onClicked: panel.helpVisible = !panel.helpVisible
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Quit drover")
                    shortcut: "Ctrl+Q"
                    onClicked: panel.quitRequested()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panel.line }

            // The terminal, and the overview of the conversation beside it.
            Item {
                id: conversationBody
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property bool overviewShown: surface.overviewShown && backend.selected !== ""
                // Too narrow for both, the overview lies over the terminal's
                // right side, and the terminal keeps its width.
                readonly property bool overviewBeside: width >= panel.overviewWidth + 480

                // The terminal, attached to the selected agent.
                Item {
                    id: terminalArea
                    objectName: "terminalArea"
                    width: conversationBody.width
                           - (conversationBody.overviewShown && conversationBody.overviewBeside ? panel.overviewWidth : 0)
                    height: conversationBody.height

                    property bool detached: false
                    // The terminals' size: while the panel glides to another size
                    // they keep theirs, clipped, and take the new one once at the
                    // end, so the agent's screen is redrawn once, not every frame.
                    clip: true
                    property real areaWidth: 0
                    property real areaHeight: 0
                    Binding {
                        target: terminalArea; property: "areaWidth"; value: terminalArea.width
                        when: !panel.morphing; restoreMode: Binding.RestoreNone
                    }
                    Binding {
                        target: terminalArea; property: "areaHeight"; value: terminalArea.height
                        when: !panel.morphing; restoreMode: Binding.RestoreNone
                    }
                    Connections {
                        target: backend
                        function onSelectedChanged() { terminalArea.detached = false }
                        // The other view comes to the front with the keyboard.
                        function onEditorChanged() {
                            if (panel.attached && !panel.helpVisible && !panel.picker.open)
                                Qt.callLater(panel.focusTerminal)
                        }
                        // The terminal takes the keyboard as it shows, the editor as it hides.
                        function onShellChanged() {
                            if (!panel.attached || panel.helpVisible || panel.picker.open)
                                return
                            Qt.callLater(() => {
                                if (backend.shellShown && panel.shellTerminal)
                                    panel.shellTerminal.focusTerminal()
                                else if (panel.editorTerminal)
                                    panel.editorTerminal.focusTerminal()
                            })
                        }
                    }
                    // The editor's side, split when its terminal shows: the editor
                    // above, the terminal below over the whole width.
                    readonly property real shellHeight: backend.editing && backend.shellShown
                        ? Math.round(terminalArea.areaHeight * 0.35) : 0

                    // Switching agents keeps the previous terminal on screen until
                    // the new one has drawn, so nothing flashes blank.
                    readonly property string agentKey: panel.attached && !terminalArea.detached && backend.selectedTerminal !== ""
                        && backend.herdrPath !== "" ? backend.selectedTerminal + "#" + panel.attachRound : ""
                    onAgentKeyChanged: {
                        if (agentKey === "") {
                            agentTerminals.clear()
                            return
                        }
                        if (agentTerminals.count > 0 && agentTerminals.get(agentTerminals.count - 1).key === agentKey)
                            return
                        while (agentTerminals.count > 1)
                            agentTerminals.remove(0)
                        agentTerminals.append({ key: agentKey })
                    }
                    function dropOlderAgentTerminals() {
                        while (agentTerminals.count > 1)
                            agentTerminals.remove(0)
                    }
                    ListModel { id: agentTerminals }
                    Repeater {
                        id: terminals
                        model: agentTerminals
                        delegate: AgentTerminal {
                            required property string key
                            required property int index
                            readonly property bool newest: index === agentTerminals.count - 1
                            terminalId: key.split("#")[0]
                            // While the editor is still drawing, the agent stays in view.
                            visible: !backend.editing || !(panel.editorTerminal && panel.editorTerminal.ready)
                            opacity: !newest || ready ? 1 : 0
                            width: terminalArea.areaWidth
                            height: terminalArea.areaHeight
                            onReadyChanged: if (ready && newest) terminalArea.dropOlderAgentTerminals()
                            onDetached: if (newest) terminalArea.detached = true
                            Component.onCompleted: if (panel.attached && !backend.editing && !panel.picker.open && !panel.helpVisible) focusTerminal()
                        }
                    }
                    Repeater {
                        id: editorTerminals
                        model: panel.attached && backend.editorTerminal !== "" && backend.herdrPath !== ""
                               ? [backend.editorTerminal + "#" + panel.attachRound] : []
                        delegate: AgentTerminal {
                            required property string modelData
                            objectName: "editorTerminal"
                            terminalId: modelData.split("#")[0]
                            editor: true
                            visible: backend.editing
                            opacity: ready ? 1 : 0
                            width: terminalArea.areaWidth
                            height: terminalArea.areaHeight - (terminalArea.shellHeight > 0 ? terminalArea.shellHeight + 1 : 0)
                            // An editor that quits closes its pane: back to the agent.
                            onDetached: backend.editorClosed()
                            Component.onCompleted: if (panel.attached && visible && !panel.picker.open && !panel.helpVisible) focusTerminal()
                        }
                    }
                    Rectangle {
                        visible: terminalArea.shellHeight > 0
                        y: terminalArea.areaHeight - terminalArea.shellHeight - 1
                        width: terminalArea.areaWidth
                        height: 1
                        color: panel.line
                    }
                    Repeater {
                        id: shellTerminals
                        model: panel.attached && backend.shellTerminal !== "" && backend.herdrPath !== ""
                               ? [backend.shellTerminal + "#" + panel.attachRound] : []
                        delegate: AgentTerminal {
                            required property string modelData
                            objectName: "shellTerminal"
                            terminalId: modelData.split("#")[0]
                            shell: true
                            // Hidden, it keeps its size, so its pane isn't squeezed.
                            visible: backend.editing && terminalArea.shellHeight > 0
                            y: terminalArea.areaHeight - height
                            width: terminalArea.areaWidth
                            height: Math.round(terminalArea.areaHeight * 0.35)
                            // Its shell exited: the terminal is gone.
                            onDetached: backend.shellClosed()
                            Component.onCompleted: if (panel.attached && visible && !panel.picker.open && !panel.helpVisible) focusTerminal()
                        }
                    }

                }

                Overview {
                    id: overview
                    objectName: "overview"
                    x: conversationBody.width - width
                    width: panel.overviewWidth
                    height: conversationBody.height
                    visible: conversationBody.overviewShown
                    statusLabel: panel.statusLabel
                }

                // Why there is no terminal, in the terminal's middle: on a card
                // above the overview where that lies over the terminal.
                Rectangle {
                    anchors.centerIn: terminalArea
                    width: Math.min(terminalArea.width - 32, 440)
                    height: message.implicitHeight + 24
                    visible: panel.terminal === null
                    radius: theme.radius
                    color: theme.background
                    border.width: conversationBody.overviewShown && !conversationBody.overviewBeside ? 1 : 0
                    border.color: panel.line
                    Column {
                        id: message
                        anchors.centerIn: parent
                        width: parent.width - 24
                        spacing: 12
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: backend.herdrPath === "" ? qsTr("herdr is not installed.")
                                : terminalArea.detached ? qsTr("The terminal was detached from the agent.")
                                : backend.selectionLost ? qsTr("The agent you were talking to is gone. Pick another one.")
                                : list.count > 0 ? qsTr("Pick an agent to talk to it.") : ""
                            color: panel.dim
                        }
                        IconButton {
                            objectName: "attachAgain"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: terminalArea.detached && backend.selected !== ""
                            label: qsTr("Attach again")
                            border.width: 1
                            border.color: theme.border
                            onClicked: {
                                terminalArea.detached = false
                                panel.attachRound++
                            }
                        }
                    }
                }

                // The result of an action, over the terminal's bottom corner.
                Rectangle {
                    anchors.right: terminalArea.right
                    anchors.bottom: terminalArea.bottom
                    anchors.margins: 16
                    opacity: backend.notice !== "" ? 1 : 0
                    visible: backend.notice !== "" || opacity > 0
                    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    width: Math.min(noticeText.implicitWidth + 20, terminalArea.width - 40)
                    height: noticeText.implicitHeight + 12
                    radius: theme.radius
                    color: theme.background
                    border.width: 1
                    border.color: backend.noticeIsError ? theme.urgent : theme.success
                    Text {
                        id: noticeText
                        anchors.centerIn: parent
                        width: parent.width - 20
                        text: backend.notice
                        color: backend.noticeIsError ? theme.urgent : theme.foreground
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // Every shortcut, over the conversation side; its own button stays
    // visible, and nothing beneath it can be clicked.
    Rectangle {
        id: help
        objectName: "help"
        x: panel.listShown ? panel.sidebarWidth + 3 : 2
        y: panel.headerHeight + 3
        width: panel.width - x - 2
        height: panel.height - y - 2
        opacity: panel.helpVisible ? 1 : 0
        visible: panel.helpVisible || opacity > 0
        enabled: panel.helpVisible
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        color: theme.background
        radius: theme.radius

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onClicked: panel.helpVisible = false
            onWheel: (wheel) => wheel.accepted = true
        }

        Column {
            anchors.centerIn: parent
            spacing: 12
            Text {
                text: qsTr("Keyboard shortcuts")
                color: theme.foreground
            }
            Grid {
                columns: 2
                columnSpacing: 24
                rowSpacing: 8
                Repeater {
                    model: [
                        qsTr("Any key"), qsTr("Goes to the agent's terminal, as in herdr"),
                        "Alt+↑ Alt+↓", qsTr("Pick an agent"),
                        "Ctrl+P", qsTr("Go to an agent, or start one in a workspace"),
                        "Ctrl+D", qsTr("In the picker: close a workspace (press twice)"),
                        "Alt+E", qsTr("Switch between the agent and the editor beside it"),
                        "Ctrl+;", qsTr("In the editor: show or hide a terminal below it (Ctrl+W closes it)"),
                        "Ctrl+Shift+O", qsTr("Open the agent in herdr"),
                        "Ctrl+B", qsTr("In the agent view: show or hide the agent list"),
                        "Ctrl+Alt+B", qsTr("Show or hide the overview of the conversation"),
                        "Super+H", qsTr("Show or hide the panel"),
                        "Super+Alt+H", qsTr("Put the panel over the whole screen, and back"),
                        "Esc", qsTr("Close the picker or help; outside the terminal, hide the panel"),
                        "F1", qsTr("Show or hide this help"),
                        "Ctrl+Q", qsTr("Quit drover")
                    ]
                    delegate: Text {
                        required property var modelData
                        required property int index
                        text: modelData
                        color: index % 2 === 0 ? theme.foreground : panel.dim
                        width: index % 2 === 0 ? implicitWidth : Math.min(implicitWidth, help.width - 260)
                        wrapMode: Text.Wrap
                    }
                }
            }
            Text {
                text: qsTr("Click anywhere here to close.")
                color: panel.dim
            }
        }
    }

    // The list's own button, at the panel's top left whether the list shows
    // or not, so it never moves under the pointer. While the list is hidden
    // it stands in for it: red when someone asks or herdr fails, and busy
    // while an agent starts.
    IconButton {
        id: listButton
        objectName: "listButton"
        x: 8
        y: (panel.headerHeight - height) / 2 + 2
        icon: ""
        shortcut: "Ctrl+B"
        // With nothing picked the list shows anyway, and the button has
        // nothing to do.
        visible: backend.selected !== ""
        // Red for agents asking other than the one in front, which says so
        // itself.
        readonly property bool othersAsk: backend.blocked - (backend.selectedStatus === "blocked" ? 1 : 0) > 0
        tint: !panel.listShown && (othersAsk || backend.error !== "") ? theme.urgent
            : panel.listShown ? panel.dim : theme.foreground
        hint: !panel.listShown && backend.error !== "" ? backend.error : qsTr("Show or hide the agent list")
        busy: !panel.listShown && backend.busy === "start"
        clickableWhileBusy: true
        onClicked: surface.listHidden = !surface.listHidden
    }

    WorkspacePicker {
        id: workspacePicker
        statusLabel: panel.statusLabel
        onAgentChosen: (pane) => {
            // Going to the agent in front, from its editor, means its terminal.
            if (pane === backend.selected && backend.editing)
                backend.toggleEditor()
            backend.select(pane)
            panel.agentPicked()
        }
        anchors.fill: parent
        anchors.margins: 2
    }

    // Starting an agent takes a few seconds; the sidebar header says so.
    Row {
        x: panel.sidebarWidth - width - 12
        y: (panel.headerHeight - height) / 2 + 2
        spacing: 6
        visible: backend.busy === "start" && panel.listShown
        IconButton { busy: true; enabled: false; opacity: 1; width: 24; height: 24 }
    }
}
