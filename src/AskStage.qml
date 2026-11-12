import QtQuick
import QtQuick.Layouts

// An agent's question on drover's stage, beside the pill. Its sheep comes
// out of the pasture the way the config says (stage = ...): "fence" jumps
// the fence in an arc and lands on the card's corner, "spotlight" trots onto
// the stage and a spotlight comes on above it. After an answer it nods and
// goes back the same way. The question's options are numbered, the last row takes an answer of your own;
// a permission prompt offers yes, yes and don't ask again, no, and no with
// what to do instead.
//
// It takes no keyboard on its own, so typing elsewhere never lands here: a
// click on it, or drover's toggle key, gives it the keyboard. Everything
// inside has a fixed place, so nothing jumps while you pick.
FocusScope {
    id: stage
    objectName: "askStage"

    // Where the pill's sheep stand, in the stage's parent: the entrance
    // starts there and the exit ends there.
    property point origin: Qt.point(0, 0)
    // How the sheep comes on stage: "fence" or "spotlight".
    property string look: "fence"
    readonly property bool fence: look !== "spotlight"
    // The sheep on stage once it has arrived.
    readonly property Item onStage: fence ? perch : star
    // The request shown: a copy of asker.current, kept through the exit.
    property var request: ({})
    readonly property bool shown: request.id !== undefined
    readonly property bool isQuestion: request.kind === "question"
    readonly property var questions: isQuestion ? request.questions : []
    property int step: 0
    property var answers: []
    // Highlighted row and, for a multi-select question, the checked rows.
    property int highlighted: 0
    property var checked: ({})
    // True from an answer until the next request is on stage.
    property bool leaving: false
    readonly property bool hasKeyboard: activeFocus
    signal wantsKeyboard()
    signal releasedKeyboard()
    // Opens the agent in drover's panel.
    signal openAgent(string pane)

    readonly property var question: isQuestion && step < questions.length ? questions[step] : ({})
    readonly property var options: isQuestion ? (question.options || [])
                                 : request.kind === "permission" ? permissionOptions() : []
    // Rows: the options, then the field for your own answer.
    readonly property int rows: options.length + 1

    function permissionOptions() {
        const list = [{ label: qsTr("Yes"), description: "", action: "allow" }]
        if (request.always)
            list.push({ label: qsTr("Yes, and don't ask again"), description: request.always, action: "always" })
        list.push({ label: qsTr("No"), description: qsTr("the agent stops and waits for you"), action: "deny" })
        return list
    }

    width: 460
    height: card.height
    visible: shown || exitFade.running || exitAnswered.running
    opacity: 0

    // A new request (or the next in line) comes on stage once the last one
    // has left.
    Connections {
        target: asker
        function onCurrentChanged() { stage.sync() }
    }
    Component.onCompleted: sync()

    function sync() {
        const next = asker.current
        if (shown && next.id === request.id) {
            request = next  // the title or workspace changed
            return
        }
        if (leaving || exitFade.running || exitAnswered.running)
            return  // the exit brings the next one
        if (shown) {
            // Answered in the terminal, or the agent stopped waiting.
            leave(false)
            return
        }
        if (next.id !== undefined)
            enter(next)
    }

    function enter(next) {
        request = next
        step = 0
        answers = []
        highlighted = 0
        checked = ({})
        leaving = false
        ownAnswer.text = ""
        entrance.restart()
    }

    function leave(answered) {
        leaving = true
        entrance.stop()
        onStage.asking = false
        if (answered && onStage.visible) {
            onStage.pose = "graze"
            onStage.tint = theme.accent
            exitAnswered.restart()
        } else {
            exitFade.restart()
        }
    }

    function finished() {
        leaving = false
        request = ({})
        const next = asker.current
        if (next.id !== undefined) {
            // The next one in line keeps the keyboard, if it was given.
            enter(next)
        } else {
            // Gone for good: the next sheep must be given the keyboard again.
            stage.focus = false
            releasedKeyboard()
        }
    }

    // Hands the keyboard back; the question stays on stage.
    function releaseKeyboard() {
        stage.focus = false
        releasedKeyboard()
    }

    function takeKeyboard() {
        wantsKeyboard()
        stage.forceActiveFocus()
        keys.forceActiveFocus()
    }

    function choose(row) {
        if (leaving || row < 0 || row >= rows)
            return
        highlighted = row
        if (row === options.length) {
            ownAnswer.forceActiveFocus()
            return
        }
        if (isQuestion && question.multiSelect) {
            const next = Object.assign({}, checked)
            next[row] = !next[row]
            checked = next
            return
        }
        if (isQuestion)
            give(options[row].label)
        else
            decide(options[row].action, "")
    }

    // The checked labels of a multi-select question, in their order.
    function confirmChecked() {
        const labels = []
        for (let i = 0; i < options.length; ++i)
            if (checked[i])
                labels.push(options[i].label)
        // Nothing ticked: the highlighted option alone.
        give(labels.length > 0 ? labels.join(", ") : options[highlighted].label)
    }

    function submitOwn() {
        const text = ownAnswer.text.trim()
        if (text === "")
            return
        if (isQuestion)
            give(text)
        else
            decide("deny", text)
    }

    // One question answered: on to the next, or all answers to the agent.
    function give(text) {
        const list = answers.slice(0, step)
        list.push(text)
        answers = list
        if (step + 1 < questions.length) {
            step += 1
            highlighted = 0
            checked = ({})
            ownAnswer.text = ""
            keys.forceActiveFocus()
            return
        }
        const id = request.id
        leave(true)
        asker.answer(id, list)
    }

    function back() {
        if (step === 0)
            return
        step -= 1
        highlighted = 0
        checked = ({})
        ownAnswer.text = ""
    }

    function decide(action, message) {
        const id = request.id
        leave(true)
        if (action === "allow")
            asker.allow(id, false)
        else if (action === "always")
            asker.allow(id, true)
        else
            asker.deny(id, message)
    }

    // Back to the terminal: Claude asks there itself.
    function toTerminal() {
        if (leaving)
            return
        const id = request.id
        leave(false)
        asker.pass(id)
    }

    function openInPanel() {
        const pane = request.pane
        toTerminal()
        openAgent(pane)
    }

    // The entrance. fence: the sheep jumps from the pasture in an arc and
    // lands on the card's corner with a little squash (500 ms), the card
    // fading in on the way. spotlight: it trots to its mark (380 ms), then
    // the spotlight comes on (180 ms).
    SequentialAnimation {
        id: entrance
        ScriptAction {
            script: {
                exitFade.stop()
                exitAnswered.stop()
                stage.opacity = 1
                card.opacity = 0
                spot.opacity = 0
                for (const sheep of [star, perch]) {
                    sheep.visible = false
                    sheep.tint = theme.urgent
                    sheep.pose = "stand"
                    sheep.asking = false
                }
                const from = stage.mapFromItem(stage.parent, stage.origin.x, stage.origin.y)
                const to = stage.onStage.mapToItem(stage, 0, stage.onStage.height)
                actor.fly(from, to, 2 / stage.onStage.px, 1, stage.fence ? 70 : 0, stage.fence ? "walk1" : "walk")
                if (stage.fence)
                    actor.dir = -1
            }
        }
        ParallelAnimation {
            NumberAnimation { target: actor; property: "p"; from: 0; to: 1; duration: stage.fence ? 500 : 380
                              easing.type: stage.fence ? Easing.OutQuad : Easing.InOutQuad }
            SequentialAnimation {
                PauseAnimation { duration: stage.fence ? 260 : 100 }
                NumberAnimation { target: card; property: "opacity"; to: 1; duration: stage.fence ? 200 : 220; easing.type: Easing.OutCubic }
            }
        }
        ScriptAction {
            script: {
                actor.visible = false
                stage.onStage.dir = stage.fence ? -1 : actor.dir
                stage.onStage.visible = true
                stage.onStage.asking = true
            }
        }
        ParallelAnimation {
            // fence: the landing squashes the sheep for a moment.
            NumberAnimation { target: squash; property: "yScale"; from: stage.fence ? 0.82 : 1; to: 1; duration: 100 }
            NumberAnimation { target: squash; property: "xScale"; from: stage.fence ? 1.1 : 1; to: 1; duration: 100 }
            NumberAnimation { target: spot; property: "opacity"; to: stage.fence ? 0 : 1; duration: 180; easing.type: Easing.OutCubic }
        }
    }

    // The exit when the stage goes without an answer from here (answered in
    // the terminal, or the agent stopped waiting): it fades out.
    SequentialAnimation {
        id: exitFade
        NumberAnimation { target: stage; property: "opacity"; to: 0; duration: 150; easing.type: Easing.OutCubic }
        ScriptAction { script: stage.finished() }
    }

    // The exit after an answer: the sheep nods in the working color, then
    // goes back to the pasture the way it came while the card fades out.
    SequentialAnimation {
        id: exitAnswered
        PauseAnimation { duration: 320 }
        ScriptAction {
            script: {
                const from = stage.onStage.mapToItem(stage, 0, stage.onStage.height)
                const to = stage.mapFromItem(stage.parent, stage.origin.x, stage.origin.y)
                stage.onStage.visible = false
                actor.tint = theme.accent
                actor.fly(from, to, 1, 2 / stage.onStage.px, stage.fence ? 50 : 0, stage.fence ? "walk1" : "walk")
            }
        }
        ParallelAnimation {
            NumberAnimation { target: actor; property: "p"; from: 0; to: 1; duration: stage.fence ? 420 : 360; easing.type: Easing.InOutQuad }
            NumberAnimation { target: card; property: "opacity"; to: 0; duration: 180; easing.type: Easing.InCubic }
        }
        ScriptAction {
            script: {
                actor.visible = false
                stage.opacity = 0
                stage.finished()
            }
        }
    }

    // The sheep on its way from the pasture, drawn over everything.
    StageSheep {
        id: actor
        px: stage.onStage.px
        // Its way, from 0 to 1: a straight line from (x0, y0) to (x1, y1),
        // lifted into an arc for a jump, growing or shrinking on the way.
        // The points are where its feet are.
        property real p: 0
        property real x0: 0
        property real y0: 0
        property real x1: 0
        property real y1: 0
        property real lift: 0
        property real s0: 1
        property real s1: 1
        x: x0 + (x1 - x0) * p
        y: y0 + (y1 - y0) * p - Math.sin(Math.PI * p) * lift - height
        scale: s0 + (s1 - s0) * p
        visible: false
        z: 10
        tint: theme.urgent
        transformOrigin: Item.BottomLeft

        function fly(from, to, startScale, endScale, arc, how) {
            p = 0
            x0 = from.x
            y0 = from.y
            x1 = to.x
            y1 = to.y
            s0 = startScale
            s1 = endScale
            lift = arc
            pose = how
            dir = to.x < from.x ? -1 : 1
            visible = true
        }
    }

    // fence: the sheep stands on the card's top edge, near its right corner.
    StageSheep {
        id: perch
        objectName: stage.fence ? "stageSheep" : ""
        px: 3
        dir: -1
        x: card.width - 18 - width
        y: card.y - height + 2
        z: 9
        visible: false
        transform: Scale { id: squash; origin.x: perch.width / 2; origin.y: perch.height }
    }

    Rectangle {
        id: card
        width: parent.width
        height: column.implicitHeight + 2
        color: theme.background
        radius: theme.radius
        border.width: 2
        border.color: theme.urgent

        // A click anywhere on the card gives it the keyboard.
        TapHandler {
            onTapped: if (!stage.hasKeyboard) stage.takeKeyboard()
        }

        ColumnLayout {
            id: column
            x: 2
            y: 2
            width: parent.width - 4
            spacing: 0

            // The stage: floorboards, the sheep in the spotlight, who asks,
            // and the ones waiting in the wings.
            Item {
                id: boards
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                clip: true

                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 5
                    Repeater {
                        model: Math.ceil(boards.width / 16)
                        Rectangle {
                            width: 16
                            height: 5
                            color: Qt.alpha(theme.foreground, 0.12)
                            Rectangle { anchors.right: parent.right; width: 2; height: parent.height; color: Qt.alpha(theme.foreground, 0.05) }
                        }
                    }
                }

                // The spotlight: a cone of warm light over the sheep's mark.
                Canvas {
                    id: spot
                    x: 2
                    y: 0
                    width: 52
                    height: 49
                    opacity: 0
                    readonly property color light: theme.terminalPalette.length > 3 ? theme.terminalPalette[3] : theme.accent
                    onLightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const g = ctx.createLinearGradient(0, 0, 0, height)
                        g.addColorStop(0, Qt.alpha(light, 0.30))
                        g.addColorStop(1, Qt.alpha(light, 0.07))
                        ctx.fillStyle = g
                        ctx.beginPath()
                        ctx.moveTo(width * 0.4, 0)
                        ctx.lineTo(width * 0.6, 0)
                        ctx.lineTo(width, height - 4)
                        ctx.lineTo(0, height - 4)
                        ctx.closePath()
                        ctx.fill()
                        ctx.fillStyle = Qt.alpha(light, 0.25)
                        ctx.beginPath()
                        ctx.ellipse(0, height - 8, width, 8)
                        ctx.fill()
                    }
                }

                StageSheep {
                    id: star
                    objectName: stage.fence ? "" : "stageSheep"
                    px: 4
                    x: 12
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    visible: false
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: stage.fence ? 12 : 64
                    anchors.right: wings.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -2
                    textFormat: Text.StyledText
                    elide: Text.ElideRight
                    color: theme.foreground
                    text: "<b>" + stage.html(stage.request.title || "") + "</b>"
                          + "<font color='" + Qt.alpha(theme.foreground, 0.55) + "'> · "
                          + stage.html(stage.isQuestion ? (stage.question.header || qsTr("asks you"))
                                                          : qsTr("wants to use %1").arg(stage.request.tool || ""))
                          + "</font>"
                }

                // The next sheep peeks in from the right, under how many wait.
                Item {
                    id: wings
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 44
                    Text {
                        objectName: "stageWaiting"
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.top: parent.top
                        anchors.topMargin: 6
                        visible: asker.waiting > 0
                        text: "+" + asker.waiting
                        color: theme.urgent
                    }
                    StageSheep {
                        x: parent.width - width / 2
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 5
                        px: 2
                        dir: -1
                        visible: asker.waiting > 0
                        opacity: 0.9
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.alpha(theme.foreground, 0.12) }

            // What it asks.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        objectName: "stageQuestion"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        color: theme.foreground
                        text: stage.isQuestion ? (stage.question.question || "")
                                               : qsTr("Allow %1?").arg(stage.request.tool || "")
                    }
                    Text {
                        Layout.alignment: Qt.AlignTop
                        visible: stage.questions.length > 1
                        text: (stage.step + 1) + "/" + stage.questions.length
                        color: Qt.alpha(theme.foreground, 0.55)
                    }
                }

                // A permission's command or file.
                Rectangle {
                    Layout.fillWidth: true
                    visible: !stage.isQuestion && (stage.request.detail || "") !== ""
                    implicitHeight: detailText.implicitHeight + 12
                    radius: theme.radius
                    color: Qt.alpha(theme.foreground, 0.06)
                    Text {
                        id: detailText
                        objectName: "stageDetail"
                        x: 8
                        y: 6
                        width: parent.width - 16
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        color: theme.foreground
                        text: stage.request.detail || ""
                    }
                }

                // The options, and your own answer last.
                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    Repeater {
                        model: stage.options
                        delegate: Rectangle {
                            id: optionRow
                            required property var modelData
                            required property int index
                            objectName: "stageOption" + index
                            readonly property bool lit: stage.highlighted === index
                            readonly property bool ticked: stage.checked[index] === true
                            width: parent.width
                            height: Math.max(30, optionContent.implicitHeight + 10)
                            radius: theme.radius
                            color: lit && stage.hasKeyboard ? Qt.alpha(theme.foreground, 0.08)
                                 : rowHover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                            Behavior on color { ColorAnimation { duration: 100 } }
                            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { stage.takeKeyboard(); stage.choose(optionRow.index) } }

                            RowLayout {
                                id: optionContent
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 8
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    radius: theme.radius
                                    color: optionRow.ticked ? Qt.alpha(theme.accent, 0.25) : "transparent"
                                    border.width: 1
                                    border.color: Qt.alpha(theme.foreground, optionRow.lit && stage.hasKeyboard ? 0.6 : 0.25)
                                    Text {
                                        anchors.centerIn: parent
                                        text: optionRow.index + 1
                                        color: optionRow.ticked ? theme.accent : theme.foreground
                                    }
                                }
                                Text {
                                    text: optionRow.modelData.label
                                    color: optionRow.lit && stage.hasKeyboard ? theme.accent : theme.foreground
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: optionRow.modelData.description || ""
                                    color: Qt.alpha(theme.foreground, 0.55)
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: ownRow
                        objectName: "stageOwnAnswer"
                        readonly property int index: stage.options.length
                        readonly property bool lit: stage.highlighted === index
                        width: parent.width
                        height: 30
                        radius: theme.radius
                        color: (lit && stage.hasKeyboard) || ownAnswer.activeFocus ? Qt.alpha(theme.foreground, 0.08) : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                radius: theme.radius
                                color: "transparent"
                                border.width: 1
                                border.color: Qt.alpha(theme.foreground, ownAnswer.activeFocus ? 0.6 : 0.25)
                                Text { anchors.centerIn: parent; text: ownRow.index + 1; color: theme.foreground }
                            }
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                TextInput {
                                    id: ownAnswer
                                    objectName: "stageOwnField"
                                    anchors.fill: parent
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: theme.foreground
                                    selectionColor: Qt.alpha(theme.foreground, 0.35)
                                    selectedTextColor: theme.foreground
                                    clip: true
                                    onActiveFocusChanged: if (activeFocus) stage.highlighted = ownRow.index
                                    Keys.onShortcutOverride: (event) => {
                                        event.accepted = event.key === Qt.Key_Escape || event.key === Qt.Key_Return
                                                       || event.key === Qt.Key_Enter
                                    }
                                    Keys.onPressed: (event) => {
                                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                            stage.submitOwn()
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Up) {
                                            keys.forceActiveFocus()
                                            if (event.key === Qt.Key_Up)
                                                stage.highlighted = Math.max(0, ownRow.index - 1)
                                            event.accepted = true
                                        }
                                    }
                                }
                                Text {
                                    anchors.fill: parent
                                    verticalAlignment: Text.AlignVCenter
                                    visible: ownAnswer.text === ""
                                    elide: Text.ElideRight
                                    color: Qt.alpha(theme.foreground, 0.4)
                                    text: stage.isQuestion ? qsTr("Your own answer…")
                                                           : qsTr("No, and do this instead…")
                                }
                                TapHandler { onTapped: { stage.takeKeyboard(); ownAnswer.forceActiveFocus() } }
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.alpha(theme.foreground, 0.12) }

            // What the keys do; before the stage has the keyboard, how to give it.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 6
                Layout.preferredHeight: 34
                spacing: 8
                Text {
                    objectName: "stageHint"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Qt.alpha(theme.foreground, 0.55)
                    text: !stage.hasKeyboard ? qsTr("Click to answer, or press drover's key")
                        : stage.isQuestion && stage.question.multiSelect
                          ? qsTr("1–%1 tick · Enter send · Esc terminal").arg(stage.rows)
                          : qsTr("1–%1 pick · Esc terminal").arg(stage.rows)
                }
                IconButton {
                    label: qsTr("Open")
                    hint: qsTr("Open the agent in drover (Ctrl+O)")
                    onClicked: stage.openInPanel()
                }
            }
        }
    }

    function html(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // The keys, while the stage has the keyboard and the field doesn't.
    Item {
        id: keys
        objectName: "stageKeys"
        focus: true
        Keys.onShortcutOverride: (event) => {
            const k = event.key
            event.accepted = (k >= Qt.Key_1 && k <= Qt.Key_9) || k === Qt.Key_Escape || k === Qt.Key_Return
                           || k === Qt.Key_Enter || k === Qt.Key_Up || k === Qt.Key_Down || k === Qt.Key_Space
                           || k === Qt.Key_Left || k === Qt.Key_Backspace
                           || (k === Qt.Key_O && (event.modifiers & Qt.ControlModifier))
        }
        Keys.onPressed: (event) => {
            const k = event.key
            event.accepted = true
            if (k >= Qt.Key_1 && k <= Qt.Key_9)
                stage.choose(k - Qt.Key_1)
            else if (k === Qt.Key_Down)
                stage.highlighted = Math.min(stage.rows - 1, stage.highlighted + 1)
            else if (k === Qt.Key_Up)
                stage.highlighted = Math.max(0, stage.highlighted - 1)
            else if (k === Qt.Key_Space)
                stage.choose(stage.highlighted)
            else if (k === Qt.Key_Return || k === Qt.Key_Enter) {
                if (stage.isQuestion && stage.question.multiSelect && stage.highlighted < stage.options.length)
                    stage.confirmChecked()
                else
                    stage.choose(stage.highlighted)
            } else if (k === Qt.Key_Left || k === Qt.Key_Backspace)
                stage.back()
            else if (k === Qt.Key_Escape)
                stage.toTerminal()
            else if (k === Qt.Key_O && (event.modifiers & Qt.ControlModifier))
                stage.openInPanel()
            else
                event.accepted = false
        }
    }
}
