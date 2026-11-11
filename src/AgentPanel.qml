import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The open panel: every agent of the herdr session, the selected agent's
// screen, and a way to answer it without leaving the current workspace.
// Everything has a fixed size, so nothing shifts as agents change state.
Rectangle {
    id: panel
    property alias messageField: message
    property bool helpVisible: false
    signal quitRequested()

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property bool blocked: backend.selectedStatus === "blocked"

    color: "#0e0e10"
    radius: theme.radius
    border.width: 1
    border.color: "#33333a"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 6

            Text {
                text: qsTr("herdr agents")
                color: "#e6e6ea"
                font.pixelSize: 15
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("%n agents", "", backend.agents.count)
                color: "#8a8a95"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            ToolButton {
                text: "?"
                font.pixelSize: 15
                onClicked: panel.helpVisible = !panel.helpVisible
                Accessible.name: qsTr("Keyboard shortcuts")
            }
            ToolButton {
                text: "⏻"
                font.pixelSize: 15
                onClicked: panel.quitRequested()
                Accessible.name: qsTr("Quit drover")
            }
        }

        // The list of agents, at a fixed height.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 4 * 46 + 3 * 2 + 8
            color: "#0a0a0c"
            radius: Math.min(4, theme.radius)

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
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
                    onPicked: backend.select(paneId)
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
                color: backend.error !== "" ? "#ff6b6b" : "#8a8a95"
                font.pixelSize: 13
            }
        }

        // The selected agent's screen, newest lines at the bottom.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#060608"
            radius: Math.min(4, theme.radius)
            clip: true

            Text {
                id: screen
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                text: backend.selected === "" ? "" : backend.output
                color: "#c8c8d0"
                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
            }

            Text {
                anchors.centerIn: parent
                visible: backend.selected === ""
                text: qsTr("Pick an agent to see its screen.")
                color: "#8a8a95"
                font.pixelSize: 12
            }

            // Results of an action sit over the screen rather than pushing
            // anything around.
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 6
                visible: backend.notice !== ""
                width: Math.min(noticeText.implicitWidth + 16, parent.width - 12)
                height: noticeText.implicitHeight + 10
                radius: Math.min(4, theme.radius)
                color: backend.noticeIsError ? "#5a1f24" : "#1f3a26"
                Text {
                    id: noticeText
                    anchors.centerIn: parent
                    width: parent.width - 16
                    text: backend.notice
                    color: "#f0f0f4"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        // Talk to the selected agent: a message, or keys when it asks a question.
        StackLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.fillHeight: false
            currentIndex: panel.blocked ? 1 : 0

            RowLayout {
                spacing: 8
                TextField {
                    id: message
                    Layout.fillWidth: true
                    enabled: backend.selected !== ""
                    placeholderText: backend.selected === "" ? qsTr("Message")
                                     : qsTr("Message to %1").arg(backend.selectedTitle)
                    onAccepted: {
                        if (text.trim() === "") {
                            backend.jump()
                            return
                        }
                        backend.prompt(text)
                        text = ""
                    }
                }
                Button {
                    text: qsTr("Open")
                    enabled: backend.selected !== ""
                    flat: true
                    onClicked: backend.jump()
                }
            }

            RowLayout {
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: implicitWidth
                    text: qsTr("Answer:")
                    color: "#ff6b6b"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Repeater {
                    model: [
                        { label: "↑", key: "up" }, { label: "↓", key: "down" },
                        { label: "1", key: "1" }, { label: "2", key: "2" }, { label: "3", key: "3" },
                        { label: "Enter", key: "enter" }, { label: "Esc", key: "esc" }
                    ]
                    delegate: ToolButton {
                        required property var modelData
                        text: modelData.label
                        font.pixelSize: 13
                        leftPadding: 4
                        rightPadding: 4
                        Layout.preferredWidth: modelData.label.length > 1 ? implicitContentWidth + 20 : 34
                        onClicked: backend.sendKeys([modelData.key])
                    }
                }
                Button {
                    text: qsTr("Open")
                    flat: true
                    onClicked: backend.jump()
                }
            }
        }
    }

    // Every shortcut, over the panel.
    Rectangle {
        anchors.fill: parent
        visible: panel.helpVisible
        color: "#0e0e10"
        radius: theme.radius

        TapHandler { onTapped: panel.helpVisible = false }

        Column {
            anchors.centerIn: parent
            spacing: 10
            Text {
                text: qsTr("Keyboard shortcuts")
                color: "#e6e6ea"
                font.pixelSize: 15
                font.bold: true
            }
            Grid {
                columns: 2
                columnSpacing: 20
                rowSpacing: 8
                Repeater {
                    model: [
                        "↑ ↓", qsTr("Pick an agent"),
                        "Enter", qsTr("Send the message, or open the agent when it's empty"),
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
                        color: index % 2 === 0 ? theme.accent : "#c8c8d0"
                        font.pixelSize: 13
                        font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                        width: index % 2 === 0 ? implicitWidth : 300
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
