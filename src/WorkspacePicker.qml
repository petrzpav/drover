import QtQuick
import QtQuick.Layouts

// Ctrl+P: pick a herdr workspace by typing part of its name, and the default
// agent starts there in a new tab. A card over the panel, so nothing behind it
// moves; Up and Down walk the matches, Enter starts, Esc closes.
Rectangle {
    id: picker
    property bool open: false
    property alias filter: field.text
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)

    visible: open
    color: Qt.alpha(theme.background, 0.6)

    function show() {
        field.text = ""
        list.currentIndex = 0
        open = true
        field.forceActiveFocus()
    }
    function close() {
        open = false
    }
    function choose(index) {
        const item = matches[index]
        if (!item || backend.busy !== "")
            return
        backend.startAgent(item.id)
        close()
    }

    // Matches by substring, those starting with the text first.
    readonly property var matches: {
        const text = field.text.trim().toLowerCase()
        const all = backend.workspaces
        if (text === "")
            return all
        const starts = [], contains = []
        for (const item of all) {
            const label = item.label.toLowerCase()
            if (label.startsWith(text)) starts.push(item)
            else if (label.includes(text)) contains.push(item)
        }
        return starts.concat(contains)
    }
    onMatchesChanged: list.currentIndex = 0

    // Clicks outside the card close it; nothing beneath can be clicked.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: picker.close()
        onWheel: (wheel) => wheel.accepted = true
    }

    Rectangle {
        id: card
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(24, parent.height * 0.12)
        width: Math.min(560, parent.width - 48)
        height: Math.min(440, parent.height - y - 24)
        radius: theme.radius
        color: theme.background
        border.width: 2
        border.color: theme.border

        MouseArea { anchors.fill: parent; onWheel: (wheel) => wheel.accepted = false }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: qsTr("Start %1 in a workspace").arg(backend.agentKind)
                color: theme.foreground
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: field.implicitHeight + 16
                radius: theme.radius
                color: Qt.alpha(theme.foreground, 0.04)
                border.width: 1
                border.color: theme.border

                TextInput {
                    id: field
                    objectName: "pickerField"
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.foreground
                    selectionColor: Qt.alpha(theme.foreground, 0.35)
                    selectedTextColor: theme.foreground
                    clip: true

                    Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
                    Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                    Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) picker.choose(list.currentIndex) }
                    Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) picker.choose(list.currentIndex) }

                    Text {
                        x: 2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: field.text === ""
                        text: qsTr("Workspace")
                        color: picker.dim
                    }
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: picker.matches
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool current: ListView.isCurrentItem
                    width: ListView.view.width
                    height: rowText.implicitHeight + 16
                    radius: theme.radius
                    color: current || rowHover.hovered ? Qt.alpha(theme.foreground, 0.08) : "transparent"
                    border.width: current ? 1 : 0
                    border.color: Qt.alpha(theme.border, 0.25)

                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: picker.choose(index) }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Text {
                            id: rowText
                            Layout.fillWidth: true
                            text: modelData.label
                            color: current ? theme.accent : theme.foreground
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: modelData.agents > 0
                            text: qsTr("%n agents", "", modelData.agents)
                            color: picker.dim
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: backend.workspaces.length === 0 ? qsTr("herdr has no workspaces.")
                                                          : qsTr("No workspace matches.")
                    color: picker.dim
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Enter starts it in a new tab · Esc closes")
                color: picker.dim
                elide: Text.ElideRight
            }
        }
    }
}
