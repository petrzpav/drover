import QtQuick

// A button with one Nerd Font glyph or a short label, centered in its own
// box, so hover and focus sit exactly on it. Drawn like the Omarchy shell's
// controls: a faint foreground fill on hover and focus, a thin border on
// focus. While busy, a spinner takes the glyph's place in the same box.
Rectangle {
    id: button
    property string icon: ""
    property string label: ""
    property color tint: theme.foreground
    // A tint change fades rather than flips (the view switch's segments).
    Behavior on tint { ColorAnimation { duration: 160; easing.type: Easing.OutCubic } }
    property string hint: ""
    property string shortcut: ""
    property bool busy: false
    signal clicked()

    implicitWidth: label === "" ? implicitHeight : Math.max(implicitHeight, content.implicitWidth + 20)
    implicitHeight: Math.max(30, content.implicitHeight + 12)
    radius: theme.radius
    opacity: enabled ? 1 : 0.4
    color: !enabled ? "transparent"
         : tap.pressed ? Qt.alpha(theme.foreground, 0.22)
         : hover.hovered || activeFocus ? Qt.alpha(theme.foreground, 0.08)
         : Qt.alpha(theme.foreground, 0)
    Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
    border.width: activeFocus ? 1 : 0
    border.color: Qt.alpha(theme.foreground, 0.25)

    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: hint !== "" ? hint : label

    // A focused button takes its own keys before the window's shortcuts do.
    Keys.onShortcutOverride: (event) => {
        event.accepted = event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            event.accepted = true
            if (!event.isAutoRepeat && !busy)
                clicked()
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: if (!button.busy) button.clicked() }

    Hint {
        visible: hover.hovered && button.hint !== ""
        text: button.shortcut === "" ? button.hint : button.hint + "   " + button.shortcut
    }

    Item {
        id: content
        // Glyph and label side by side: the glyph in a fixed box, so a
        // spinner takes only its place and the label stays put.
        readonly property bool both: button.icon !== "" && button.label !== ""
        readonly property real box: 16
        anchors.centerIn: parent
        implicitWidth: both ? box + 8 + labelText.implicitWidth
                     : button.label !== "" ? labelText.implicitWidth : glyph.implicitWidth
        implicitHeight: Math.max(glyph.implicitHeight, labelText.implicitHeight)
        width: implicitWidth
        height: implicitHeight

        Text {
            id: glyph
            x: content.both ? (content.box - width) / 2 : (content.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            visible: button.icon !== "" || button.busy
            text: button.busy ? "" : button.icon
            color: button.tint
            RotationAnimator on rotation {
                running: button.busy
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
            }
            onVisibleChanged: rotation = 0
        }
        Text {
            id: labelText
            x: content.both ? content.box + 8 : (content.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            visible: button.label !== "" && (!button.busy || content.both)
            text: button.label
            color: button.tint
        }
    }
}
