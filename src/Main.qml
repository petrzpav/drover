import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A layer surface in the bottom-right corner of every workspace: the
// indicator alone, or the indicator with the agent panel opened beside it.
ApplicationWindow {
    id: win
    visible: false
    title: "drover"
    color: "transparent"
    flags: Qt.FramelessWindowHint

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool expanded: false
    readonly property int pillWidth: 176
    readonly property int pillHeight: 36
    readonly property int panelWidth: 460
    readonly property int panelHeight: 540
    readonly property int gap: 8

    // The surface is anchored to the corner, so growing it opens the panel to
    // the left of the indicator without moving the indicator.
    width: expanded ? pillWidth + gap + panelWidth : pillWidth
    height: expanded ? panelHeight : pillHeight

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

    Shortcut { sequence: "Esc"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: win.expanded = false }
    Shortcut { sequence: "Space"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: win.expanded = false }
    Shortcut { sequence: "?"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: panel.helpVisible = !panel.helpVisible }
    Shortcut { sequence: "Q"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Up"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(-1) }
    Shortcut { sequence: "Down"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(1) }
    Shortcut { sequence: "Ctrl+O"; enabled: win.expanded; context: Qt.ApplicationShortcut; onActivated: backend.jump() }
    Shortcut { sequence: "Return"; enabled: win.expanded && !panel.messageField.activeFocus; context: Qt.ApplicationShortcut
               onActivated: panel.blocked ? backend.sendKeys(["enter"]) : backend.jump() }

    // Digits answer the question of a blocked agent.
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: String(index + 1)
            enabled: win.expanded && panel.blocked && !panel.messageField.activeFocus
            context: Qt.ApplicationShortcut
            onActivated: backend.sendKeys([String(index + 1)])
        }
    }

    Connections {
        target: backend
        function onJumped() { win.expanded = false }
        // The message field hides while the agent asks something; keep the
        // keys working by moving focus off it.
        function onSelectionChanged() {
            if (panel.blocked && panel.messageField.activeFocus)
                keys.forceActiveFocus()
        }
    }

    Item { id: keys; focus: true }

    AgentPanel {
        id: panel
        visible: win.expanded
        x: 0
        y: 0
        width: win.panelWidth
        height: win.panelHeight
        onQuitRequested: Qt.quit()

        // Right-to-left languages mirror the panel's content; the Translator
        // sets the application's layout direction.
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
    }

    HerdPill {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: win.pillWidth
        height: win.pillHeight
        expanded: win.expanded
        onToggled: win.expanded = !win.expanded
    }
}
