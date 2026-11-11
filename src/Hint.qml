import QtQuick
import QtQuick.Controls

// A tooltip drawn like the Omarchy shell's: the theme background inside the
// active border.
ToolTip {
    id: tip
    delay: 600
    padding: 8
    contentItem: Text {
        text: tip.text
        color: theme.foreground
    }
    background: Rectangle {
        color: theme.background
        radius: theme.radius
        border.width: 1
        border.color: theme.border
    }
}
