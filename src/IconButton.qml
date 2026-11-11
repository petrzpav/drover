import QtQuick

// A square button with one Nerd Font glyph or a short label, centered in its
// own box, so hover and focus sit exactly on the icon. Drawn like the Omarchy
// shell's controls: a faint foreground fill on hover, a thin border on focus.
Rectangle {
    id: button
    property string icon: ""
    property string label: ""
    property color tint: theme.foreground
    property string hint: ""
    signal clicked()

    implicitWidth: label === "" ? 30 : content.implicitWidth + 20
    implicitHeight: 30
    radius: Math.min(4, theme.radius)
    opacity: enabled ? 1 : 0.4
    color: !enabled ? "transparent"
         : tap.pressed ? Qt.alpha(theme.foreground, 0.22)
         : hover.hovered ? Qt.alpha(theme.foreground, 0.08)
         : "transparent"
    border.width: activeFocus ? 1 : 0
    border.color: Qt.alpha(theme.foreground, 0.4)

    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: hint !== "" ? hint : label
    Keys.onSpacePressed: if (enabled) clicked()
    Keys.onReturnPressed: if (enabled) clicked()

    HoverHandler { id: hover; enabled: button.enabled; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; enabled: button.enabled; onTapped: button.clicked() }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6
        Text {
            visible: button.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: button.icon
            color: button.tint
            font.family: "monospace"
            font.pixelSize: 15
        }
        Text {
            visible: button.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: button.label
            color: button.tint
            font.family: "monospace"
            font.pixelSize: 13
        }
    }
}
