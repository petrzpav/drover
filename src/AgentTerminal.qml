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
    // The editor beside the agent rather than the agent: it gets Ctrl+P (the
    // desktop's binding is forwarded) and Ctrl+Shift+letters as such.
    property bool editor: false
    // The terminal below the editor: Ctrl+; hides it, Ctrl+W closes it.
    property bool shell: false
    signal detached()

    function focusTerminal() {
        terminal.forceActiveFocus()
    }
    function sendControl(letter) {
        terminal.sendControl(letter)
    }
    readonly property bool terminalFocused: terminal.activeFocus

    TerminalView {
        id: terminal
        objectName: "agentTerminal"
        anchors.fill: parent
        // The editor and its terminal fill their side to the edge, like the
        // editor's own bars; the agent's text keeps a margin.
        anchors.leftMargin: root.editor || root.shell ? 0 : 16
        anchors.topMargin: root.editor || root.shell ? 0 : 6
        anchors.rightMargin: root.editor || root.shell ? 0 : 6
        anchors.bottomMargin: root.editor || root.shell ? 0 : 6
        font: Qt.application.font
        background: theme.background
        foreground: theme.foreground
        selectionColor: Qt.alpha(theme.foreground, 0.35)
        colors: theme.terminalPalette
        program: backend.herdrPath
        arguments: ["terminal", "attach", root.terminalId, "--takeover"]
        // drover's own shortcuts; every other key belongs to the agent, or
        // the editor, which also takes Ctrl+P (its file finder).
        reservedShortcuts: root.shell ? ["Alt+Up", "Alt+Down", "Alt+E", "Ctrl+;", "Ctrl+W", "Ctrl+Shift+O", "F1", "Ctrl+Q"]
                         : root.editor ? ["Alt+Up", "Alt+Down", "Alt+E", "Ctrl+;", "Ctrl+Shift+O", "F1", "Ctrl+Q"]
                                       : ["Alt+Up", "Alt+Down", "Alt+E", "Ctrl+P", "Ctrl+Shift+O", "F1", "Ctrl+Q"]
        extendedKeys: root.editor && backend.editor === "fresh"
        doubleCtrlB: true
        onFinished: root.detached()
        Component.onCompleted: start()
    }
}
