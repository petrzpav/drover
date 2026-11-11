import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open panel, laid out like a chat: every agent of the herdr session on
// the left, the selected agent's screen on the right with a box to talk to it,
// or keys to answer it when it asks. Drawn like an Omarchy shell popup: the
// theme background and the Hyprland active border, in the system font at its
// own size; the agent's screen and the message box are monospace. Everything
// has a fixed size, so nothing shifts as agents change state.
Rectangle {
    id: panel
    property alias messageField: message
    property bool helpVisible: false
    signal quitRequested()

    readonly property bool blocked: backend.selectedStatus === "blocked"
    readonly property color line: Qt.alpha(theme.foreground, 0.12)

    color: theme.background
    radius: theme.radius
    border.width: 1
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
             : status === "done" ? theme.success : theme.muted
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // Sidebar: the agents.
        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: 14
                Layout.rightMargin: 8
                spacing: 8
                Text {
                    text: qsTr("herdr agents")
                    color: theme.foreground
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n agents", "", backend.agents.count)
                    color: theme.muted
                    elide: Text.ElideRight
                }
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
                        onPicked: { backend.select(paneId); panel.forceActiveFocus() }
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
                    color: backend.error !== "" ? theme.urgent : theme.muted
                }
            }
        }

        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: panel.line }

        // The conversation with the selected agent.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: 16
                Layout.rightMargin: 8
                spacing: 10

                StatusDot { visible: backend.selected !== ""; status: backend.selectedStatus }
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
                    enabled: backend.selected !== ""
                    onClicked: backend.jump()
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Keyboard shortcuts")
                    tint: panel.helpVisible ? theme.accent : theme.foreground
                    onClicked: panel.helpVisible = !panel.helpVisible
                }
                IconButton {
                    icon: ""
                    hint: qsTr("Quit drover")
                    onClicked: panel.quitRequested()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panel.line }

            // The agent's screen; it follows new output unless scrolled up.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: screen
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    contentWidth: width
                    contentHeight: screenText.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    property bool following: true
                    onMovementEnded: following = atYEnd
                    onContentHeightChanged: if (following) contentY = Math.max(0, contentHeight - height)
                    onHeightChanged: if (following) contentY = Math.max(0, contentHeight - height)

                    TextEdit {
                        id: screenText
                        width: screen.width - 24
                        x: 12
                        topPadding: 10
                        bottomPadding: 10
                        text: backend.output
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText
                        color: theme.foreground
                        selectionColor: Qt.alpha(theme.foreground, 0.35)
                        selectedTextColor: theme.foreground
                        font.family: "monospace"
                    }
                }

                Connections {
                    target: backend
                    function onSelectionChanged() { screen.following = true }
                }

                Text {
                    anchors.centerIn: parent
                    visible: backend.selected === ""
                    text: qsTr("Pick an agent to see its screen.")
                    color: theme.muted
                }

                // Results of an action sit over the screen rather than
                // pushing anything around.
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 10
                    visible: backend.notice !== ""
                    width: Math.min(noticeText.implicitWidth + 20, parent.width - 20)
                    height: noticeText.implicitHeight + 12
                    radius: Math.min(4, theme.radius)
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

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: panel.line }

            // Talk to the agent: a message, or keys when it asks a question.
            StackLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                Layout.fillHeight: false
                currentIndex: panel.blocked ? 1 : 0

                Item {
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 10
                        radius: Math.min(4, theme.radius)
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
                                font.family: "monospace"
                                selectByMouse: true
                                onCursorRectangleChanged: {
                                    if (cursorRectangle.y + cursorRectangle.height > messageScroll.contentY + messageScroll.height)
                                        messageScroll.contentY = cursorRectangle.y + cursorRectangle.height - messageScroll.height
                                    else if (cursorRectangle.y < messageScroll.contentY)
                                        messageScroll.contentY = cursorRectangle.y
                                }

                                function send() {
                                    if (text.trim() === "") {
                                        backend.jump()
                                        return
                                    }
                                    // Cleared once herdr took it, so a
                                    // rejected message stays in the box.
                                    backend.prompt(text)
                                }

                                // Enter sends, Shift+Enter starts a new line.
                                Keys.onReturnPressed: (event) => {
                                    if (event.modifiers & Qt.ShiftModifier) {
                                        event.accepted = false
                                        return
                                    }
                                    send()
                                }
                                Keys.onEnterPressed: (event) => send()

                                Text {
                                    visible: message.text === "" && !message.inputMethodComposing
                                    text: backend.selected === "" ? qsTr("Message")
                                        : qsTr("Message to %1").arg(backend.selectedTitle)
                                    color: theme.muted
                                    font: message.font
                                    width: message.width
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
                            tint: theme.accent
                            enabled: backend.selected !== "" && message.text.trim() !== ""
                            onClicked: message.send()
                        }

                        Connections {
                            target: backend
                            function onPromptSent() { message.clear() }
                        }
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        Text {
                            text: qsTr("The agent asks you something. Answer with the keys it offers:")
                            color: theme.urgent
                            Layout.fillWidth: true
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
                                    onClicked: backend.sendKeys([key])
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Every shortcut, over the panel.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        visible: panel.helpVisible
        color: theme.background
        radius: theme.radius

        TapHandler { onTapped: panel.helpVisible = false }

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
                        "Alt+↑ Alt+↓", qsTr("Pick an agent"),
                        "Enter", qsTr("Send the message, or open the agent when it's empty"),
                        "Shift+Enter", qsTr("New line in the message"),
                        "Ctrl+O", qsTr("Open the agent in herdr"),
                        "1–9", qsTr("Answer an agent that asks you"),
                        "Esc", qsTr("Hide the panel"),
                        "?", qsTr("Show or hide this help"),
                        "Q", qsTr("Quit drover")
                    ]
                    delegate: Text {
                        required property var modelData
                        required property int index
                        text: modelData
                        color: index % 2 === 0 ? theme.accent : theme.foreground
                        width: index % 2 === 0 ? implicitWidth : 420
                        wrapMode: Text.Wrap
                    }
                }
            }
            Text {
                width: 560
                text: qsTr("Space, arrows, ? and Q work when the message box isn't focused; Esc always hides the panel.")
                color: theme.muted
                wrapMode: Text.Wrap
            }
        }
    }
}
