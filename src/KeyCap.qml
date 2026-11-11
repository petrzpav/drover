import QtQuick

// A key the user can press for an agent that asks something, drawn as a
// small keycap.
IconButton {
    property string key: ""
    border.width: 1
    border.color: Qt.alpha(theme.foreground, activeFocus ? 0.6 : 0.25)
}
