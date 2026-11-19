import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Ctrl+P: the running agents first, to go to one without the agent list;
// then herdr's workspaces, where the default agent starts in a new tab.
// Typing narrows both. A card over the panel, so nothing behind it moves;
// Up and Down walk the matches, Enter goes or starts, Esc closes. A name no
// workspace has yet adds a row that creates it, in a folder picked in the
// desktop's folder picker (Ctrl+O or the row's folder button; Enter too,
// unless the text already names a folder, or a new one in an existing
// folder, which Enter makes). Ctrl+D (or the row's trash
// button) closes a workspace after a second press confirms it.
Rectangle {
    id: picker
    property bool open: false
    // The agent the user went to, by pane.
    signal agentChosen(string pane)
    // The panel's words for an agent's state.
    property var statusLabel: (status) => status
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
        // At once, so an Enter right behind Ctrl+P already has its row.
        restoreHighlight()
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
        if (item && item.agent) {
            armed = ""
            close()
            agentChosen(item.pane)
            return
        }
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
        if (!item || item.create || item.agent || backend.busy !== "")
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

    // The agents whose title or workspace has the text (what their rows
    // show); then the workspaces by substring, those starting with the text
    // first; then, unless a workspace has exactly that name, a row that
    // creates it.
    readonly property var agentMatches: {
        const text = field.text.trim().toLowerCase()
        const found = []
        for (const agent of backend.agents.rows)
            if (text === "" || (agent.title + " " + agent.workspace).toLowerCase().includes(text))
                found.push({ id: "agent:" + agent.paneId, agent: true, pane: agent.paneId, label: agent.title,
                             status: agent.status, workspace: agent.workspace, cwd: agent.cwd })
        return found
    }
    readonly property var matches: agentMatches.concat(workspaceMatches)
    readonly property var workspaceMatches: {
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
            found.push({ id: "create:", create: true, label: fresh.label, folder: fresh.folder, found: fresh.found,
                         makeFolder: fresh.makeFolder, agents: 0 })
        }
        return found
    }
    // The row the user moved the highlight to, by id; empty means the
    // default row. A refresh keeps the highlight on it while it exists.
    property string highlighted: ""
    // Where the highlight starts. With nothing typed: the agent the user was
    // at before the one in front, else the first other agent, so Ctrl+P,
    // Enter switches back and forth like Alt+Tab. With text: a workspace it
    // names, so typing a workspace and Enter still starts an agent there;
    // else an agent; else the row that creates the workspace.
    function defaultIndex() {
        let index = -1
        if (field.text.trim() === "") {
            index = matches.findIndex(item => item.agent && item.pane === previousAgent
                                                && item.pane !== backend.selected)
            if (index < 0)
                index = matches.findIndex(item => item.agent && item.pane !== backend.selected)
        } else {
            index = matches.findIndex(item => !item.agent && !item.create)
            if (index < 0)
                index = matches.findIndex(item => item.agent)
        }
        return Math.max(0, index)
    }
    function restoreHighlight() {
        let index = -1
        if (highlighted !== "")
            index = matches.findIndex(item => item.id === highlighted)
        list.currentIndex = index >= 0 ? index : defaultIndex()
    }
    function moveHighlight(index) {
        list.currentIndex = Math.max(0, Math.min(list.count - 1, index))
        highlighted = matches[list.currentIndex] ? matches[list.currentIndex].id : ""
    }
    // The agent in front before the current one.
    property string previousAgent: ""
    property string currentAgent: ""
    Component.onCompleted: currentAgent = backend.selected
    Connections {
        target: backend
        function onSelectedChanged() {
            if (picker.currentAgent !== backend.selected && picker.currentAgent !== "")
                picker.previousAgent = picker.currentAgent
            picker.currentAgent = backend.selected
        }
    }
    // The view gets a new list only when its rows change, not their data:
    // an agent's status changing every few seconds neither rebuilds the rows
    // nor moves the list under the pointer. Each row reads its data from
    // matches. The view resets its index when it gets a new list, so the
    // highlight comes back after that.
    property var rowKeys: []
    onMatchesChanged: {
        const keys = matches.map(item => item.create ? "create:" + item.label : item.id)
        if (keys.join("\n") === rowKeys.join("\n"))
            return
        rowKeys = keys
        restoreHighlight()
        Qt.callLater(restoreHighlight)
    }
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
                text: picker.agentMatches.length > 0 || backend.agents.count > 0
                      ? qsTr("Go to an agent, or start %1 in a workspace").arg(backend.agentKind)
                      : qsTr("Start %1 in a workspace").arg(backend.agentKind)
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
                        text: backend.agents.count > 0 ? qsTr("Agent or workspace") : qsTr("Workspace")
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
                model: picker.rowKeys
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Column {
                    id: entry
                    required property int index
                    readonly property var modelData: picker.matches[index] ?? ({})
                    readonly property bool current: ListView.isCurrentItem
                    readonly property bool marked: !modelData.create && !modelData.agent && modelData.id === picker.armed
                    // Each group under its own heading, once there are agents
                    // to tell from the workspaces.
                    readonly property string heading: picker.agentMatches.length === 0 ? ""
                        : index === 0 && modelData.agent ? qsTr("Agents")
                        : !modelData.agent && (index === 0 || picker.matches[index - 1].agent) ? qsTr("Workspaces")
                        : ""
                    width: ListView.view.width
                    spacing: 2

                    Text {
                        visible: entry.heading !== ""
                        leftPadding: 12
                        topPadding: entry.index === 0 ? 0 : 8
                        bottomPadding: 2
                        text: entry.heading
                        color: picker.dim
                    }

                    Rectangle {
                        id: row
                        readonly property var modelData: entry.modelData
                        readonly property int index: entry.index
                        readonly property bool current: entry.current
                        readonly property bool marked: entry.marked
                        width: entry.width
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
                            // An agent's row has no button: its text ends where its
                            // title begins, 12 from the edge.
                            anchors.rightMargin: row.modelData.agent ? 12 : 4
                            spacing: 8
                            StatusDot {
                                visible: row.modelData.agent === true
                                status: row.modelData.status ?? ""
                                size: 8
                            }
                            Text {
                                id: rowText
                                Layout.fillWidth: true
                                text: row.modelData.create ? qsTr("Create “%1”").arg(row.modelData.label) : row.modelData.label
                                color: row.marked ? theme.urgent : theme.foreground
                                elide: Text.ElideRight
                            }
                            // On the right, flush with the workspaces' agent counts: an
                            // agent's state and workspace, as wide as its longest state,
                            // so the title never re-elides when the state changes.
                            Text {
                                visible: row.modelData.agent || row.modelData.create || row.modelData.agents > 0
                                text: row.modelData.agent ? picker.statusLabel(row.modelData.status) + " · " + row.modelData.workspace
                                    : !row.modelData.create ? qsTr("%n agents", "", row.modelData.agents)
                                    : row.modelData.found ? row.modelData.folder
                                    : row.modelData.makeFolder ? qsTr("new folder %1").arg(row.modelData.folder) : qsTr("pick a folder")
                                color: row.marked ? theme.urgent : picker.dim
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideMiddle
                                Layout.preferredWidth: row.modelData.agent
                                    ? Math.ceil(Math.max(...["working", "blocked", "done", "idle", "unknown"].map(
                                          s => statusMetrics.advanceWidth(picker.statusLabel(s) + " · " + row.modelData.workspace))))
                                    : -1
                                Layout.maximumWidth: row.width / 2
                                FontMetrics { id: statusMetrics }
                            }
                            // Always takes its place on a workspace's row, so those
                            // line up whether or not it shows.
                            Item {
                                visible: !row.modelData.agent
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                                IconButton {
                                    id: trash
                                    // On the Create row it picks the folder instead.
                                    objectName: row.modelData.create ? "pickerFolder" : "pickerClose_" + row.modelData.label
                                    anchors.fill: parent
                                    visible: !row.modelData.agent
                                             && (row.modelData.create || row.current || rowHover.hovered || row.marked)
                                    activeFocusOnTab: false
                                    icon: row.modelData.create ? "\uf07c" : "\uf1f8"
                                    tint: row.marked ? theme.urgent : picker.dim
                                    busy: row.marked && backend.busy === "close"
                                    hint: row.modelData.create ? qsTr("Pick its folder")
                                        : row.marked ? qsTr("Click again to close it") : qsTr("Close this workspace")
                                    shortcut: row.modelData.create ? "Ctrl+O" : "Ctrl+D"
                                    onClicked: row.modelData.create ? picker.chooseFolder() : picker.remove(row.index)
                                }
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
                      : picker.current && picker.current.agent
                      ? qsTr("Enter goes to it · Esc closes")
                      : qsTr("Enter starts it in a new tab · Ctrl+D closes the workspace · Esc closes")
                color: picker.armedItem ? theme.urgent : picker.dim
                elide: Text.ElideRight
            }
        }
    }
}
