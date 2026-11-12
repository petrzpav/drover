import QtQuick

// One pixel sheep, drawn like the pill's (HerdSheep), at any size: the one
// on the stage, the one trotting there from the pasture, the one waiting in
// the wings. Its legs move only while it walks.
Canvas {
    id: sheep
    property int px: 3
    property color tint: theme.urgent
    // stand, walk or graze (head down: a nod).
    property string pose: "stand"
    property int dir: 1
    // A blinking question mark above it.
    property bool asking: false

    readonly property var sprites: ({
        walk1: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", ".L..L..."],
        walk2: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", "..L..L.."],
        stand: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", ".L...L.."],
        graze: ["..WWW...", ".WWWWW..", "WWWWWWH.", ".WWWWWHH", ".L...L.."]
    })
    readonly property var mark: ["###", "..#", ".#.", "...", ".#."]
    // Room for the question mark above the sheep.
    readonly property int headroom: 7

    width: 8 * px
    height: (5 + headroom) * px

    property bool step: false
    property bool blink: true
    Timer {
        interval: 110
        repeat: true
        running: sheep.visible && sheep.pose === "walk"
        onTriggered: sheep.step = !sheep.step
    }
    Timer {
        interval: 550
        repeat: true
        running: sheep.visible && sheep.asking
        onTriggered: sheep.blink = !sheep.blink
        onRunningChanged: sheep.blink = true
    }

    onPxChanged: requestPaint()
    onTintChanged: requestPaint()
    onPoseChanged: requestPaint()
    onDirChanged: requestPaint()
    onAskingChanged: requestPaint()
    onStepChanged: requestPaint()
    onBlinkChanged: requestPaint()
    Connections {
        target: theme
        function onChanged() { sheep.requestPaint() }
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const name = pose === "walk" ? (step ? "walk2" : "walk1") : pose
        const sprite = sprites[name] || sprites.stand
        const top = headroom * px
        for (let j = 0; j < 5; ++j) {
            for (let i = 0; i < 8; ++i) {
                const c = sprite[j][dir > 0 ? i : 7 - i]
                if (c === ".")
                    continue
                ctx.fillStyle = c === "W" ? tint : c === "E" ? theme.background : theme.foreground
                ctx.globalAlpha = c === "L" ? 0.8 : 1
                ctx.fillRect(i * px, top + j * px, px, px)
            }
        }
        ctx.globalAlpha = 1
        if (asking && blink) {
            ctx.fillStyle = theme.urgent
            const x0 = (dir > 0 ? 5 : 0) * px
            for (let j = 0; j < mark.length; ++j)
                for (let i = 0; i < 3; ++i)
                    if (mark[j][i] === "#")
                        ctx.fillRect(x0 + i * px, j * px, px, px)
        }
    }
}
