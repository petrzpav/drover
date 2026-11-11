import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open panel, laid out like a chat: every agent of the herdr session on
// the left, the selected agent's screen on the right with a box to talk to it,
// or keys to answer it when it asks. Drawn like an Omarchy shell popup: the
// theme background and the Hyprland active border, in the terminal's font
// (the application font, set in main.cpp). Everything has a fixed size, and
// messages lie over the content, so nothing shifts as agents change state.
Rectangle {
    id: panel
    property alias messageField: message
    property alias answerBar: answer
    property alias picker: workspacePicker
    property bool helpVisible: false
    signal quitRequested()
    // The user picked an agent; Main decides where the keyboard goes.
    signal agentPicked()

    readonly property bool blocked: backend.selectedStatus === "blocked"
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
                Text {
                    Layout.fillWidth: true
                    text: backend.selectedTitle
                    color: theme.foreground
                    elide: Text.ElideRight
                }
                Text {
                    visible: backend.selected !== ""
                    text: panel.statusLabel(backend.selectedStatus)
                    color: panel.statusColor(backend.selectedStatus)
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Open the agent in herdr")
                    shortcut: "Ctrl+O"
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

            // The agent's screen, line for line as in its terminal. It follows
            // new output; scrolled up or with text selected, it holds still
            // and offers the new output instead.
            Item {
                id: screenArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: screen
                    objectName: "screen"
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    contentWidth: Math.max(width, screenText.implicitWidth)
                    contentHeight: screenText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                    property string shown: ""
                    property bool following: true
                    property bool pending: false
                    property bool autoScrolling: false

                    function toBottom() {
                        autoScrolling = true
                        contentY = Math.max(0, contentHeight - height)
                        autoScrolling = false
                    }
                    function catchUp() {
                        screenText.deselect()
                        shown = backend.outputHtml
                        pending = false
                        following = true
                        toBottom()
                    }

                    // Whatever moved the view (wheel, drag, scrollbar),
                    // following is whether it ended at the bottom.
                    onContentYChanged: if (!autoScrolling) following = contentY >= contentHeight - height - 4
                    onFollowingChanged: if (following && pending) catchUp()
                    onContentHeightChanged: if (following) toBottom()
                    onHeightChanged: if (following) toBottom()

                    TextEdit {
                        id: screenText
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10
                        text: screen.shown
                        readOnly: true
                        selectByMouse: true
                        activeFocusOnPress: false
                        wrapMode: TextEdit.NoWrap
                        textFormat: TextEdit.RichText
                        color: theme.foreground
                        selectionColor: Qt.alpha(theme.foreground, 0.35)
                        selectedTextColor: theme.foreground
                    }
                }

                Connections {
                    target: backend
                    function onOutputChanged() {
                        if (screen.following && screenText.selectedText === "") {
                            screen.shown = backend.outputHtml
                            screen.pending = false
                        } else {
                            screen.pending = true
                        }
                    }
                    function onSelectedChanged() {
                        screen.contentX = 0
                        screen.catchUp()
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: backend.selected === "" && list.count > 0
                    text: backend.selectionLost ? qsTr("The agent you were talking to is gone. Pick another one.")
                                                : qsTr("Pick an agent to see its screen.")
                    color: panel.dim
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    spacing: 8
                    layoutDirection: Qt.RightToLeft

                    // New output waits while the view is held.
                    IconButton {
                        visible: screen.pending
                        label: qsTr("New output ↓")
                        tint: theme.accent
                        color: theme.background
                        border.width: 1
                        border.color: theme.border
                        onClicked: screen.catchUp()
                    }

                    // The result of an action, beside the box it came from.
                    Rectangle {
                        visible: backend.notice !== ""
                        width: Math.min(noticeText.implicitWidth + 20, screenArea.width - 40)
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

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panel.line }

            // Talk to the agent: a message, or keys when it asks a question.
            StackLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                Layout.fillHeight: false
                currentIndex: panel.blocked ? 1 : 0

                Item {
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 12
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        radius: theme.radius
                        color: Qt.alpha(theme.foreground, 0.04)
                        border.width: 1
                        border.color: message.activeFocus ? theme.border : Qt.alpha(theme.foreground, 0.25)

                        Flickable {
                            id: messageScroll
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 46
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            clip: true
                            contentWidth: width
                            contentHeight: message.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds

                            TextEdit {
                                id: message
                                objectName: "messageField"
                                width: messageScroll.width
                                enabled: backend.selected !== ""
                                wrapMode: TextEdit.Wrap
                                color: theme.foreground
                                selectionColor: Qt.alpha(theme.foreground, 0.35)
                                selectedTextColor: theme.foreground
                                selectByMouse: true
                                onCursorRectangleChanged: {
                                    if (cursorRectangle.y + cursorRectangle.height > messageScroll.contentY + messageScroll.height)
                                        messageScroll.contentY = cursorRectangle.y + cursorRectangle.height - messageScroll.height
                                    else if (cursorRectangle.y < messageScroll.contentY)
                                        messageScroll.contentY = cursorRectangle.y
                                }

                                function send() {
                                    if (backend.busy !== "")
                                        return
                                    if (text.trim() === "") {
                                        backend.jump()
                                        return
                                    }
                                    backend.prompt(text)
                                }

                                // Enter sends, Shift+Enter starts a new line.
                                Keys.onReturnPressed: (event) => {
                                    if (event.modifiers & Qt.ShiftModifier) {
                                        event.accepted = false
                                        return
                                    }
                                    if (!event.isAutoRepeat)
                                        send()
                                }
                                Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) send() }

                                Text {
                                    x: 2
                                    visible: message.text === "" && !message.inputMethodComposing
                                    text: backend.selected === "" ? qsTr("Message")
                                        : qsTr("Message to %1").arg(backend.selectedTitle)
                                    color: panel.dim
                                    width: message.width - 2
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        IconButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            icon: ""
                            hint: qsTr("Send")
                            shortcut: "Enter"
                            tint: theme.accent
                            busy: backend.busy === "send"
                            enabled: backend.selected !== "" && message.text.trim() !== ""
                            onClicked: message.send()
                        }

                        // Cleared once herdr took it, and only if it is still
                        // what was sent, so nothing typed meanwhile is lost
                        // and a rejected message stays in the box.
                        Connections {
                            target: backend
                            function onPromptSent(text) {
                                if (message.text === text)
                                    message.clear()
                            }
                        }
                    }
                }

                // Answer keys. The keyboard reaches them only when the user
                // puts it there (Tab, a click, or picking the agent), never
                // because an agent started asking while they typed.
                FocusScope {
                    id: answer
                    objectName: "answerBar"
                    activeFocusOnTab: true
                    property string lastKey: ""

                    function press(key) {
                        if (backend.busy !== "")
                            return
                        lastKey = key
                        backend.sendKeys([key])
                    }

                    Keys.onShortcutOverride: (event) => {
                        const k = event.key
                        event.accepted = (k >= Qt.Key_1 && k <= Qt.Key_9) || k === Qt.Key_Up || k === Qt.Key_Down
                            || k === Qt.Key_Return || k === Qt.Key_Enter
                            || (k === Qt.Key_Escape && (event.modifiers & Qt.ShiftModifier))
                    }
                    Keys.onPressed: (event) => {
                        const k = event.key
                        let key = ""
                        if (k >= Qt.Key_1 && k <= Qt.Key_9) key = String(k - Qt.Key_0)
                        else if (k === Qt.Key_Up) key = "up"
                        else if (k === Qt.Key_Down) key = "down"
                        else if (k === Qt.Key_Return || k === Qt.Key_Enter) key = "enter"
                        else if (k === Qt.Key_Escape && (event.modifiers & Qt.ShiftModifier)) key = "esc"
                        if (key === "")
                            return
                        event.accepted = true
                        if (!event.isAutoRepeat)
                            answer.press(key)
                    }

                    TapHandler { onTapped: answer.forceActiveFocus() }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 6
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        radius: theme.radius
                        color: answer.activeFocus ? Qt.alpha(theme.foreground, 0.04) : "transparent"
                        border.width: 1
                        border.color: answer.activeFocus ? theme.border : "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: answer.activeFocus
                                      ? qsTr("Answer with 1–9, ↑ ↓ and Enter; Shift+Esc sends Esc.")
                                      : qsTr("The agent asks you something. Press Tab or click here to answer from the keyboard.")
                                color: theme.foreground
                                elide: Text.ElideRight
                            }
                            Row {
                                spacing: 6
                                Repeater {
                                    model: [
                                        { label: "↑", key: "up" }, { label: "↓", key: "down" },
                                        { label: "1", key: "1" }, { label: "2", key: "2" }, { label: "3", key: "3" },
                                        { label: "Enter", key: "enter" }, { label: "Esc", key: "esc" }
                                    ]
                                    delegate: KeyCap {
                                        required property var modelData
                                        label: modelData.label
                                        key: modelData.key
                                        activeFocusOnTab: false
                                        enabled: backend.busy === "" || busy
                                        busy: backend.busy === "keys" && answer.lastKey === key
                                        onClicked: answer.press(key)
                                    }
                                }
                            }
                        }
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
                        "Enter", qsTr("Send the message; with an empty box, open the agent"),
                        "Shift+Enter", qsTr("New line in the message"),
                        "Alt+↑ Alt+↓", qsTr("Pick an agent"),
                        "Ctrl+P", qsTr("Start an agent in a workspace"),
                        "Ctrl+O", qsTr("Open the agent in herdr"),
                        "Tab", qsTr("Move to the answer keys when an agent asks you"),
                        "1–9 ↑ ↓ Enter", qsTr("Answer, while the answer keys have the keyboard"),
                        "Shift+Esc", qsTr("Send Esc to the agent"),
                        "Esc", qsTr("Hide the panel"),
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
