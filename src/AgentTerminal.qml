import QtQuick
import Drover

// A terminal attached to the agent's pane (`herdr terminal attach`), so every
// key works as in herdr itself: Shift+Tab, Esc, Ctrl+C, the agent's own
// menus. While attached, the pane takes this terminal's size; it goes back to
// herdr's when the terminal goes away (the panel closes, another agent is
// picked). The wheel and clicks go to herdr, which scrolls the agent's
// history; Shift+drag selects, Ctrl+Shift+C and Ctrl+Shift+V copy and paste.
Item {
    id: root
    required property string terminalId
    signal detached()

    function focusTerminal() {
        terminal.forceActiveFocus()
    }
    readonly property bool terminalFocused: terminal.activeFocus

    TerminalView {
        id: terminal
        objectName: "agentTerminal"
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.topMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: 6
        font: Qt.application.font
        background: theme.background
        foreground: theme.foreground
        selectionColor: Qt.alpha(theme.foreground, 0.35)
        colors: theme.terminalPalette
        program: backend.herdrPath
        arguments: ["terminal", "attach", root.terminalId, "--takeover"]
        // drover's own shortcuts; every other key belongs to the agent.
        reservedShortcuts: ["Alt+Up", "Alt+Down", "Ctrl+P", "Ctrl+Shift+O", "F1", "Ctrl+Q"]
        onFinished: root.detached()
        Component.onCompleted: start()
    }
}
