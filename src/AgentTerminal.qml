import QtQuick
import QMLTermWidget

// A real terminal attached to the agent's pane (`herdr terminal attach`), so
// every key works as in herdr itself: Shift+Tab, Esc, Ctrl+C, the agent's own
// menus. While attached, the pane takes this terminal's size; it goes back
// to herdr's when the terminal goes away (the panel closes, another agent is
// picked).
Item {
    id: root
    required property string terminalId
    signal detached()

    function focusTerminal() {
        terminal.forceActiveFocus()
    }
    readonly property bool terminalFocused: terminal.activeFocus

    QMLTermWidget {
        id: terminal
        objectName: "agentTerminal"
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.topMargin: 6
        anchors.rightMargin: scrollbar.width + 6
        anchors.bottomMargin: 6
        font: Qt.application.font
        colorScheme: theme.terminalScheme
        antialiasText: true
        enableBold: true
        fullCursorHeight: true
        // Painted on the GPU: a full terminal repainted as an image on every
        // keystroke adds noticeable delay at this size.
        renderTarget: QQuickPaintedItem.FramebufferObject

        session: QMLTermSession {
            id: session
            shellProgram: backend.herdrPath
            shellProgramArgs: ["terminal", "attach", root.terminalId, "--takeover"]
            onFinished: root.detached()
        }

        Component.onCompleted: session.startShellProgram()
    }

    QMLTermScrollbar {
        id: scrollbar
        terminal: terminal
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 8
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: Qt.alpha(theme.foreground, 0.3)
        }
    }

    // A click gives the terminal the keyboard; the click itself goes on to
    // the terminal (selection, the agent's own mouse handling).
    MouseArea {
        anchors.fill: terminal
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => {
            terminal.forceActiveFocus()
            mouse.accepted = false
        }
        onWheel: (wheel) => wheel.accepted = false
    }
}
