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
    readonly property Item terminal: !backend.editing ? (terminals.count > 0 ? terminals.itemAt(0) : null)
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
    // The user picked an agent; Main decides where the keyboard goes.
    signal agentPicked()

    readonly property color line: Qt.alpha(theme.foreground, 0.12)
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    readonly property int sidebarWidth: 280
    readonly property int headerHeight: 48

    color: theme.background
    radius: theme.radius
    border.width: 2
    border.color: theme.border

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
            Layout.preferredWidth: panel.sidebarWidth
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0

            Text {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.headerHeight
                Layout.leftMargin: 18
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

                    delegate: AgentRow {
                        width: ListView.view.width
                        selected: paneId === backend.selected
                        statusText: panel.statusLabel(status)
                        onPicked: { backend.select(paneId); panel.agentPicked() }
                        onOpened: { backend.select(paneId); backend.jump() }
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

        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: panel.line }

        // The conversation with the selected agent.
        ColumnLayout {
            id: conversation
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.headerHeight
                Layout.leftMargin: 16
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
                    visible: backend.selected !== ""
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
                                label: modelData.label
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
                    icon: ""
                    hint: qsTr("Keyboard shortcuts")
                    shortcut: "F1"
                    tint: panel.helpVisible ? theme.accent : theme.foreground
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

            // The terminal, attached to the selected agent.
            Item {
                id: terminalArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                property bool detached: false
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
                    ? Math.round(terminalArea.height * 0.35) : 0

                Repeater {
                    id: terminals
                    model: panel.attached && !terminalArea.detached && backend.selectedTerminal !== ""
                           && backend.herdrPath !== "" ? [backend.selectedTerminal + "#" + panel.attachRound] : []
                    delegate: AgentTerminal {
                        required property string modelData
                        terminalId: modelData.split("#")[0]
                        visible: !backend.editing
                        width: terminalArea.width
                        height: terminalArea.height
                        onDetached: terminalArea.detached = true
                        Component.onCompleted: if (panel.attached && visible) focusTerminal()
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
                        width: terminalArea.width
                        height: terminalArea.height - (terminalArea.shellHeight > 0 ? terminalArea.shellHeight + 1 : 0)
                        // An editor that quits closes its pane: back to the agent.
                        onDetached: backend.editorClosed()
                        Component.onCompleted: if (panel.attached && visible) focusTerminal()
                    }
                }
                Rectangle {
                    visible: terminalArea.shellHeight > 0
                    y: terminalArea.height - terminalArea.shellHeight - 1
                    width: terminalArea.width
                    height: 1
                    color: panel.line
                }
                Repeater {
                    id: shellTerminals
                    model: panel.attached && backend.editing && backend.shellTerminal !== "" && backend.herdrPath !== ""
                           ? [backend.shellTerminal + "#" + panel.attachRound] : []
                    delegate: AgentTerminal {
                        required property string modelData
                        objectName: "shellTerminal"
                        terminalId: modelData.split("#")[0]
                        shell: true
                        // Hidden, it keeps its size, so its pane isn't squeezed.
                        visible: terminalArea.shellHeight > 0
                        y: terminalArea.height - height
                        width: terminalArea.width
                        height: Math.round(terminalArea.height * 0.35)
                        // Its shell exited: the terminal is gone.
                        onDetached: backend.shellClosed()
                        Component.onCompleted: if (panel.attached && visible) focusTerminal()
                    }
                }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    spacing: 12
                    visible: panel.terminal === null
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

                // The result of an action, over the terminal's bottom corner.
                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 16
                    visible: backend.notice !== ""
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
        objectName: "help"
        x: panel.sidebarWidth + 3
        y: panel.headerHeight + 3
        width: panel.width - x - 2
        height: panel.height - y - 2
        visible: panel.helpVisible
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
                        "Alt+↑ Alt+↓", qsTr("Pick an agent (in the editor: move lines)"),
                        "Ctrl+P", qsTr("Start an agent in a workspace, or create one"),
                        "Ctrl+D", qsTr("In the picker: close a workspace (press twice)"),
                        "Alt+E", qsTr("Switch between the agent and the editor beside it"),
                        "Ctrl+;", qsTr("In the editor: show or hide a terminal below it (Ctrl+W closes it)"),
                        "Ctrl+Shift+O", qsTr("Open the agent in herdr"),
                        "Super+H", qsTr("Show or hide the panel"),
                        "Esc", qsTr("Close the picker or help; outside the terminal, hide the panel"),
                        "F1", qsTr("Show or hide this help"),
                        "Ctrl+Q", qsTr("Quit drover")
                    ]
                    delegate: Text {
                        required property var modelData
                        required property int index
                        text: modelData
                        color: index % 2 === 0 ? theme.accent : theme.foreground
                        width: index % 2 === 0 ? implicitWidth : Math.min(implicitWidth, panel.width - panel.sidebarWidth - 260)
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

    WorkspacePicker {
        id: workspacePicker
        anchors.fill: parent
        anchors.margins: 2
    }

    // Starting an agent takes a few seconds; the sidebar header says so.
    Row {
        x: panel.sidebarWidth - width - 12
        y: (panel.headerHeight - height) / 2 + 2
        spacing: 6
        visible: backend.busy === "start"
        IconButton { busy: true; enabled: false; opacity: 1; width: 24; height: 24 }
    }
}
