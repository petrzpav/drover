import QtQuick
import QtQuick.Layouts

// Ctrl+P: pick a herdr workspace by typing part of its name, and the default
// agent starts there in a new tab. A card over the panel, so nothing behind it
// moves; Up and Down walk the matches, Enter starts, Esc closes. A name no
// workspace has yet adds a row that creates it, in a folder picked in the
// desktop's folder picker (Ctrl+O or the row's folder button; Enter too,
// unless the text already names a folder, or a new one in an existing
// folder, which Enter makes). Ctrl+D (or the row's trash
// button) closes a workspace after a second press confirms it.
Rectangle {
    id: picker
    property bool open: false
    property alias filter: field.text
    readonly property color dim: Qt.alpha(theme.foreground, 0.55)
    // The workspace a first Ctrl+D marked for closing, by id: the list
    // refreshes every second, so an index could point at another one.
    property string armed: ""
    readonly property var armedItem: {
        for (const item of backend.workspaces)
            if (item.id === armed)
                return item
        return null
    }
    readonly property var current: matches[list.currentIndex] ?? null

    // Fades in and out; it takes input only while open.
    opacity: open ? 1 : 0
    visible: open || opacity > 0
    enabled: open
    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    color: Qt.alpha(theme.background, 0.6)

    function show() {
        field.text = ""
        highlighted = ""
        list.currentIndex = 0
        armed = ""
        open = true
        field.forceActiveFocus()
    }
    function close() {
        armed = ""
        open = false
    }
    // Esc first takes back a pending close; true when it did.
    function cancel() {
        if (armed === "")
            return false
        armed = ""
        return true
    }
    function choose(index) {
        const item = matches[index]
        if (!item || backend.busy !== "")
            return
        if (armed !== "") {
            if (item.id === armed)
                backend.closeWorkspace(armed)
            else
                armed = ""
            return
        }
        if (item.create && !item.found && !item.makeFolder) {
            chooseFolder()
            return
        }
        if (item.create)
            backend.createWorkspace(field.text)
        else
            backend.startAgent(item.id)
        close()
    }
    // The panel steps aside while the picker is open, and comes back after.
    function chooseFolder() {
        // Only a name no workspace has yet, as on the Create row.
        if (backend.busy !== "" || !matches.some(item => item.create))
            return
        backend.chooseFolderAndCreate(field.text)
    }
    // The first press marks the workspace, the second closes it.
    function remove(index) {
        const item = matches[index]
        if (!item || item.create || backend.busy !== "")
            return
        moveHighlight(index)
        if (armed === item.id)
            backend.closeWorkspace(item.id)
        else
            armed = item.id
    }
    Connections {
        target: backend
        function onBusyChanged() { if (backend.busy === "") picker.armed = "" }
    }

    // Matches by substring, those starting with the text first; then, unless
    // a workspace has exactly that name, a row that creates it.
    readonly property var matches: {
        const text = field.text.trim().toLowerCase()
        const all = backend.workspaces
        if (text === "")
            return all
        const starts = [], contains = []
        let exact = false
        for (const item of all) {
            const label = item.label.toLowerCase()
            if (label === text) exact = true
            if (label.startsWith(text)) starts.push(item)
            else if (label.includes(text)) contains.push(item)
        }
        const found = starts.concat(contains)
        if (!exact) {
            const fresh = backend.newWorkspace(field.text)
            found.push({ id: "", create: true, label: fresh.label, folder: fresh.folder, found: fresh.found,
                         makeFolder: fresh.makeFolder, agents: 0 })
        }
        return found
    }
    // The workspace the user moved the highlight to; empty means the top
    // row. A refresh keeps the highlight on it while it exists. The view
    // resets its index when it gets the new list, so this runs after that.
    property string highlighted: ""
    function restoreHighlight() {
        let index = 0
        if (highlighted !== "")
            for (let i = 0; i < matches.length; ++i)
                if (!matches[i].create && matches[i].id === highlighted)
                    index = i
        list.currentIndex = index
    }
    function moveHighlight(index) {
        list.currentIndex = Math.max(0, Math.min(list.count - 1, index))
        highlighted = matches[list.currentIndex] ? matches[list.currentIndex].id : ""
    }
    onMatchesChanged: Qt.callLater(restoreHighlight)
    // A workspace closed elsewhere can't be closed again.
    onArmedItemChanged: if (!armedItem) armed = ""

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

                    onTextChanged: { picker.armed = ""; picker.highlighted = "" }
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
                            event.accepted = true
                            if (!event.isAutoRepeat)
                                picker.remove(list.currentIndex)
                        } else if (event.key === Qt.Key_O && event.modifiers === Qt.ControlModifier) {
                            event.accepted = true
                            if (!event.isAutoRepeat)
                                picker.chooseFolder()
                        }
                    }
                    Keys.onUpPressed: { picker.armed = ""; picker.moveHighlight(list.currentIndex - 1) }
                    Keys.onDownPressed: { picker.armed = ""; picker.moveHighlight(list.currentIndex + 1) }
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
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool current: ListView.isCurrentItem
                    readonly property bool marked: !modelData.create && modelData.id === picker.armed
                    width: ListView.view.width
                    height: Math.max(rowText.implicitHeight + 16, 30)
                    radius: theme.radius
                    color: marked ? Qt.alpha(theme.urgent, 0.12)
                         : current || rowHover.hovered ? Qt.alpha(theme.foreground, 0.08) : "transparent"
                    border.width: current || marked ? 1 : 0
                    border.color: marked ? theme.urgent : Qt.alpha(theme.border, 0.25)

                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    // A click on the trash button is not a click on the row.
                    TapHandler {
                        onTapped: (point) => {
                            if (!trash.visible || !trash.contains(trash.mapFromItem(row, point.position)))
                                picker.choose(row.index)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 4
                        spacing: 8
                        Text {
                            id: rowText
                            Layout.fillWidth: true
                            text: modelData.create ? qsTr("Create “%1”").arg(modelData.label) : modelData.label
                            color: row.marked ? theme.urgent : theme.foreground
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: modelData.create || modelData.agents > 0
                            text: !modelData.create ? qsTr("%n agents", "", modelData.agents)
                                : modelData.found ? modelData.folder
                                : modelData.makeFolder ? qsTr("new folder %1").arg(modelData.folder) : qsTr("pick a folder")
                            color: row.marked ? theme.urgent : picker.dim
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: row.width / 2
                        }
                        // Always takes its place, so rows line up whether or not it shows.
                        Item {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            IconButton {
                                id: trash
                                // On the Create row it picks the folder instead.
                                objectName: modelData.create ? "pickerFolder" : "pickerClose_" + modelData.label
                                anchors.fill: parent
                                visible: modelData.create || row.current || rowHover.hovered || row.marked
                                activeFocusOnTab: false
                                icon: modelData.create ? "\uf07c" : "\uf1f8"
                                tint: row.marked ? theme.urgent : picker.dim
                                busy: row.marked && backend.busy === "close"
                                hint: modelData.create ? qsTr("Pick its folder")
                                    : row.marked ? qsTr("Click again to close it") : qsTr("Close this workspace")
                                shortcut: modelData.create ? "Ctrl+O" : "Ctrl+D"
                                onClicked: modelData.create ? picker.chooseFolder() : picker.remove(row.index)
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("herdr has no workspaces.")
                    color: picker.dim
                }
            }

            Text {
                Layout.fillWidth: true
                text: picker.armedItem
                      ? qsTr("Ctrl+D or Enter closes “%1” and everything in it · Esc keeps it").arg(picker.armedItem.label)
                      : picker.current && picker.current.create && picker.current.makeFolder
                      ? qsTr("Enter makes the folder, creates it there and starts %1 · Ctrl+O picks another").arg(backend.agentKind)
                      : picker.current && picker.current.create && picker.current.found
                      ? qsTr("Enter creates it there and starts %1 · Ctrl+O picks another folder").arg(backend.agentKind)
                      : picker.current && picker.current.create
                      ? qsTr("Enter or Ctrl+O picks its folder, then %1 starts there · Esc closes").arg(backend.agentKind)
                      : qsTr("Enter starts it in a new tab · Ctrl+D closes the workspace · Esc closes")
                color: picker.armedItem ? theme.urgent : picker.dim
                elide: Text.ElideRight
            }
        }
    }
}
