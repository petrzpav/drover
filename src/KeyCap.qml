import QtQuick

// A key the user can press for a blocked agent, shown as a small keycap.
IconButton {
    property string key: ""
    implicitWidth: Math.max(34, label.length * 8 + 20)
    border.width: 1
    border.color: Qt.alpha(theme.foreground, activeFocus ? 0.6 : 0.25)
}
