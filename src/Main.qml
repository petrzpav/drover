import QtQuick
import QtQuick.Controls

// A transparent layer surface over every workspace, with the indicator where
// the user dragged it (the bottom-right corner at first) and the agent panel
// opened beside it, on whichever side has room. Only those two take input.
ApplicationWindow {
    id: win
    visible: false
    title: "drover"
    color: "transparent"
    flags: Qt.FramelessWindowHint

    property bool expanded: false
    readonly property int pillWidth: 176
    readonly property int pillHeight: 36
    readonly property int gap: 8
    readonly property int edge: 12

    // The indicator keeps its distance from the right and bottom edges.
    readonly property real pillX: clamp(width - pillWidth - surface.offset.x, 0, width - pillWidth)
    readonly property real pillY: clamp(height - pillHeight - surface.offset.y, 0, height - pillHeight)
    readonly property rect panelRect: placePanel(width, height, pillX, pillY)

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    // Beside the indicator on the side with room, level with it (bottoms
    // aligned in the lower half of the screen, tops in the upper half); when
    // neither side has room, above or below it. Never over the indicator.
    function placePanel(w, h, px, py) {
        const fullWidth = Math.min(1400, w - 2 * edge)
        const fullHeight = Math.min(820, h - 2 * edge)
        const lower = py + pillHeight / 2 > h / 2
        const levelY = clamp(lower ? py + pillHeight - fullHeight : py, edge, h - fullHeight - edge)
        const roomLeft = px - gap - edge
        const roomRight = w - edge - (px + pillWidth + gap)
        const minimumWidth = 720
        if (Math.max(roomLeft, roomRight) >= minimumWidth) {
            const side = Math.min(fullWidth, Math.max(roomLeft, roomRight))
            const x = roomLeft >= roomRight ? px - gap - side : px + pillWidth + gap
            return Qt.rect(x, levelY, side, fullHeight)
        }
        const room = lower ? py - gap - edge : h - edge - (py + pillHeight + gap)
        const height = Math.max(240, Math.min(fullHeight, room))
        const x = clamp(px + pillWidth / 2 - fullWidth / 2, edge, w - fullWidth - edge)
        return Qt.rect(x, lower ? py - gap - height : py + pillHeight + gap, fullWidth, height)
    }

    readonly property var inputRegion: [pillX, pillY, panelRect, expanded]
    onInputRegionChanged: surface.setInputRegion(Qt.rect(pillX, pillY, pillWidth, pillHeight),
                                                 expanded ? panelRect : Qt.rect(0, 0, 0, 0))

    function setExpanded(value) {
        expanded = value
    }

    function openPicker() {
        expanded = true
        panel.helpVisible = false
        panel.picker.show()
    }

    // The keyboard goes to the message box, or for an agent that asks
    // something, to its answer keys, but only after the user chose that
    // agent. Otherwise it rests on `keys`, where typing does nothing.
    function focusForAgent() {
        if (panel.blocked)
            panel.answerBar.forceActiveFocus()
        else
            panel.messageField.forceActiveFocus()
    }

    onExpandedChanged: {
        surface.setExpanded(expanded)
        backend.active = expanded
        panel.helpVisible = false
        panel.picker.close()
        if (expanded && !panel.blocked)
            panel.messageField.forceActiveFocus()
        else
            keys.forceActiveFocus()
    }

    readonly property bool resting: keys.activeFocus

    Shortcut { sequence: "Esc"; enabled: win.expanded; context: Qt.ApplicationShortcut
               onActivated: {
                   if (panel.picker.open) {
                       panel.picker.close()
                       win.focusForAgent()
                   } else if (panel.helpVisible) {
                       panel.helpVisible = false
                   } else {
                       win.expanded = false
                   }
               } }
    Shortcut { sequence: "Ctrl+P"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: win.openPicker() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "F1"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: panel.helpVisible = !panel.helpVisible }
    Shortcut { sequence: "?"; enabled: win.expanded && win.resting; context: Qt.ApplicationShortcut; onActivated: panel.helpVisible = !panel.helpVisible }
    Shortcut { sequence: "Alt+Up"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: { backend.selectNext(-1); win.focusForAgent() } }
    Shortcut { sequence: "Alt+Down"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: { backend.selectNext(1); win.focusForAgent() } }
    Shortcut { sequence: "Up"; enabled: win.expanded && win.resting; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(-1) }
    Shortcut { sequence: "Down"; enabled: win.expanded && win.resting; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(1) }
    Shortcut { sequence: "Ctrl+O"; enabled: win.expanded; autoRepeat: false; context: Qt.ApplicationShortcut; onActivated: backend.jump() }

    Connections {
        target: backend
        function onJumped() { win.expanded = false }
        // The agent the picker started is selected; talk to it right away.
        function onSelectedChanged() {
            if (win.expanded && !panel.picker.open && win.resting)
                win.focusForAgent()
        }
        // An agent that starts asking while the user types takes nothing
        // from the keyboard: it rests until the user moves to the answer
        // keys. Once the agent stops asking, the message box gets it back.
        function onSelectedInfoChanged() {
            if (!win.expanded)
                return
            if (panel.blocked && !panel.answerBar.activeFocus)
                keys.forceActiveFocus()
            else if (!panel.blocked && (panel.answerBar.activeFocus || win.resting))
                panel.messageField.forceActiveFocus()
        }
    }

    Item {
        id: keys
        objectName: "restingFocus"
        focus: true
        Keys.onTabPressed: win.focusForAgent()
    }

    AgentPanel {
        id: panel
        visible: win.expanded
        x: win.panelRect.x
        y: win.panelRect.y
        width: win.panelRect.width
        height: win.panelRect.height
        onQuitRequested: Qt.quit()
        onAgentPicked: win.focusForAgent()

        // Right-to-left languages mirror the panel's content; the Translator
        // sets the application's layout direction.
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
    }

    HerdPill {
        x: win.pillX
        y: win.pillY
        width: win.pillWidth
        height: win.pillHeight
        expanded: win.expanded
        onToggled: win.expanded = !win.expanded

        property point startOffset
        onMoveStarted: startOffset = Qt.point(win.width - win.pillWidth - win.pillX,
                                              win.height - win.pillHeight - win.pillY)
        onMoved: (dx, dy) => surface.offset = Qt.point(
                     win.clamp(startOffset.x - dx, 0, win.width - win.pillWidth),
                     win.clamp(startOffset.y - dy, 0, win.height - win.pillHeight))
        onMoveFinished: surface.save()
    }
}
