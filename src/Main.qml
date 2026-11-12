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

    // The indicator keeps its distance from the right and bottom edges, on
    // whole pixels: anything drawn at a fraction of one (the terminal above
    // all) comes out blurred.
    readonly property int pillX: Math.round(clamp(width - pillWidth - surface.offset.x, 0, width - pillWidth))
    readonly property int pillY: Math.round(clamp(height - pillHeight - surface.offset.y, 0, height - pillHeight))
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
        return Qt.rect(Math.round(x), lower ? py - gap - height : py + pillHeight + gap, fullWidth, height)
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

    // The keyboard goes to the agent's terminal whenever the panel opens or
    // an agent is picked; the terminal is created a moment later.
    function focusForAgent() {
        Qt.callLater(panel.focusTerminal)
    }

    onExpandedChanged: {
        surface.setExpanded(expanded)
        panel.helpVisible = false
        panel.picker.close()
        if (expanded)
            focusForAgent()
    }

    // Esc belongs to the agent while its terminal has the keyboard (it
    // interrupts Claude, closes menus); otherwise it closes drover's own
    // layers, then the panel.
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut
               enabled: win.expanded && (panel.picker.open || panel.helpVisible || !panel.terminalFocused)
               onActivated: {
                   if (panel.picker.open) {
                       if (panel.picker.cancel())
                           return
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
    Shortcut { sequence: "Alt+Up"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: { backend.selectNext(-1); win.focusForAgent() } }
    Shortcut { sequence: "Alt+Down"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: { backend.selectNext(1); win.focusForAgent() } }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: win.expanded; autoRepeat: false; context: Qt.ApplicationShortcut; onActivated: backend.jump() }

    Connections {
        target: backend
        function onJumped() { win.expanded = false }
        // A newly selected agent (picked, or started from the picker) gets
        // the keyboard, unless the picker is still open.
        function onSelectedChanged() {
            if (win.expanded && !panel.picker.open)
                win.focusForAgent()
        }
    }

    AgentPanel {
        id: panel
        objectName: "panel"
        visible: win.expanded
        x: win.panelRect.x
        y: win.panelRect.y
        width: win.panelRect.width
        height: win.panelRect.height
        attached: win.expanded
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
