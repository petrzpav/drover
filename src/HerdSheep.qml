import QtQuick

// The herd as sheep (indicator = sheep): a pixel sheep per agent, in the
// color of its state. Working sheep wander the pasture; one that finishes
// runs to the fence, jumps into the pen and grazes there; one that asks you
// stands still under a blinking question mark; idle ones sleep. A sheep
// whose agent goes back to work jumps out again.
Item {
    id: field
    objectName: "herdSheep"
    signal flash(color tint)

    // Each agent's sheep, by pane: its state and where it is.
    property var flock: ({})
    // Sheep standing in the pen.
    property int penned: 0
    // One sheep pixel, in screen pixels; the field is 86 × 16 of them.
    readonly property int px: 2
    property real lastTick: 0

    readonly property var sprites: ({
        walk1: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", ".L..L..."],
        walk2: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", "..L..L.."],
        stand: ["..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW..", ".L...L.."],
        graze: ["..WWW...", ".WWWWW..", "WWWWWWH.", ".WWWWWHH", ".L...L.."],
        lie:   ["........", "..WWW...", ".WWWWWHH", "WWWWWWHE", ".WWWWW.."]
    })
    readonly property var glyphs: ({
        "?": ["###", "..#", ".#.", "...", ".#."],
        "z": ["####", "..#.", ".#..", "####"]
    })

    // The pasture ends at x 56 (a sheep is 8 wide), the fence stands at 65,
    // the pen holds three places.
    function penSlot(i) { return 68 + (Math.max(i, 0) % 3) * 5 }

    function add(pane, status, index) {
        const inPen = status === "done"
        flock[pane] = {
            status: status, index: index, inPen: inPen, jump: null, lift: 0, pose: "stand",
            x: inPen ? 68 + Math.random() * 10 : 2 + Math.random() * 50,
            dir: Math.random() < 0.5 ? 1 : -1,
            legT: Math.random() * 2, zT: Math.random() * 2.4
        }
    }
    function change(pane, status) {
        const sheep = flock[pane]
        if (!sheep)
            return
        const before = sheep.status
        sheep.status = status
        if (status === "done" && before === "working")
            field.flash(theme.success)
        else if (status === "blocked")
            field.flash(theme.urgent)
    }

    function move(sheep, s, t, penIndex) {
        if (sheep.jump) {
            const p = Math.min(1, (t - sheep.jump.t0) / sheep.jump.duration)
            sheep.x = sheep.jump.x0 + (sheep.jump.x1 - sheep.jump.x0) * p
            sheep.lift = Math.sin(Math.PI * p) * 7
            sheep.pose = "walk1"
            if (p >= 1) {
                sheep.inPen = sheep.jump.toPen
                sheep.jump = null
                sheep.lift = 0
            }
            return
        }
        function walk(target, speed) {
            const d = target - sheep.x
            if (d)
                sheep.dir = d > 0 ? 1 : -1
            sheep.x += Math.sign(d) * Math.min(Math.abs(d), speed * s)
            sheep.legT += s * speed / 4
            sheep.pose = "walk"
            return Math.abs(target - sheep.x) < 0.01
        }
        const wantPen = sheep.status === "done"
        if (wantPen !== sheep.inPen) {
            if (!sheep.inPen) {
                if (walk(56, 34))
                    sheep.jump = { x0: 56, x1: penSlot(penIndex), t0: t, duration: 520, toPen: true }
            } else if (walk(68, 20)) {
                sheep.jump = { x0: 68, x1: 50, t0: t, duration: 520, toPen: false }
            }
            return
        }
        if (sheep.inPen) {
            const slot = penSlot(penIndex)
            if (Math.abs(slot - sheep.x) > 0.5) {
                walk(slot, 12)
            } else {
                sheep.pose = "graze"
                sheep.dir = 1
            }
            return
        }
        if (sheep.status === "working") {
            if (Math.random() < 0.25 * s)
                sheep.dir *= -1
            let x = sheep.x + sheep.dir * 9 * s
            if (x < 1 || x > 56) {
                sheep.dir *= -1
                x = Math.max(1, Math.min(56, x))
            }
            sheep.x = x
            sheep.legT += s * 6
            sheep.pose = "walk"
        } else {
            sheep.pose = sheep.status === "blocked" ? "stand" : "lie"
        }
    }

    function sorted() {
        return Object.values(flock).sort((a, b) => a.index - b.index)
    }

    function tick() {
        const t = Date.now()
        const s = lastTick ? Math.min(0.2, (t - lastTick) / 1000) : 0.05
        lastTick = t
        const all = sorted()
        const done = all.filter(sheep => sheep.status === "done")
        let pen = 0
        for (const sheep of all) {
            move(sheep, s, t, done.indexOf(sheep))
            if (sheep.inPen && !sheep.jump)
                ++pen
        }
        penned = pen
        canvas.requestPaint()
    }

    function glyph(ctx, name, x, y, size, color, alpha) {
        ctx.globalAlpha = alpha
        ctx.fillStyle = color
        const rows = glyphs[name]
        for (let j = 0; j < rows.length; ++j)
            for (let i = 0; i < rows[j].length; ++i)
                if (rows[j][i] === "#")
                    ctx.fillRect(x + i * size, y + j * size, size, size)
        ctx.globalAlpha = 1
    }

    function drawSheep(ctx, sheep, t) {
        const color = sheep.status === "working" ? theme.accent : sheep.status === "blocked" ? theme.urgent
                    : sheep.status === "done" ? theme.success : Qt.alpha(theme.foreground, 0.45)
        const pose = sheep.pose === "walk" ? (Math.floor(sheep.legT) % 2 ? "walk2" : "walk1") : sheep.pose
        const sprite = sprites[pose]
        const x0 = Math.round(sheep.x), y0 = 10 - Math.round(sheep.lift)
        for (let j = 0; j < 5; ++j) {
            for (let i = 0; i < 8; ++i) {
                const c = sprite[j][sheep.dir > 0 ? i : 7 - i]
                if (c === ".")
                    continue
                ctx.fillStyle = c === "W" ? color : c === "E" ? theme.background : theme.foreground
                ctx.globalAlpha = c === "L" ? 0.8 : 1
                ctx.fillRect((x0 + i) * px, (y0 + j) * px, px, px)
            }
        }
        ctx.globalAlpha = 1
        if (sheep.status === "blocked" && !sheep.inPen && !sheep.jump && Math.floor(t / 550) % 2 === 0)
            glyph(ctx, "?", (x0 + (sheep.dir > 0 ? 5 : 0)) * px, 3 * px - 4, px, theme.urgent, 1)
        if (pose === "lie") {
            const k = ((t / 1000 + sheep.zT) % 2.4) / 2.4
            glyph(ctx, "z", (x0 + (sheep.dir > 0 ? 6 : -1)) * px + Math.round(k * 4) * sheep.dir,
                  Math.round(17 - k * 15), 1, theme.muted, 1 - k)
        }
    }

    Repeater {
        model: backend.agents
        delegate: Item {
            required property string paneId
            required property string status
            required property int index
            visible: false
            Component.onCompleted: field.add(paneId, status, index)
            Component.onDestruction: delete field.flock[paneId]
            onStatusChanged: field.change(paneId, status)
            onIndexChanged: if (field.flock[paneId]) field.flock[paneId].index = index
        }
    }

    Timer {
        interval: 50
        repeat: true
        running: field.visible && backend.agents.count > 0
        onTriggered: field.tick()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 2

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const t = Date.now()
            // The ground and a few tufts of grass.
            ctx.globalAlpha = 0.35
            ctx.fillStyle = theme.muted
            for (let x = 0; x < 86; x += 3)
                ctx.fillRect(x * px, 15 * px, px, px)
            for (const x of [6, 19, 31, 44, 52]) {
                ctx.fillRect(x * px, 14 * px, px, px)
                ctx.fillRect((x + 1) * px, 13 * px, px, px)
                ctx.fillRect((x + 2) * px, 14 * px, px, px)
            }
            ctx.globalAlpha = 1
            const all = field.sorted()
            // Sheep in the pen stand behind its fence; the rest in front.
            for (const sheep of all)
                if (sheep.inPen && !sheep.jump)
                    field.drawSheep(ctx, sheep, t)
            ctx.fillStyle = theme.muted
            for (const x of [65, 75, 85])
                ctx.fillRect(x * px, 9 * px, px, 7 * px)
            ctx.fillRect(65 * px, 11 * px, 21 * px, px)
            ctx.fillRect(65 * px, 13 * px, 21 * px, px)
            for (const sheep of all)
                if (!sheep.inPen || sheep.jump)
                    field.drawSheep(ctx, sheep, t)
        }
    }
}
