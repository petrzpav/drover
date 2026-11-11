import QtQuick

// A key the user can press for a blocked agent, shown as a small keycap.
IconButton {
    property string key: ""
    implicitWidth: Math.max(implicitHeight, label.length * 10 + 20)
    border.width: 1
    border.color: Qt.alpha(theme.foreground, activeFocus ? 0.6 : 0.25)
}
