import QtQuick
import QtQuick.Controls
import QtQuick.Window

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
    readonly property real pillX: Math.max(0, Math.min(width - pillWidth, width - pillWidth - surface.offset.x))
    readonly property real pillY: Math.max(0, Math.min(height - pillHeight, height - pillHeight - surface.offset.y))

    // Big enough to follow an agent's work, and never past the screen.
    readonly property int panelWidth: Math.max(560, Math.min(1180, width - pillWidth - gap - 2 * edge))
    readonly property int panelHeight: Math.max(400, Math.min(820, height - 2 * edge))
    // Beside the indicator, on the side with room, and level with it: bottoms
    // aligned in the lower half of the screen, tops in the upper half.
    readonly property bool panelOnLeft: pillX - gap - panelWidth >= edge
                                        || pillX + pillWidth / 2 > width / 2
    readonly property real panelX: clamp(panelOnLeft ? pillX - gap - panelWidth : pillX + pillWidth + gap,
                                         edge, width - panelWidth - edge)
    readonly property real panelY: clamp(pillY + pillHeight / 2 > height / 2 ? pillY + pillHeight - panelHeight : pillY,
                                         edge, height - panelHeight - edge)

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    readonly property var inputRegion: [pillX, pillY, panelX, panelY, panelWidth, panelHeight, expanded]
    onInputRegionChanged: surface.setInputRegion(Qt.rect(pillX, pillY, pillWidth, pillHeight),
                                                 expanded ? Qt.rect(panelX, panelY, panelWidth, panelHeight)
                                                          : Qt.rect(0, 0, 0, 0))

    function setExpanded(value) {
        expanded = value
    }

    onExpandedChanged: {
        surface.setExpanded(expanded)
        backend.active = expanded
        panel.helpVisible = false
        if (expanded && backend.selectedStatus !== "blocked")
            panel.messageField.forceActiveFocus()
        else
            keys.forceActiveFocus()
    }

    readonly property bool typing: panel.messageField.activeFocus

    Shortcut { sequence: "Esc"; enabled: win.expanded; context: Qt.ApplicationShortcut
               onActivated: panel.helpVisible ? panel.helpVisible = false : win.expanded = false }
    Shortcut { sequence: "Space"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut; onActivated: win.expanded = false }
    Shortcut { sequence: "?"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut; onActivated: panel.helpVisible = !panel.helpVisible }
    Shortcut { sequence: "Q"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Alt+Up"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(-1) }
    Shortcut { sequence: "Alt+Down"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(1) }
    Shortcut { sequence: "Up"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(-1) }
    Shortcut { sequence: "Down"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(1) }
    Shortcut { sequence: "Ctrl+O"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.jump() }
    Shortcut { sequence: "Return"; enabled: win.expanded && !win.typing; context: Qt.ApplicationShortcut
               onActivated: panel.blocked ? backend.sendKeys(["enter"]) : panel.messageField.forceActiveFocus() }

    // Digits answer the question of a blocked agent.
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: String(index + 1)
            enabled: win.expanded && panel.blocked && !win.typing
            context: Qt.ApplicationShortcut
            onActivated: backend.sendKeys([String(index + 1)])
        }
    }

    Connections {
        target: backend
        function onJumped() { win.expanded = false }
        // The message box hides while the agent asks something; keep the
        // keys working by moving focus off it, and come back to it after.
        function onSelectionChanged() {
            if (!win.expanded)
                return
            if (panel.blocked && win.typing)
                keys.forceActiveFocus()
        }
    }

    Item { id: keys; focus: true }

    AgentPanel {
        id: panel
        visible: win.expanded
        x: win.panelX
        y: win.panelY
        width: win.panelWidth
        height: win.panelHeight
        onQuitRequested: Qt.quit()

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
