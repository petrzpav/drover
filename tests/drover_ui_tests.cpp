// Drives the real Main.qml offscreen against a fake herdr, for the behavior
// that lives in QML: where the keyboard goes, what a click can reach, and
// where the panel opens.

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QWheelEvent>
#include <QPainter>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <atomic>
#include <thread>

#include "asker.h"
#include "backend.h"
#include "hook.h"
#include "surface.h"
#include "terminalfont.h"
#include "terminalview.h"
#include "theme.h"
#include "timeline.h"
#include "translator.h"

namespace {

void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray agents(const char *first, const char *second) {
    return QByteArray(R"({"result":{"agents":[
      {"agent":"claude","agent_status":")") + first + R"(","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
      {"agent":"claude","agent_status":")" + second + R"(","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})";
}


// A Claude hook asking drover, on its own thread like the real one.
struct AskingHook {
    std::atomic<bool> done = false;
    QByteArray output;
    std::thread thread;
    Asker *asker = nullptr;
    AskingHook(const QJsonObject &input, const QString &pane, const QString &server, Asker *owner = nullptr)
        : asker(owner) {
        const QByteArray json = QJsonDocument(input).toJson();
        thread = std::thread([this, json, pane, server] {
            output = Hook::run(json, {{"HERDR_PANE_ID", pane}}, server);
            done = true;
        });
    }
    // A test that failed half way lets the hook go rather than hang.
    ~AskingHook() {
        QElapsedTimer timer;
        timer.start();
        while (!done && asker && timer.elapsed() < 3000) {
            const QVariant id = asker->current().value("id");
            if (id.isValid())
                asker->pass(id.toInt());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        if (done)
            thread.join();
        else
            thread.detach();
    }
    QJsonObject result() const { return QJsonDocument::fromJson(output).object().value("hookSpecificOutput").toObject(); }
};

QJsonObject questionInput(bool two) {
    QJsonArray questions{QJsonObject{{"question", "Which colors?"}, {"header", "Colors"}, {"multiSelect", two},
        {"options", QJsonArray{QJsonObject{{"label", "Red"}, {"description", "warm"}}, QJsonObject{{"label", "Green"}},
                                QJsonObject{{"label", "Blue"}}}}}};
    if (two)
        questions.append(QJsonObject{{"question", "Which size?"}, {"header", "Size"}, {"multiSelect", false},
            {"options", QJsonArray{QJsonObject{{"label", "Small"}}, QJsonObject{{"label", "Large"}}}}});
    return QJsonObject{{"hook_event_name", "PreToolUse"}, {"tool_name", "AskUserQuestion"}, {"session_id", "s1"},
                       {"tool_input", QJsonObject{{"questions", questions}}}};
}

QJsonObject permissionInput() {
    return QJsonObject{{"hook_event_name", "PermissionRequest"}, {"tool_name", "Bash"}, {"session_id", "s1"},
                       {"tool_input", QJsonObject{{"command", "rm -rf build"}}}};
}

// One drover with a fake herdr in a temporary directory.
struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Translator> translator;
    std::unique_ptr<Theme> theme;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<Asker> asker;
    std::unique_ptr<Timeline> timeline;
    QString askServer = "drover-ui-test-" + QUuid::createUuid().toString(QUuid::Id128);
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    int quits = 0;

    explicit Rig(const QByteArray &list, int expected = 2) {
        write(dir.filePath("agents.json"), list);
        write(dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1","cwd":"/a"},
          {"pane_id":"w2:p1","tab_id":"w2:t1","terminal_id":"term_2","cwd":"/b"}]}})");
        write(dir.filePath("ws.json"), R"({"result":{"workspaces":[{"workspace_id":"w1","label":"garden"},{"workspace_id":"w2","label":"gate"}]}})");
        const QString herdr = dir.filePath("herdr");
        write(herdr, QString(R"sh(#!/bin/sh
echo "$@" >> '%1/log'
case "$1 $2" in
  "workspace list") cat '%1/ws.json' ;;
  "agent list") cat '%1/agents.json' ;;
  "pane list") cat '%1/panes.json' ;;
  "pane split")
    case "$*" in
      *down*) echo '{"result":{"pane":{"pane_id":"w1:p10","terminal_id":"term_10","tab_id":"w1:t1"}}}' ;;
      *) echo '{"result":{"pane":{"pane_id":"w1:p9","terminal_id":"term_9","tab_id":"w1:t1"}}}' ;;
    esac ;;
  "terminal attach")
    trap 'echo "detached $3" >> "%1/log"; exit 0' HUP TERM INT
    stty raw -echo 2>/dev/null
    # Every byte the agent would get, in hex.
    while byte=$(dd bs=1 count=1 2>/dev/null | od -An -tx1); [ -n "$byte" ]; do
      echo "key$byte" >> "%1/log"
    done
    echo "detached $3" >> "%1/log" ;;
  *) echo '{"result":{"type":"ok"}}' ;;
esac
)sh").arg(dir.path()).toUtf8());
        QFile::setPermissions(herdr, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        translator.reset(new Translator(dir.filePath("language"), dir.path()));
        theme.reset(new Theme(dir.path()));
        backend.reset(new Backend(herdr, dir.filePath("no-hyprctl")));
        surface.reset(new Surface(dir.filePath("position")));
        asker.reset(new Asker);
        timeline.reset(new Timeline);
        timeline->setProjectsDirectory(dir.filePath("projects"));
        timeline->setInterval(30);
        QVERIFY(asker->listen(askServer));
        QObject::connect(backend.get(), &Backend::listed, asker.get(), &Asker::agentsListed);
        QObject::connect(asker.get(), &Asker::askingChanged, backend.get(), [this] { backend->setAsking(asker->askingPanes()); });
        QObject::connect(asker.get(), &Asker::listingWanted, backend.get(), &Backend::refresh);
        engine.reset(new QQmlApplicationEngine);
        QObject::connect(engine.get(), &QQmlEngine::quit, engine.get(), [this] { ++quits; });
        engine->rootContext()->setContextProperty("translator", translator.get());
        engine->rootContext()->setContextProperty("theme", theme.get());
        engine->rootContext()->setContextProperty("backend", backend.get());
        engine->rootContext()->setContextProperty("surface", surface.get());
        engine->rootContext()->setContextProperty("asker", asker.get());
        engine->rootContext()->setContextProperty("timeline", timeline.get());
        engine->load(QUrl("qrc:/Main.qml"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window);
        window->resize(1920, 1174);
        window->show();
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_COMPARE(backend->agents()->count(), expected);
    }

    QString log() {
        QFile file(dir.filePath("log"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    // Items by objectName in the visual tree, where Repeater delegates live.
    QQuickItem *find(const char *name) { return find(window->contentItem(), QString::fromLatin1(name)); }
    static QQuickItem *find(QQuickItem *item, const QString &name) {
        if (item->objectName() == name)
            return item;
        for (auto *child : item->childItems())
            if (auto *found = find(child, name))
                return found;
        return nullptr;
    }
    void expand() {
        QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, true));
        QTest::qWait(50);
    }
    void type(const QString &text) {
        for (const QChar c : text)
            QTest::keyClick(window, c.toLatin1());
    }
};

// Four agents of a made-up garden herd, in the given states, for screenshots.
QByteArray garden(const QStringList &states) {
    static const char *tasks[][2] = {{"Plant the tomato beds", "garden"}, {"Repaint the shed door", "shed"},
                                     {"Fix the fence gate", "fence"}, {"Clean up the pond pump", "pond"}};
    QStringList rows;
    for (int i = 0; i < 4; ++i)
        rows << QString(R"({"agent":"claude","agent_session":{"kind":"id","value":"garden-%3"},)"
                        R"("agent_status":"%1","cwd":"/home/ann/Work/%2","pane_id":"w%3:p1",)"
                        R"("terminal_id":"term_%3","terminal_title_stripped":"%4","workspace_id":"w%3"})")
                    .arg(states[i], tasks[i][1]).arg(i + 1).arg(tasks[i][0]);
    return "{\"result\":{\"agents\":[" + rows.join(",").toUtf8() + "]}}";
}

} // namespace

class DroverUiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<TerminalView>("Drover", 1, 0, "TerminalView");
    }

    void helpCoversWhatIsBehindIt()
    {
        Rig rig(agents("blocked", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_F1);
        auto *help = rig.find("help");
        QTRY_VERIFY(help->isVisible());
        // A click where the terminal is only closes the help.
        const QPointF bottom = help->mapToScene(QPointF(help->width() / 2, help->height() - 40));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, bottom.toPoint());
        QTest::qWait(300);
        QVERIFY(!help->isVisible());
        QCOMPARE(rig.quits, 0);
    }

    void escapeBelongsToTheTerminal()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTRY_VERIFY(rig.find("agentTerminal"));
        QTRY_VERIFY(rig.find("agentTerminal")->hasActiveFocus());
        // Esc goes to the agent, and the panel stays.
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QVERIFY(rig.window->property("expanded").toBool());
        // Help and the picker close first.
        QTest::keyClick(rig.window, Qt::Key_F1);
        QVERIFY(rig.find("help")->isVisible());
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY(!rig.find("help")->isVisible());
        QVERIFY(rig.window->property("expanded").toBool());
        // Outside the terminal, Esc hides the panel.
        rig.find("panel")->forceActiveFocus();
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QVERIFY(!rig.window->property("expanded").toBool());
    }

    void attachesWhileThePanelIsOpen()
    {
        Rig rig(agents("idle", "working"));
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("terminal attach"));
        rig.expand();
        QTRY_VERIFY(rig.log().contains("terminal attach term_1 --takeover\n"));
        // Another agent: the terminal moves to it.
        QTest::keyClick(rig.window, Qt::Key_Down, Qt::AltModifier);
        QTRY_VERIFY(rig.log().contains("terminal attach term_2 --takeover\n"));
        QTRY_VERIFY(rig.log().contains("detached term_1\n"));
        // Closing the panel detaches, so the pane gets herdr's size back.
        QMetaObject::invokeMethod(rig.window, "setExpanded", Q_ARG(QVariant, false));
        QTRY_VERIFY(rig.log().contains("detached term_2\n"));
    }

    void keysReachTheAgent()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTRY_VERIFY(rig.find("agentTerminal") && rig.find("agentTerminal")->hasActiveFocus());
        QTest::qWait(300);
        // Shift+Tab (Claude's mode switch) is ESC [ Z, Ctrl+C is 0x03, Esc is 0x1b.
        QTest::keyClick(rig.window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTest::keyClick(rig.window, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY(rig.log().contains("key 1b\nkey 5b\nkey 5a\nkey 03\nkey 1b\n"));
        QVERIFY(rig.find("agentTerminal")->hasActiveFocus());
        QVERIFY(rig.window->property("expanded").toBool());
    }

    void quitNeedsControl()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        rig.find("panel")->forceActiveFocus();
        QTest::keyClick(rig.window, Qt::Key_Q);
        QCOMPARE(rig.quits, 0);
        QTest::keyClick(rig.window, Qt::Key_Q, Qt::ControlModifier);
        QCOMPARE(rig.quits, 1);
    }

    void pickerStartsTheDefaultAgent()
    {
        Rig rig(agents("idle", "idle"));
        rig.backend->setAgentKind("claude");
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        auto *field = rig.find("pickerField");
        QVERIFY(field && field->hasActiveFocus());
        rig.type("ga");
        // Typed text starts on a workspace it names, past the agents that
        // work there; "garden" and "gate" both start with "ga", and Down
        // picks the second.
        QTest::keyClick(rig.window, Qt::Key_Down);
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_VERIFY(rig.log().contains("tab create --workspace w2"));
    }

    void pickerGoesToARunningAgent()
    {
        Rig rig(agents("working", "blocked"));
        rig.expand();
        QTRY_COMPARE(rig.backend->selected(), QString("w1:p1"));
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        auto *panel = rig.find("panel");
        auto *picker = panel->property("picker").value<QObject *>();
        QTRY_VERIFY(picker->property("open").toBool());
        // The agents come first, the highlight on the one not in front:
        // Ctrl+P, Enter switches, like Alt+Tab.
        QTRY_COMPARE(picker->property("current").toMap().value("pane").toString(), QString("w2:p1"));
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_COMPARE(rig.backend->selected(), QString("w2:p1"));
        QVERIFY(!picker->property("open").toBool());
        QTRY_VERIFY(panel->property("terminalFocused").toBool());
        QVERIFY(!rig.log().contains("tab create"));

        // Again: back to the agent before, like Alt+Tab.
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(picker->property("open").toBool());
        QCOMPARE(picker->property("current").toMap().value("pane").toString(), QString("w1:p1"));
        // Typing narrows them by title or workspace, unless it names a
        // workspace: then that one, to start an agent there.
        rig.type("gat");
        QTRY_COMPARE(picker->property("current").toMap().value("label").toString(), QString("gate"));
        rig.find("pickerField")->setProperty("text", "");
        rig.type("plant");
        QTRY_COMPARE(picker->property("current").toMap().value("pane").toString(), QString("w1:p1"));
        // Ctrl+D leaves an agent alone.
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QTest::qWait(50);
        QVERIFY(picker->property("armed").toString().isEmpty());
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_COMPARE(rig.backend->selected(), QString("w1:p1"));
        QVERIFY(!rig.log().contains("tab create"));
        QVERIFY(!rig.log().contains("workspace close"));

        // An agent's state changing keeps the rows and the highlight where
        // they are, the Create row too.
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(picker->property("open").toBool());
        rig.type("ga");
        QTest::keyClick(rig.window, Qt::Key_Down);
        QTest::keyClick(rig.window, Qt::Key_Down);
        QTRY_VERIFY(picker->property("current").toMap().value("create").toBool());
        auto *row = rig.find("pickerFolder");
        QVERIFY(row);
        write(rig.dir.filePath("agents.json"), agents("idle", "done"));
        QTRY_COMPARE(rig.backend->selectedStatus(), QString("idle"));
        QTest::qWait(100);
        QVERIFY(picker->property("current").toMap().value("create").toBool());
        QCOMPARE(rig.find("pickerFolder"), row);
    }

    void pickerCreatesAWorkspaceInAPickedFolder()
    {
        Rig rig(agents("idle", "idle"));
        QString asked;
        rig.backend->setFolderChooser([&](const QString &start, const QString &, auto done) {
            asked = start;
            // The panel stepped aside for the picker.
            QVERIFY(!rig.window->property("expanded").toBool());
            done(rig.dir.path(), {});
        });
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        rig.type("orchardzzq");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_VERIFY(rig.log().contains("workspace create --label orchardzzq --cwd " + rig.dir.path() + " --no-focus"));
        QCOMPARE(asked, QDir::homePath());
        QVERIFY(rig.window->property("expanded").toBool());
        QVERIFY(!rig.log().contains("tab create"));
    }

    void cancellingTheFolderPickerBringsThePickerBack()
    {
        Rig rig(agents("idle", "idle"));
        int asked = 0;
        // The portal answers later, after the panel has stepped aside.
        rig.backend->setFolderChooser([&](const QString &, const QString &, auto done) {
            ++asked;
            QTimer::singleShot(200, [done] { done({}, {}); });
        });
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        rig.type("orchardzzq");
        QTest::keyClick(rig.window, Qt::Key_O, Qt::ControlModifier);
        QTRY_COMPARE(asked, 1);
        auto *field = rig.find("pickerField");
        QTRY_VERIFY(field->isVisible());
        QTRY_VERIFY(field->hasActiveFocus());
        QCOMPARE(field->property("text").toString(), QString("orchardzzq"));
        QVERIFY(!rig.log().contains("workspace create"));
        // Ctrl+O does nothing for a name a workspace already has.
        field->setProperty("text", "gate");
        QTest::keyClick(rig.window, Qt::Key_O, Qt::ControlModifier);
        QTest::qWait(50);
        QCOMPARE(asked, 1);
    }

    void pickerClosesAWorkspaceOnlyOnTheSecondPress()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        rig.type("ga");
        QTest::keyClick(rig.window, Qt::Key_Down);
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(!rig.log().contains("workspace close"));
        // A workspace that sorts first shows up between the presses; the
        // second press still closes the one that was marked.
        write(rig.dir.filePath("ws.json"), R"({"result":{"workspaces":[{"workspace_id":"w1","label":"garden"},
            {"workspace_id":"w2","label":"gate"},{"workspace_id":"w3","label":"gable"}]}})");
        QTRY_COMPARE(rig.backend->workspaces().size(), 3);
        QTest::qWait(50);
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QTRY_VERIFY(rig.log().contains("workspace close w2\n"));
        QVERIFY(!rig.log().contains("workspace close w1") && !rig.log().contains("workspace close w3"));

        // Esc takes a mark back and keeps the picker open.
        QFile::remove(rig.dir.filePath("log"));
        rig.type("te");
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QVERIFY(rig.find("pickerField")->isVisible());
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QTest::qWait(100);
        QVERIFY(!rig.log().contains("workspace close"));
        QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
        QTRY_VERIFY(rig.log().contains("workspace close w2\n"));
        QVERIFY(!rig.log().contains("tab create"));
    }

    void clickingTheTrashButtonStartsNothing()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        rig.type("gate");
        QQuickItem *trash = nullptr;
        QTRY_VERIFY((trash = rig.find("pickerClose_gate")) && trash->isVisible());
        const QPoint center = trash->mapToScene(QPointF(trash->width() / 2, trash->height() / 2)).toPoint();
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center);
        QTest::qWait(100);
        QVERIFY(!rig.log().contains("workspace close"));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center);
        QTRY_VERIFY(rig.log().contains("workspace close w2\n"));
        QVERIFY(!rig.log().contains("tab create"));
    }

    void theHerdHasABarPerAgentAndMarksWhoFinished()
    {
        Rig rig(agents("working", "blocked"));
        auto *label = rig.find("herdLabel");
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), QString("1 asks you"));
        int bars = 0;
        std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
            bars += item->objectName() == "herdBar";
            for (auto *child : item->childItems())
                count(child);
        };
        count(rig.window->contentItem());
        QCOMPARE(bars, 2);

        // The first agent finishes: its bar shoots up, then settles low.
        write(rig.dir.filePath("agents.json"), agents("done", "idle"));
        QTRY_COMPARE(label->property("text").toString(), QString("1 done"));
        auto *bar = rig.find("herdBar");
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("level").toReal() > 1.0, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("level").toReal(), 0.45, 3000);
    }

    void sheepJumpIntoThePenWhenTheyFinish()
    {
        Rig rig(agents("working", "idle"));
        rig.backend->setIndicator("sheep");
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = rig.find("herdSheep")));
        QVERIFY(!rig.find("herdBars"));
        QCOMPARE(field->property("penned").toInt(), 0);
        write(rig.dir.filePath("agents.json"), agents("done", "idle"));
        QTRY_COMPARE_WITH_TIMEOUT(field->property("penned").toInt(), 1, 6000);
        // Back to work, it jumps out again.
        write(rig.dir.filePath("agents.json"), agents("working", "idle"));
        QTRY_COMPARE_WITH_TIMEOUT(field->property("penned").toInt(), 0, 6000);
    }

    void dotsCountTheStates()
    {
        Rig rig(agents("working", "working"));
        rig.backend->setIndicator("dots");
        QTRY_VERIFY(rig.find("herdDots"));
        QVERIFY(!rig.find("herdBar"));
    }

    // Not a test: with DROVER_SCREENSHOTS=<dir> (bin/screenshots), draws
    // the README's pictures there, a still of the panel and frames of each
    // indicator style watching the herd change.
    void screenshots()
    {
        const QString out = qEnvironmentVariable("DROVER_SCREENSHOTS");
        if (out.isEmpty())
            QSKIP("set DROVER_SCREENSHOTS to a directory to draw the README's pictures");
        QDir().mkpath(out);
        // As drover itself: everything in the terminal's font and size.
        const QFont font = QGuiApplication::font();
        QGuiApplication::setFont(TerminalFont::read(QDir::homePath()));
        const auto restore = qScopeGuard([&] { QGuiApplication::setFont(font); });

        Rig rig(garden({"working", "blocked", "done", "idle"}), 4);
        // The selected agent's terminal shows a short exchange.
        write(rig.dir.filePath("transcript"), QByteArray(
            "\r\n\033[97m\xe2\x97\x8f\033[0m Reading \033[1mbeds.md\033[0m and the planting calendar.\r\n\r\n"
            "\033[97m\xe2\x97\x8f\033[0m The south bed gets the most sun, so the tomatoes go there;\r\n"
            "  basil between the rows keeps pests off.\r\n\r\n"
            "\033[92m\xe2\x97\x8f\033[0m \033[1mUpdate\033[0m(beds.md)\r\n"
            "  \xe2\x8e\xbf  Added \033[92m6\033[0m lines\r\n\r\n"
            "\033[2m\xe2\x80\xbb recap: \033[3mplanting plan ready for review\033[0m\r\n"
            "\033[90m\xe2\x9d\xaf\033[0m \r\n"
            "  \033[33m\xe2\x8f\xb5\xe2\x8f\xb5 auto mode on\033[0m\033[1A\033[3G"));
        QFile script(rig.dir.filePath("herdr"));
        QVERIFY(script.open(QIODevice::ReadOnly));
        QString text = QString::fromUtf8(script.readAll());
        script.close();
        text.replace("  \"terminal attach\")\n", QString("  \"terminal attach\")\n    cat '%1/transcript'; exec sleep 60 ;;\n  \"terminal attach-old\")\n").arg(rig.dir.path()));
        write(rig.dir.filePath("herdr"), text.toUtf8());

        // Its conversation so far, for the overview beside the terminal.
        {
            QDir(rig.dir.path()).mkpath("projects/-home-ann-Work-garden");
            QByteArray transcript;
            const auto add = [&](QJsonObject object) {
                object.insert("timestamp", "2026-11-26T08:" + QString::number(10 + transcript.count('\n')) + ":00.000Z");
                transcript += QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
            };
            const auto prompt = [&](const QString &text) {
                add({{"type", "user"}, {"uuid", "u" + QString::number(transcript.count('\n'))}, {"origin", QJsonObject{{"kind", "human"}}},
                     {"message", QJsonObject{{"content", text}}}});
            };
            const auto says = [&](const QString &text) {
                add({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}}}});
            };
            const auto runs = [&](const QString &id, const QString &name, const QJsonObject &input, const QString &out = "ok",
                                  bool error = false) {
                add({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
                    QJsonObject{{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", input}}}}}}});
                if (!out.isEmpty())
                    add({{"type", "user"}, {"message", QJsonObject{{"content", QJsonArray{QJsonObject{
                        {"type", "tool_result"}, {"tool_use_id", id}, {"content", out}, {"is_error", error}}}}}}});
            };
            prompt("Plan the tomato beds for this spring, the south side gets the most sun");
            runs("a", "Bash", {{"command", "ls"}, {"description", "List the beds and their soil tests"}});
            runs("b", "Bash", {{"command", "cat"}, {"description", "Read last year's harvest notes"}});
            runs("c", "Bash", {{"command", "make"}, {"description", "Check the planting calendar"}},
                 "Exit code 1\ncalendar.ics: no events after March", true);
            says("Last year's calendar ends in March, so I planned from the soil tests alone.");
            runs("d", "AskUserQuestion", {{"questions", QJsonArray{QJsonObject{{"question", "Basil between the tomato rows?"}}}}}, "");
            add({{"type", "user"}, {"toolUseResult", QJsonObject{{"answers", QJsonObject{{"Basil between the tomato rows?", "Yes, it keeps pests off"}}}}},
                 {"message", QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "tool_result"}, {"tool_use_id", "d"}, {"content", "answered"}}}}}}});
            runs("e", "Edit", {{"file_path", "/home/ann/Work/garden/beds.md"}});
            runs("f", "Write", {{"file_path", "/home/ann/Work/garden/calendar.ics"}});
            runs("g", "Bash", {{"command", "git commit -m \"Plan the tomato beds\" && git push"}});
            says("The plan is in beds.md: tomatoes on the south bed, basil between the rows, sowing from April 12.");
            prompt("Order the seeds we're missing");
            says("Looking at what's left in the seed box first.");
            runs("h", "Bash", {{"command", "grep"}, {"description", "Count the seeds left in the box"}}, "");
            write(rig.dir.filePath("projects/-home-ann-Work-garden/garden-1.jsonl"), transcript);
        }

        // The window is see-through; the pictures put it on a black desktop.
        const auto grab = [&](const QRect &rect) {
            QImage image(rect.size(), QImage::Format_RGB32);
            image.fill(Qt::black);
            QPainter painter(&image);
            painter.drawImage(0, 0, rig.window->grabWindow().copy(rect));
            return image;
        };
        const auto pillRect = [&] {
            return QRect(rig.window->property("pillX").toInt(), rig.window->property("pillY").toInt(), 176, 36);
        };

        // The panel, open on the first agent, with the bars beside it.
        rig.expand();
        QTest::qWait(1500);
        const QRect panel = rig.window->property("panelRect").toRectF().toAlignedRect();
        grab(panel.united(pillRect()).adjusted(-24, -24, 24, 24)).save(out + "/screenshot.png");
        QMetaObject::invokeMethod(rig.window, "setExpanded", Q_ARG(QVariant, false));

        // The same agent's conversation in points, its first turn summed up.
        {
            const QJsonArray points{
                QJsonObject{{"text", "The plan is in beds.md: tomatoes on the south bed, basil between the rows"},
                            {"kind", "done"}, {"step", 9}},
                QJsonObject{{"text", "Last year's planting calendar ends in March, so the plan rests on the soil tests"},
                            {"kind", "fail"}, {"step", 3}},
                QJsonObject{{"text", "Committed and pushed as \"Plan the tomato beds\""}, {"kind", "ship"}, {"step", 8},
                            {"visual", "south bed   T · B · T · B · T · B · T      T tomatoes\n"
                                       "            ─────────────────────────      B basil\n"
                                       "calendar    [fail]January – March only[/] → plan from soil tests\n"
                                       "sowing      [ok]April 12[/]   soil tests [ok]✓[/]   beds.md [ok]✓[/]"}}};
            write(rig.dir.filePath("reply.json"), QJsonDocument(QJsonObject{{"type", "result"}, {"is_error", false},
                {"result", QString::fromUtf8(QJsonDocument(QJsonObject{{"points", points}}).toJson(QJsonDocument::Compact))}})
                .toJson(QJsonDocument::Compact));
            qputenv("DROVER_TEST_DIR", rig.dir.path().toUtf8());
            rig.timeline->setCacheDirectory(rig.dir.filePath("cache"));
            rig.timeline->setSummaryProgram(TESTS_DIR "/fake_claude.sh");
            rig.timeline->setSummaryModel("sonnet");
            rig.surface->setOverviewShown(true);
            rig.expand();
            QTRY_VERIFY_WITH_TIMEOUT(rig.timeline->index(0).data(Timeline::PointsRole).toList().size() == 3, 10000);
            QTest::qWait(800);
            grab(panel.united(pillRect()).adjusted(-24, -24, 24, 24)).save(out + "/overview.png");
            QMetaObject::invokeMethod(rig.window, "setExpanded", Q_ARG(QVariant, false));
            rig.surface->setOverviewShown(false);
            rig.timeline->setSummaryModel("");
        }

        // An agent's question on the stage, with the sheep in the pill.
        {
            rig.backend->setIndicator("sheep");
            const QJsonObject question{{"hook_event_name", "PreToolUse"}, {"tool_name", "AskUserQuestion"},
                {"tool_input", QJsonObject{{"questions", QJsonArray{QJsonObject{
                    {"question", "Which database should the planting log use?"}, {"header", "Database"},
                    {"multiSelect", false},
                    {"options", QJsonArray{
                        QJsonObject{{"label", "PostgreSQL (Recommended)"}, {"description", "what the other apps run on"}},
                        QJsonObject{{"label", "SQLite"}, {"description", "one file, no server"}},
                        QJsonObject{{"label", "MySQL"}, {"description", "the old server has it"}}}}}}}}}};
            AskingHook hook(question, "w2:p1", rig.askServer, rig.asker.get());
            auto *stage = rig.find("askStage");
            QTRY_VERIFY(stage->isVisible());
            QMetaObject::invokeMethod(rig.window, "toggle");
            QTest::qWait(1200);
            const QRect area = QRectF(stage->x(), stage->y(), stage->width(), stage->height()).toAlignedRect();
            grab(area.united(pillRect()).adjusted(-24, -64, 24, 24)).save(out + "/stage.png");
        }
        QTest::qWait(400);

        // Each style, while one agent finishes, one gets to work and one asks.
        const QList<QStringList> story = {
            {"working", "working", "done", "idle"}, {"done", "working", "done", "idle"},
            {"done", "working", "done", "working"}, {"done", "blocked", "done", "working"},
        };
        for (const char *style : {"dots", "bars", "sheep"}) {
            write(rig.dir.filePath("agents.json"), garden(story[0]));
            rig.backend->setIndicator(style);
            QTest::qWait(2500);
            const QString frames = out + "/" + style;
            QDir().mkpath(frames);
            int frame = 0;
            QElapsedTimer clock;
            clock.start();
            for (int step = 1; step <= story.size(); ++step) {
                if (step < story.size())
                    write(rig.dir.filePath("agents.json"), garden(story[step]));
                while (clock.elapsed() < step * 2200) {
                    grab(pillRect().adjusted(-10, -10, 10, 10)).save(QString("%1/%2.png").arg(frames).arg(frame++, 4, 10, QChar('0')));
                    QTest::qWait(40);
                }
            }
        }
    }

    void altEOpensAnEditorBesideTheAgent()
    {
        Rig rig(agents("working", "idle"));
        // A stand-in fresh on PATH: Ctrl+Shift letters go as such only to Fresh.
        write(rig.dir.filePath("fresh"), "#!/bin/sh\n");
        QFile::setPermissions(rig.dir.filePath("fresh"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", rig.dir.path().toUtf8() + ":" + path);
        const auto restore = qScopeGuard([&] { qputenv("PATH", path); });
        rig.backend->setEditor("fresh");
        rig.expand();
        QTRY_VERIFY(rig.log().contains("terminal attach term_1"));
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(rig.backend->editing());
        QCOMPARE(rig.backend->editorTerminal(), QString("term_9"));
        QVERIFY(rig.log().contains("pane split w1:p1 --direction right --cwd /a --no-focus"));
        QVERIFY(rig.log().contains("pane rename w1:p9 editor"));
        QVERIFY(rig.log().contains("pane run w1:p9 exec fresh"));
        QTRY_VERIFY(rig.log().contains("terminal attach term_9"));
        // Ctrl+B is the editor's (its file explorer), not drover's list; herdr's
        // attach takes a lone Ctrl+B as its prefix, so it goes doubled.
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier);
        QTRY_VERIFY(rig.log().contains("key 02\nkey 02\n"));
        QVERIFY(rig.find("panel")->property("sidebarShown").toBool());

        // The desktop's Ctrl+P goes to the editor, not to the picker.
        QMetaObject::invokeMethod(rig.window, "openPicker");
        QTRY_VERIFY(rig.log().contains("key 10"));
        QVERIFY(!rig.find("panel")->property("picker").value<QObject *>()->property("open").toBool());
        // Ctrl+Shift+letters reach it as such.
        QTest::keyClick(rig.window, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(rig.log().contains("key 1b\nkey 5b\nkey 31\nkey 30\nkey 37\nkey 3b\nkey 36\nkey 75"));

        // Alt+E again: back to the agent, whose terminal stayed attached, as
        // does the editor's, so switching back and forth is instant.
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(!rig.backend->editing());
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(rig.backend->editing());
        QTest::qWait(300);
        QCOMPARE(rig.log().count("terminal attach term_1"), 1);
        QCOMPARE(rig.log().count("terminal attach term_9"), 1);
        QCOMPARE(rig.log().count("pane split"), 1);
    }

    void draggingARowMovesItsAgent()
    {
        Rig rig(garden({"working", "blocked", "done", "idle"}), 4);
        rig.backend->setOrderFile(rig.dir.filePath("order"));
        rig.expand();
        QTRY_COMPARE(rig.backend->selected(), QString("w1:p1"));
        auto order = [&] {
            QStringList ids;
            for (const auto &agent : rig.backend->agents()->agents())
                ids << agent.paneId;
            return ids;
        };
        auto *row = rig.find("agentRow_w2:p1");
        QVERIFY(row);
        const qreal step = row->height() + 2;
        const QPoint at = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mousePress(rig.window, Qt::LeftButton, {}, at);
        // Down past two rows, in small steps like a hand. The row starts from
        // where it is, not with the drag threshold's jump.
        QTest::mouseMove(rig.window, at + QPoint(0, 14));
        QTRY_VERIFY(row->property("lifted").toBool());
        QVERIFY(row->property("offset").toReal() < 6);
        for (int i = 1; i <= 20; ++i)
            QTest::mouseMove(rig.window, at + QPoint(0, 14 + int(i * step * 2 / 20)));
        // The rows it passed made room: the third one is where it was.
        QTRY_COMPARE(rig.find("agentRow_w3:p1")->property("offset").toReal(), -step);
        // Past the end it stops at the last place, inside the list.
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(rig.window, at + QPoint(0, 14 + int(step * 2) + i * 40));
        QTRY_COMPARE(row->property("offset").toReal(), 2 * step);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at + QPoint(0, 14 + int(step * 2) + 400));
        QTRY_COMPARE(order(), QStringList({"w1:p1", "w3:p1", "w4:p1", "w2:p1"}));
        // A drag is no click: the agent in front stays; the row glides home.
        QCOMPARE(rig.backend->selected(), QString("w1:p1"));
        QTRY_COMPARE(rig.find("agentRow_w2:p1")->property("offset").toReal(), 0.0);
        QTRY_VERIFY(!rig.find("agentRow_w2:p1")->property("lifted").toBool());
        for (const char *pane : {"agentRow_w1:p1", "agentRow_w3:p1", "agentRow_w4:p1"})
            QCOMPARE(rig.find(pane)->property("offset").toReal(), 0.0);
        QFile file(rig.dir.filePath("order"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("w1:p1\nw3:p1\nw4:p1\nw2:p1"));
    }

    void draggingARowLeavesTheListWhereItIs()
    {
        // More agents than the list shows, so it could scroll.
        QStringList rows;
        for (int i = 1; i <= 20; ++i)
            rows << QString(R"({"agent":"claude","agent_status":"idle","cwd":"/a","pane_id":"w%1:p1",)"
                            R"("terminal_id":"term_%1","terminal_title_stripped":"Agent %1","workspace_id":"w%1"})").arg(i);
        Rig rig("{\"result\":{\"agents\":[" + rows.join(",").toUtf8() + "]}}", 20);
        rig.expand();
        QTest::qWait(300);
        auto *row = rig.find("agentRow_w3:p1");
        QVERIFY(row);
        auto *view = row->parentItem()->parentItem();
        QVERIFY(view->inherits("QQuickListView"));
        QCOMPARE(view->property("contentY").toReal(), 0.0);
        const QPointF before = row->mapToScene(QPointF(0, 0));
        const QPoint at = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mousePress(rig.window, Qt::LeftButton, {}, at);
        // The first pixels of a drag move the row only, never the list.
        for (int i = 1; i <= 30; ++i) {
            QTest::mouseMove(rig.window, at + QPoint(0, i));
            QCOMPARE(view->property("contentY").toReal(), 0.0);
            const qreal y = row->mapToScene(QPointF(0, 0)).y() - before.y();
            QVERIFY2(y >= -0.5 && y <= i + 0.5, qPrintable(QString("row at %1 after %2 px").arg(y).arg(i)));
        }
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at + QPoint(0, 30));
        QTest::qWait(300);
        QCOMPARE(view->property("contentY").toReal(), 0.0);
        // The wheel still scrolls it.
        QWheelEvent wheel(at, rig.window->mapToGlobal(at), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(rig.window, &wheel);
        QTRY_VERIFY(view->property("contentY").toReal() > 0);
    }

    void anAgentEndingMidDragLeavesNothingAskew()
    {
        Rig rig(garden({"working", "blocked", "done", "idle"}), 4);
        rig.expand();
        QTRY_COMPARE(rig.backend->selected(), QString("w1:p1"));
        auto *row = rig.find("agentRow_w2:p1");
        const qreal step = row->height() + 2;
        const QPoint at = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mousePress(rig.window, Qt::LeftButton, {}, at);
        for (int i = 1; i <= 20; ++i)
            QTest::mouseMove(rig.window, at + QPoint(0, int(i * step * 1.2 / 20)));
        QTRY_VERIFY(row->property("lifted").toBool());
        // The dragged agent's pane closes mid-drag.
        auto list = QJsonDocument::fromJson(garden({"working", "blocked", "done", "idle"})).object();
        auto result = list["result"].toObject();
        auto agents = result["agents"].toArray();
        agents.removeAt(1);
        result["agents"] = agents;
        list["result"] = result;
        write(rig.dir.filePath("agents.json"), QJsonDocument(list).toJson());
        QTRY_COMPARE(rig.backend->agents()->count(), 3);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at + QPoint(0, int(step * 1.2)));
        QTest::qWait(300);
        for (const char *pane : {"agentRow_w1:p1", "agentRow_w3:p1", "agentRow_w4:p1"}) {
            QCOMPARE(rig.find(pane)->property("offset").toReal(), 0.0);
            QVERIFY(!rig.find(pane)->property("lifted").toBool());
        }
        QStringList ids;
        for (const auto &agent : rig.backend->agents()->agents())
            ids << agent.paneId;
        QCOMPARE(ids, QStringList({"w1:p1", "w3:p1", "w4:p1"}));
    }

    void ctrlBHidesTheAgentList()
    {
        Rig rig(agents("working", "idle"));
        rig.expand();
        auto *panel = rig.find("panel");
        QTRY_VERIFY(panel->property("agentTerminal").value<QQuickItem *>());
        auto *terminal = panel->property("agentTerminal").value<QQuickItem *>();
        QTRY_VERIFY(panel->property("terminalFocused").toBool());
        const qreal narrow = terminal->width();
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier);
        QTRY_VERIFY(!panel->property("listShown").toBool());
        // The terminal takes the list's room, keeps the keyboard, and the key
        // stays drover's: the next key is the first the agent gets.
        QTRY_VERIFY(terminal->width() > narrow + 200);
        QVERIFY(panel->property("terminalFocused").toBool());
        QTest::keyClick(rig.window, Qt::Key_X);
        QTRY_VERIFY(rig.log().contains("key 78"));
        QVERIFY(!rig.log().contains("key 02"));
        // The help covers the whole conversation side now.
        QTest::keyClick(rig.window, Qt::Key_F1);
        auto *help = rig.find("help");
        QTRY_VERIFY(help->isVisible());
        QCOMPARE(help->x(), 2.0);
        QTest::keyClick(rig.window, Qt::Key_F1);
        QTRY_VERIFY(!help->isVisible());
        // The list's button turns red while an agent asks.
        auto *button = rig.find("listButton");
        QVERIFY(button);
        const QColor quiet = button->property("tint").value<QColor>();
        rig.backend->setAsking({"w2:p1"});
        QTRY_VERIFY(button->property("tint").value<QColor>() != quiet);
        // Its button shows the list again, from the very same place.
        const QPointF place = button->mapToScene(QPointF(0, 0));
        QTest::mouseClick(rig.window, Qt::LeftButton, {},
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(panel->property("listShown").toBool());
        QTRY_COMPARE(terminal->width(), narrow);
        QCOMPARE(button->mapToScene(QPointF(0, 0)), place);
        // Over the picker, Ctrl+B leaves the list alone.
        QTest::keyClick(rig.window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(panel->property("picker").value<QObject *>()->property("open").toBool());
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier);
        QTest::qWait(50);
        QVERIFY(panel->property("listShown").toBool());
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("picker").value<QObject *>()->property("open").toBool());

        // The agent in front ends while the list is hidden: the list comes
        // back and stays, and its button, with nothing to do, goes.
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier);
        QTRY_VERIFY(!panel->property("listShown").toBool());
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[]}})");
        QTRY_VERIFY(rig.backend->selected().isEmpty());
        QTRY_VERIFY(panel->property("listShown").toBool());
        QVERIFY(panel->property("sidebarShown").toBool());
        QVERIFY(!button->isVisible());
    }

    void ctrlAltBShowsTheConversationInPoints()
    {
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_session":{"kind":"id","value":"s-1"},"agent_status":"working","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
          {"agent":"codex","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})");
        QDir(rig.dir.path()).mkpath("projects/-a");
        const auto entry = [](const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'; };
        write(rig.dir.filePath("projects/-a/s-1.jsonl"),
              entry({{"type", "user"}, {"timestamp", "2026-11-26T09:00:00.000Z"}, {"origin", QJsonObject{{"kind", "human"}}},
                     {"message", QJsonObject{{"content", "Plant the tomato beds"}}}})
              + entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
                  QJsonObject{{"type", "text"}, {"text", "The south bed gets the most sun."}},
                  QJsonObject{{"type", "tool_use"}, {"id", "b1"}, {"name", "Bash"},
                              {"input", QJsonObject{{"command", "make"}, {"description", "Water the beds"}}}}}}}}}));

        rig.expand();
        auto *panel = rig.find("panel");
        QTRY_VERIFY(panel->property("agentTerminal").value<QQuickItem *>());
        QTRY_VERIFY(panel->property("terminalFocused").toBool());
        auto *overview = rig.find("overview");
        QVERIFY(overview && !overview->isVisible());
        QCOMPARE(panel->property("view").toString(), QString("agent"));
        QVERIFY(!rig.timeline->active());

        // Ctrl+Alt+B: the conversation in points over the whole side, and
        // the keyboard in its message field; the key never reaches the agent.
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(overview->isVisible());
        QCOMPARE(panel->property("view").toString(), QString("overview"));
        QCOMPARE(overview->width(), rig.find("terminalArea")->width());
        auto *input = rig.find("overviewInput");
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_COMPARE(rig.timeline->state(), QString("ready"));
        QTRY_VERIFY(rig.find("pointText2"));
        QVERIFY(rig.find("pointText2")->property("text").toString().startsWith("Water the beds"));
        QVERIFY(!rig.log().contains("key 02"));

        // What the agent does next comes in while you look.
        QFile file(rig.dir.filePath("projects/-a/s-1.jsonl"));
        QVERIFY(file.open(QIODevice::Append));
        file.write(entry({{"type", "user"}, {"message", QJsonObject{{"content", QJsonArray{
            QJsonObject{{"type", "tool_result"}, {"tool_use_id", "b1"}, {"content", "ok"}}}}}}}));
        file.write(entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
            QJsonObject{{"type", "text"}, {"text", "Both beds are **watered**.\n\n- south\n- " + QString("and more ").repeated(60)}}}}}}}));
        file.close();
        QTRY_VERIFY(rig.find("pointText3"));
        // Two lines at most, plain; a click opens it where it is, as Markdown.
        auto *said = rig.find("pointText3");
        QTRY_COMPARE(said->property("lineCount").toInt(), 2);
        QVERIFY(said->property("truncated").toBool());
        auto *row = rig.find("point3");
        const qreal closed = said->height();
        QTest::qWait(50);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, row->mapToScene(QPointF(row->width() / 2, 10)).toPoint());
        QTRY_VERIFY(rig.find("pointWords3"));
        auto *words = rig.find("pointWords3");
        QVERIFY(!said->isVisible());
        QTRY_VERIFY(words->height() > closed * 2);
        QCOMPARE(words->property("textFormat").toInt(), int(Qt::MarkdownText));
        QTRY_VERIFY(input->hasActiveFocus());

        // The agent's words fetch nothing: pictures become links.
        QVariant safe;
        QMetaObject::invokeMethod(overview, "markdown", Q_RETURN_ARG(QVariant, safe),
                                  Q_ARG(QVariant, "a ![map](http://x/m.png) <img src=\"http://x/i.png\"> b"));
        const QString zw = QString(QChar(0x200b));
        QCOMPARE(safe.toString(), "a !" + zw + "[map](http://x/m.png) <" + zw + "img src=\"http://x/i.png\"> b");
        QMetaObject::invokeMethod(overview, "markdown", Q_RETURN_ARG(QVariant, safe),
                                  Q_ARG(QVariant, "!![a](x) \\`![b](y)\\` ``` ![c][r]"));
        QCOMPARE(safe.toString(), "!!" + zw + "[a](x) \\`!" + zw + "[b](y)\\` ``` !" + zw + "[c][r]");

        // The opened point stays in place, rather than the list following its end.
        QVERIFY(!rig.find("overviewList")->property("follow").toBool());

        // A click on nothing in particular keeps the keyboard off the agent.
        auto *field = input->parentItem()->parentItem()->parentItem();
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, field->mapToScene(QPointF(-12, field->height() / 2)).toPoint());
        QTest::mouseClick(rig.window, Qt::RightButton, {}, row->mapToScene(QPointF(row->width() / 2, 10)).toPoint());
        QTRY_VERIFY(input->hasActiveFocus());
        QVERIFY(!panel->property("terminalFocused").toBool());

        // A message to the agent: Enter sends it, and the field empties.
        rig.type("hello");
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ShiftModifier);
        rig.type("there");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_VERIFY(rig.log().contains("agent prompt w1:p1 hello\nthere"));
        QTRY_COMPARE(input->property("text").toString(), QString());
        QVERIFY(!rig.log().contains("key 68"));

        // Alt+E goes to the editor and back to the points.
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_COMPARE(panel->property("view").toString(), QString("editor"));
        QTRY_VERIFY(!overview->isVisible());
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_COMPARE(panel->property("view").toString(), QString("overview"));
        QTRY_VERIFY(overview->isVisible());

        // Kept for the next start.
        Surface again(rig.dir.filePath("position"));
        QVERIFY(again.overviewShown());

        // Another kind of agent: no points, but a message still goes; each
        // agent keeps its own draft.
        rig.type("for one");
        QTest::keyClick(rig.window, Qt::Key_Down, Qt::AltModifier);
        QTRY_COMPARE(rig.backend->selected(), QString("w2:p1"));
        QTRY_COMPARE(rig.timeline->session(), QString());
        QVERIFY(overview->isVisible());
        QCOMPARE(input->property("text").toString(), QString());
        QTest::keyClick(rig.window, Qt::Key_Up, Qt::AltModifier);
        QTRY_COMPARE(rig.backend->selected(), QString("w1:p1"));
        QCOMPARE(input->property("text").toString(), QString("for one"));
        QTest::keyClick(rig.window, Qt::Key_Down, Qt::AltModifier);
        QTRY_COMPARE(rig.backend->selected(), QString("w2:p1"));

        // An agent that asks: the field says where to answer, and takes you there.
        rig.backend->setAsking({"w2:p1"});
        QTRY_VERIFY(!input->isEnabled());
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, field->mapToScene(QPointF(field->width() / 2, field->height() / 2)).toPoint());
        QTRY_COMPARE(panel->property("view").toString(), QString("agent"));
        // For now: the overview is still the view kept.
        QVERIFY(rig.surface->overviewShown());
        rig.backend->setAsking({});
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(panel->property("view").toString(), QString("overview"));

        // Ctrl+Alt+B: back to the terminal, with the keyboard.
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!overview->isVisible());
        QTRY_VERIFY(panel->property("terminalFocused").toBool());
        QVERIFY(!rig.timeline->active());
        QVERIFY(!rig.surface->overviewShown());
    }

    void aSummaryStandsInForItsPoints()
    {
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_session":{"kind":"id","value":"s-1"},"agent_status":"idle","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})");
        // A turn worth a summary, then many short ones after it.
        QDir(rig.dir.path()).mkpath("projects/-a");
        const auto entry = [](const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'; };
        const auto said = [&](const QString &words) {
            return entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", words}}}}}}});
        };
        QByteArray transcript = entry({{"type", "user"}, {"uuid", "u1"}, {"origin", QJsonObject{{"kind", "human"}}},
                                       {"message", QJsonObject{{"content", "Plant the beds"}}}})
            + said("Digging first.")
            + entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "tool_use"},
                {"id", "b1"}, {"name", "Bash"}, {"input", QJsonObject{{"command", "dig"}, {"description", "Dig the bed"}}}}}}}}})
            + entry({{"type", "user"}, {"message", QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "tool_result"},
                {"tool_use_id", "b1"}, {"content", "ok"}}}}}}})
            + said("The beds are planted.");
        for (int i = 0; i < 20; ++i)
            transcript += entry({{"type", "user"}, {"uuid", QString("n%1").arg(i)}, {"origin", QJsonObject{{"kind", "human"}}},
                                 {"message", QJsonObject{{"content", QString("Note %1").arg(i)}}}}) + said(QString("Noted %1.").arg(i));
        write(rig.dir.filePath("projects/-a/s-1.jsonl"), transcript);
        rig.timeline->setCacheDirectory(rig.dir.filePath("cache"));
        rig.timeline->setSummaryProgram(TESTS_DIR "/fake_claude.sh");
        qputenv("DROVER_TEST_DIR", rig.dir.path().toUtf8());
        rig.surface->setOverviewShown(true);

        rig.expand();
        auto *list = rig.find("overviewList");
        QTRY_COMPARE(list->property("count").toInt(), 21);
        QTRY_VERIFY(list->property("atYEnd").toBool());
        const auto away = [&] { QTest::mouseMove(rig.window, rig.find("viewSwitch")->mapToScene(QPointF(4, 4)).toPoint()); };

        // Scrolled up, past the first turn (as the user would: the list stops
        // following the end): a summary coming in above moves nothing in view.
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        list->setProperty("follow", false);
        QTRY_VERIFY(rig.find("prompt1"));
        list->setProperty("contentY", list->property("contentY").toReal() + 300);
        QTest::qWait(100);
        QTRY_VERIFY(rig.find("prompt8"));
        auto *anchor = rig.find("prompt8");
        const qreal before = anchor->mapToScene(QPointF(0, 0)).y();
        away();
        rig.timeline->setSummaryModel("sonnet");
        QTRY_COMPARE(rig.timeline->index(0).data(Timeline::PointsRole).toList().size(), 1);
        QTest::qWait(100);
        QCOMPARE(anchor->mapToScene(QPointF(0, 0)).y(), before);

        // In view, under the pointer: the points stay until the pointer leaves
        // the turn. (The turn summed up afresh: the cached summary is dropped
        // and comes again.)
        rig.timeline->setSummaryModel("");
        QFile::remove(rig.dir.filePath("cache/s-1.json"));
        rig.timeline->reload();
        QTRY_COMPARE(list->property("count").toInt(), 21);
        QTRY_COMPARE(rig.timeline->state(), QString("ready"));
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        list->setProperty("follow", false);
        // An older turn shows its headline only: opened, all of it.
        QTRY_VERIFY(rig.find("prompt1"));
        QVERIFY(!rig.find("point2"));
        QMetaObject::invokeMethod(rig.find("overview"), "toggle", Q_ARG(QVariant, "turn1"));
        QTRY_VERIFY(rig.find("point2"));
        QTest::mouseMove(rig.window, rig.find("point2")->mapToScene(QPointF(40, 5)).toPoint());
        QTest::qWait(100);
        rig.timeline->setSummaryModel("sonnet");
        QTRY_COMPARE(rig.timeline->index(0).data(Timeline::PointsRole).toList().size(), 1);
        QTRY_VERIFY(rig.find("point2"));
        QTest::qWait(200);
        QVERIFY(rig.find("point2")->isVisible());
        away();
        QTRY_VERIFY(rig.find("summary1_0_0") && rig.find("summary1_0_0")->isVisible());
        QVERIFY(!rig.find("point2") || !rig.find("point2")->isVisible());

        // A turn coming into view under the pointer shows its summary as it is.
        QTest::mouseMove(rig.window, list->mapToScene(QPointF(list->width() / 2, list->height() - 20)).toPoint());
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        QTRY_VERIFY(!rig.find("summary1_0_0"));
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        QTRY_VERIFY(rig.find("summary1_0_0") && rig.find("summary1_0_0")->isVisible());
        QVERIFY(!rig.find("point2") || !rig.find("point2")->isVisible());

        // The drawing the model made, as drawn, in the theme's colors.
        write(rig.dir.filePath("reply.json"), QJsonDocument(QJsonObject{{"type", "result"}, {"is_error", false},
            {"result", QString::fromUtf8(QJsonDocument(QJsonObject{{"points", QJsonArray{QJsonObject{
                {"text", "Noted"}, {"kind", "done"}, {"step", 1}, {"visual", "a  [ok]b[/] <c>"}}}}}).toJson(QJsonDocument::Compact))}})
            .toJson(QJsonDocument::Compact));
        QFile more(rig.dir.filePath("projects/-a/s-1.jsonl"));
        QVERIFY(more.open(QIODevice::Append));
        more.write(entry({{"type", "user"}, {"uuid", "u9"}, {"origin", QJsonObject{{"kind", "human"}}},
                          {"message", QJsonObject{{"content", "Draw it"}}}})
                   + said("One.") + said("Two.") + said("Three.")
                   + entry({{"type", "system"}, {"subtype", "turn_duration"}, {"durationMs", 1000}}));
        more.close();
        QTRY_VERIFY_WITH_TIMEOUT(rig.timeline->index(21).data(Timeline::PointsRole).toList().size() == 1, 10000);
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        QTRY_VERIFY(rig.find("visualText22_0_0"));
        const QString drawn = rig.find("visualText22_0_0")->property("text").toString();
        QVERIFY(drawn.startsWith("a&nbsp;&nbsp;<font color=\""));
        QVERIFY(drawn.endsWith("b</font>&nbsp;&lt;c&gt;"));
        QFile::remove(rig.dir.filePath("reply.json"));
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        QTRY_VERIFY(rig.find("summary1_0_0"));

        // A click on the summary opens the points it sums up, under it.
        auto *point = rig.find("summary1_0_0");
        QTest::qWait(50);
        QTest::mouseClick(rig.window, Qt::LeftButton, {},
                          point->mapToScene(QPointF(point->width() / 2, point->height() / 2)).toPoint());
        QTRY_VERIFY(rig.find("point2") && rig.find("point2")->isVisible());
        QVERIFY(rig.find("point2")->mapToScene(QPointF(0, 0)).x() > point->mapToScene(QPointF(0, 0)).x());
    }

    void aLongTurnShowsItsNewestPoints()
    {
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_session":{"kind":"id","value":"s-1"},"agent_status":"working","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})");
        QDir(rig.dir.path()).mkpath("projects/-a");
        const auto entry = [](const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'; };
        QByteArray transcript = entry({{"type", "user"}, {"origin", QJsonObject{{"kind", "human"}}},
                                       {"message", QJsonObject{{"content", "Plant every bed"}}}});
        for (int i = 1; i <= 200; ++i)
            transcript += entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
                QJsonObject{{"type", "text"}, {"text", QString("Bed %1 is planted.").arg(i)}}}}}}});
        write(rig.dir.filePath("projects/-a/s-1.jsonl"), transcript);
        rig.surface->setOverviewShown(true);
        rig.expand();
        // The newest few only, the rest behind a line.
        QTRY_VERIFY(rig.find("point200"));
        QVERIFY(rig.find("point195"));
        QVERIFY(!rig.find("point194"));
        auto *earlier = rig.find("earlier1");
        QVERIFY(earlier && earlier->isVisible());
        QVERIFY(earlier->childItems().at(0)->property("text").toString().contains("194"));
        // More come: still the newest few.
        QFile more(rig.dir.filePath("projects/-a/s-1.jsonl"));
        QVERIFY(more.open(QIODevice::Append));
        more.write(entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
            QJsonObject{{"type", "text"}, {"text", "Bed 201 is planted."}}}}}}}));
        more.close();
        QTRY_VERIFY(rig.find("point201"));
        QTRY_VERIFY(!rig.find("point195"));
        // A reply that ends with a question: the question, always in sight.
        QFile asks(rig.dir.filePath("projects/-a/s-1.jsonl"));
        QVERIFY(asks.open(QIODevice::Append));
        asks.write(entry({{"type", "assistant"}, {"message", QJsonObject{{"content", QJsonArray{
            QJsonObject{{"type", "text"}, {"text", "All beds are planted.\n\nLong notes " + QString("and more ").repeated(80)
                                                     + "\n\nShall I **water** them now?"}}}}}}}));
        asks.close();
        QTRY_VERIFY(rig.find("question1") && rig.find("question1")->isVisible());
        QCOMPARE(rig.find("question1")->childItems().at(1)->property("text").toString(), QString("Shall I water them now?"));

        // A click shows twenty more, never all at once.
        QTest::qWait(50);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, earlier->mapToScene(QPointF(60, earlier->height() / 2)).toPoint());
        QTRY_VERIFY(rig.find("point177"));
        QVERIFY(!rig.find("point176"));
    }

    void perfRealTranscript()
    {
        const QString real = qEnvironmentVariable("DROVER_PERF_TRANSCRIPT");
        if (real.isEmpty())
            QSKIP("set DROVER_PERF_TRANSCRIPT to a transcript to time the overview on it");
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_session":{"kind":"id","value":"s-1"},"agent_status":"working","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})");
        QDir(rig.dir.path()).mkpath("projects/-a");
        QFile source(real);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray all = source.readAll();
        const int half = all.lastIndexOf('\n', all.size() * 9 / 10) + 1;
        write(rig.dir.filePath("projects/-a/s-1.jsonl"), all.left(half));
        if (qEnvironmentVariableIsSet("DROVER_PERF_SUMMARY")) {
            qputenv("DROVER_TEST_DIR", rig.dir.path().toUtf8());
            rig.timeline->setCacheDirectory(rig.dir.filePath("cache"));
            rig.timeline->setSummaryProgram(TESTS_DIR "/fake_claude.sh");
            rig.timeline->setSummaryModel("sonnet");
        }
        rig.expand();
        QTest::qWait(500);
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(rig.window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(rig.timeline->state() == "ready", 30000);
        qWarning() << "ready after" << timer.elapsed() << "ms, turns" << rig.timeline->count();
        // How long a frame takes while the transcript grows.
        QFile file(rig.dir.filePath("projects/-a/s-1.jsonl"));
        QVERIFY(file.open(QIODevice::Append));
        qint64 worst = 0;
        int at = half;
        for (int round = 0; round < 20; ++round) {
            const int next = all.indexOf('\n', qMin(int(all.size()) - 1, at + (all.size() - half) / 20)) + 1;
            file.write(all.mid(at, next - at));
            file.flush();
            at = next;
            QElapsedTimer frame;
            for (int i = 0; i < 10; ++i) {
                frame.restart();
                QTest::qWait(10);
                worst = qMax(worst, frame.elapsed() - 10);
            }
        }
        qWarning() << "worst stall while growing" << worst << "ms";
    }

    void aNarrowPanelKeepsItsHeaderInside()
    {
        Rig rig(agents("working", "idle"));
        rig.window->resize(820, 900);
        rig.expand();
        auto *panel = rig.find("panel");
        QTRY_VERIFY(panel->property("agentTerminal").value<QQuickItem *>());
        auto *area = rig.find("terminalArea");
        const QRectF panelRect = rig.window->property("panelRect").toRectF();
        // The header keeps to icons: the terminal ends inside the panel.
        QTRY_VERIFY(panelRect.contains(area->mapRectToScene(area->boundingRect())));
        QVERIFY(panelRect.contains(rig.find("viewSwitch")->mapRectToScene(rig.find("viewSwitch")->boundingRect())));
        // The terminal detached: its message and button.
        area->setProperty("detached", true);
        auto *again = rig.find("attachAgain");
        QTRY_VERIFY(again->isVisible());
        const QPointF center = again->mapToScene(QPointF(again->width() / 2, again->height() / 2));
        // A frame for the button to take the pointer.
        QTest::qWait(50);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center.toPoint());
        QTRY_VERIFY(!area->property("detached").toBool());
    }

    void superAltHPutsThePanelOverTheWholeScreen()
    {
        Rig rig(agents("working", "idle"));
        const QRectF whole(12, 12, rig.window->width() - 24, rig.window->height() - 24);
        auto panelRect = [&] { return rig.window->property("panelRect").toRectF(); };
        auto *pill = rig.find("pill");
        QVERIFY(pill);
        // Closed, it opens the panel over the whole screen; the herd steps aside.
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QTRY_VERIFY(rig.window->property("expanded").toBool());
        QCOMPARE(panelRect(), whole);
        QVERIFY(!pill->isVisible());
        auto *panel = rig.find("panel");
        QTRY_VERIFY(panel->property("terminalFocused").toBool());
        // Open, it switches back to the panel's own size, and again; the
        // header's button does the same.
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QVERIFY(rig.window->property("expanded").toBool());
        QVERIFY(panelRect().width() < whole.width());
        // The herd comes back once the panel has shrunk past it.
        QVERIFY(!pill->isVisible());
        QTRY_VERIFY(pill->isVisible());
        // The panel glides; the terminal keeps its size until the end, then
        // takes the new one.
        QTRY_VERIFY(panel->property("agentTerminal").value<QQuickItem *>());
        auto *terminal = panel->property("agentTerminal").value<QQuickItem *>();
        QTRY_COMPARE(panel->width(), panelRect().width());
        QTest::qWait(300);
        const qreal small = terminal->width();
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QTest::qWait(60);
        QVERIFY(panel->width() > panelRect().x() && panel->width() < whole.width());
        QCOMPARE(terminal->width(), small);
        QVERIFY(!pill->isVisible());
        QTRY_COMPARE(panel->width(), whole.width());
        QTRY_VERIFY(terminal->width() > small + 100);
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QTRY_VERIFY(pill->isVisible());
        QTRY_COMPARE(terminal->width(), small);
        auto *button = rig.find("fullscreenButton");
        QTRY_COMPARE(panel->width(), panelRect().width());
        QTest::qWait(50);
        QTest::mouseClick(rig.window, Qt::LeftButton, {},
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_COMPARE(panelRect(), whole);
        // With the help open the help keeps the keyboard: Esc closes it and
        // never reaches the agent.
        QTest::keyClick(rig.window, Qt::Key_F1);
        QTRY_VERIFY(panel->property("helpVisible").toBool());
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QMetaObject::invokeMethod(rig.window, "toggleFullscreen");
        QTest::qWait(50);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("helpVisible").toBool());
        QTest::qWait(100);
        QVERIFY(!rig.log().contains("key 1b"));
        // Closed, the herd comes back once the panel has faded, and the
        // panel opens over the whole screen again, as it was left.
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(!rig.window->property("expanded").toBool());
        QTRY_VERIFY(pill->isVisible());
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(rig.window->property("expanded").toBool());
        QCOMPARE(panelRect(), whole);
        QVERIFY(!pill->isVisible());
        // The next start too, with the list as it was left.
        rig.surface->setListHidden(true);
        Surface next(rig.dir.filePath("position"));
        QVERIFY(next.fullscreen());
        QVERIFY(next.listHidden());
        rig.surface->setFullscreen(false);
        rig.surface->setListHidden(false);
        Surface after(rig.dir.filePath("position"));
        QVERIFY(!after.fullscreen());
        QVERIFY(!after.listHidden());
    }

    void ctrlSemicolonOpensATerminalBelowTheEditor()
    {
        Rig rig(agents("working", "idle"));
        rig.backend->setEditor("sh");
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(rig.backend->editing());
        QTest::keyClick(rig.window, Qt::Key_Semicolon, Qt::ControlModifier);
        QTRY_VERIFY(rig.backend->shellShown());
        QVERIFY(rig.log().contains("pane split w1:p9 --direction down --cwd /a --no-focus"));
        QVERIFY(rig.log().contains("pane rename w1:p10 shell"));
        QTRY_VERIFY(rig.log().contains("terminal attach term_10"));
        auto *shell = rig.find("shellTerminal");
        QVERIFY(shell);
        QTRY_VERIFY(rig.find("panel")->property("shellFocused").toBool());

        // Ctrl+; hides it and gives the editor the keyboard; again shows it.
        QTest::keyClick(rig.window, Qt::Key_Semicolon, Qt::ControlModifier);
        QTRY_VERIFY(!rig.backend->shellShown());
        QVERIFY(!shell->isVisible());
        QTest::keyClick(rig.window, Qt::Key_Semicolon, Qt::ControlModifier);
        QTRY_VERIFY(rig.backend->shellShown());
        QCOMPARE(rig.log().count("pane split w1:p9"), 1);

        // Ctrl+W in it closes it for good.
        QTRY_VERIFY(rig.find("panel")->property("shellFocused").toBool());
        QTest::keyClick(rig.window, Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY(rig.log().contains("pane close w1:p10"));
        QVERIFY(!rig.backend->shellShown());
        QCOMPARE(rig.backend->shellTerminal(), QString());
    }

    // The terminal shows once herdr's first screen has settled, so the cursor
    // never jumps across it; an agent whose output never pauses shows soon
    // anyway, well before the 400 ms patience.
    void aTerminalShowsOnceItsScreenSettles()
    {
        for (const QString mode : {"burst", "stream"}) {
            Rig rig(agents("idle", "idle"));
            const QString herdr = rig.dir.filePath("herdr");
            QFile file(herdr);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QString script = QString::fromUtf8(file.readAll());
            file.close();
            const QString out = mode == "burst" ? "printf 'hello\\r\\n'; sleep 30"
                                                : "while :; do printf x; sleep 0.02; done";
            const QString marker = "\"terminal attach\")";
            QVERIFY(script.contains(marker));
            script.replace(marker, marker + "\n    " + out + " ;;\n  \"never used\")");
            QFile::remove(herdr);
            write(herdr, script.toUtf8());
            QFile::setPermissions(herdr, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QElapsedTimer timer;
            timer.start();
            rig.expand();
            QQuickItem *view = nullptr;
            QTRY_VERIFY((view = rig.find("agentTerminal")));
            QTRY_VERIFY_WITH_TIMEOUT(view->property("ready").toBool(), 2000);
            // The view's own readiness, not the 400 ms patience behind it.
            QVERIFY2(timer.elapsed() < 380, qPrintable(mode + QString(" took %1 ms").arg(timer.elapsed())));
        }
    }

    void switchingAgentsKeepsTheOldTerminalUntilTheNewOneDraws()
    {
        Rig rig(agents("working", "idle"));
        rig.expand();
        QTRY_VERIFY(rig.log().contains("terminal attach term_1"));
        const auto count = [&] {
            int n = 0;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                n += item->objectName() == "agentTerminal";
                for (auto *child : item->childItems())
                    walk(child);
            };
            walk(rig.window->contentItem());
            return n;
        };
        QTRY_COMPARE(count(), 1);
        rig.backend->selectNext(1);
        // The fake agent draws nothing: the old one stays for a moment...
        QTRY_VERIFY(rig.log().contains("terminal attach term_2"));
        QCOMPARE(count(), 2);
        // ...then goes, at the latest after 400 ms.
        QTRY_COMPARE_WITH_TIMEOUT(count(), 1, 1500);
    }

    void anEditorAlreadyInTheTabIsReused()
    {
        Rig rig(agents("working", "idle"));
        rig.backend->setEditor("sh");
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1","cwd":"/a"},
          {"pane_id":"w1:p4","tab_id":"w1:t1","terminal_id":"term_e","label":"editor"},
          {"pane_id":"w2:p4","tab_id":"w2:t1","terminal_id":"term_other","label":"editor"}]}})");
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_E, Qt::AltModifier);
        QTRY_VERIFY(rig.backend->editing());
        QCOMPARE(rig.backend->editorTerminal(), QString("term_e"));
        QVERIFY(!rig.log().contains("pane split"));
        // Alt+Down picks the next agent from the editor too; the panel then
        // shows that agent, not an editor.
        QTest::keyClick(rig.window, Qt::Key_Down, Qt::AltModifier);
        QTRY_VERIFY(!rig.backend->editing());
        QCOMPARE(rig.backend->selected(), QString("w2:p1"));
    }

    void anAgentThatEndsTakesItsEditorAndShellAlong()
    {
        Rig rig(agents("working", "idle"));
        // w1's agent has an editor beside it; w2's has none.
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1"},
          {"pane_id":"w1:p4","tab_id":"w1:t1","terminal_id":"term_e","label":"editor"},
          {"pane_id":"w2:p1","tab_id":"w2:t1","terminal_id":"term_2"}]}})");
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[]}})");
        QTRY_VERIFY_WITH_TIMEOUT(rig.log().contains("pane close w1:p4"), 8000);
        QTRY_VERIFY(rig.log().contains("pane close w1:p1"));
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("pane close w2:p1"));
    }

    void anAgentThatEndsTakesItsTabAlong()
    {
        Rig rig(agents("working", "idle"));
        // w1's agent ran in a tab of its own beside the workspace's first;
        // w2's runs in the workspace's only tab, which keeps it open.
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p9","tab_id":"w1:t1","terminal_id":"term_9","workspace_id":"w1"},
          {"pane_id":"w1:p1","tab_id":"w1:t2","terminal_id":"term_1","workspace_id":"w1"},
          {"pane_id":"w1:p4","tab_id":"w1:t2","terminal_id":"term_e","workspace_id":"w1","label":"editor"},
          {"pane_id":"w2:p1","tab_id":"w2:t1","terminal_id":"term_2","workspace_id":"w2"}]}})");
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[]}})");
        QTRY_VERIFY_WITH_TIMEOUT(rig.log().contains("tab close w1:t2"), 8000);
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("tab close w1:t1"));
        QVERIFY(!rig.log().contains("tab close w2:t1"));
        QVERIFY(!rig.log().contains("pane close"));
    }

    void aTabWhereAnotherAgentWorksStays()
    {
        Rig rig(agents("working", "idle"));
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p9","tab_id":"w1:t1","terminal_id":"term_9","workspace_id":"w1"},
          {"pane_id":"w1:p1","tab_id":"w1:t2","terminal_id":"term_1","workspace_id":"w1"},
          {"pane_id":"w1:p3","tab_id":"w1:t2","terminal_id":"term_3","workspace_id":"w1","agent":"codex"},
          {"pane_id":"w2:p1","tab_id":"w2:t1","terminal_id":"term_2","workspace_id":"w2"}]}})");
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[]}})");
        QTest::qWait(4500);
        QVERIFY(!rig.log().contains("tab close"));
    }

    void anAgentMissingForAMomentKeepsItsEditor()
    {
        Rig rig(agents("working", "idle"));
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1"},
          {"pane_id":"w1:p4","tab_id":"w1:t1","terminal_id":"term_e","label":"editor"}]}})");
        // herdr doesn't see the agents for one poll, then does again.
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[]}})");
        QTRY_COMPARE(rig.backend->agents()->count(), 0);
        write(rig.dir.filePath("agents.json"), agents("working", "idle"));
        QTRY_COMPARE(rig.backend->agents()->count(), 2);
        QTest::qWait(3500);
        QVERIFY(!rig.log().contains("pane close"));
    }

    void anEditorSharedWithAnotherAgentStays()
    {
        Rig rig(agents("working", "idle"));
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1"},
          {"pane_id":"w1:p2","tab_id":"w1:t1","terminal_id":"term_3","agent":"codex"},
          {"pane_id":"w1:p4","tab_id":"w1:t1","terminal_id":"term_e","label":"editor"}]}})");
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","workspace_id":"w2"}]}})");
        QTest::qWait(4500);
        QVERIFY(!rig.log().contains("pane close"));
    }

    void aPaneWhereAnAgentStillRunsIsNotClosed()
    {
        Rig rig(agents("working", "idle"));
        write(rig.dir.filePath("panes.json"), R"({"result":{"panes":[
          {"pane_id":"w1:p1","tab_id":"w1:t1","terminal_id":"term_1","agent":"claude"},
          {"pane_id":"w1:p4","tab_id":"w1:t1","terminal_id":"term_e","label":"editor"}]}})");
        write(rig.dir.filePath("agents.json"), R"({"result":{"agents":[
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","workspace_id":"w2"}]}})");
        QTRY_VERIFY_WITH_TIMEOUT(rig.log().contains("pane close w1:p4"), 8000);
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("pane close w1:p1"));
    }

    void anAgentsQuestionComesOnStageWithoutTakingTheKeyboard()
    {
        Rig rig(agents("working", "idle"));
        auto *stage = rig.find("askStage");
        QVERIFY(stage);
        QVERIFY(!stage->isVisible());
        AskingHook hook(questionInput(false), "w1:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(stage->isVisible());
        // The sheep counts as asking in the pill.
        QTRY_COMPARE(rig.backend->blocked(), 1);
        QTRY_VERIFY(rig.find("stageSheep")->isVisible());  // on its mark after the entrance
        QVERIFY(rig.find("stageSheep")->property("asking").toBool());
        QCOMPARE(rig.find("stageQuestion")->property("text").toString(), QString("Which colors?"));
        // Keys typed elsewhere never answer it.
        QTest::keyClick(rig.window, Qt::Key_1);
        QTest::qWait(100);
        QVERIFY(!hook.done);
        QVERIFY(!stage->property("hasKeyboard").toBool());
        // drover's key gives it the keyboard.
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(stage->property("hasKeyboard").toBool());
        QVERIFY(!rig.window->property("expanded").toBool());
        // Pressed again, it takes the keyboard back; the question stays.
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(!stage->property("hasKeyboard").toBool());
        QVERIFY(stage->isVisible());
        QVERIFY(!rig.window->property("expanded").toBool());
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(stage->property("hasKeyboard").toBool());
        QTest::keyClick(rig.window, Qt::Key_2);
        QTRY_VERIFY(hook.done);
        QCOMPARE(hook.result().value("updatedInput").toObject().value("answers").toObject()
                     .value("Which colors?").toString(), QString("Green"));
        QTRY_VERIFY(!stage->isVisible());
        QTRY_COMPARE(rig.backend->blocked(), 0);
    }

    void theSpotlightLookWorksToo()
    {
        Rig rig(agents("working", "idle"));
        rig.asker->setLook("spotlight");
        auto *stage = rig.find("askStage");
        stage->setProperty("look", "spotlight");
        AskingHook hook(questionInput(false), "w1:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(stage->isVisible());
        QTRY_VERIFY(rig.find("stageSheep")->isVisible());
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(stage->property("hasKeyboard").toBool());
        QTest::keyClick(rig.window, Qt::Key_1);
        QTRY_VERIFY(hook.done);
        QCOMPARE(hook.result().value("updatedInput").toObject().value("answers").toObject()
                     .value("Which colors?").toString(), QString("Red"));
        QTRY_VERIFY(!stage->isVisible());
    }

    void theStageTicksSeveralAndTakesYourOwnWords()
    {
        Rig rig(agents("working", "idle"));
        auto *stage = rig.find("askStage");
        AskingHook hook(questionInput(true), "w1:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(stage->isVisible());
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(stage->property("hasKeyboard").toBool());
        QTest::keyClick(rig.window, Qt::Key_1);
        QTest::keyClick(rig.window, Qt::Key_3);
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_COMPARE(rig.find("stageQuestion")->property("text").toString(), QString("Which size?"));
        QVERIFY(!hook.done);
        QTest::keyClick(rig.window, Qt::Key_3);  // the own-answer row
        QTRY_VERIFY(rig.find("stageOwnField")->hasActiveFocus());
        rig.type("Medium");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_VERIFY(hook.done);
        const QJsonObject answers = hook.result().value("updatedInput").toObject().value("answers").toObject();
        QCOMPARE(answers.value("Which colors?").toString(), QString("Red, Blue"));
        QCOMPARE(answers.value("Which size?").toString(), QString("Medium"));
    }

    void theStageAnswersAPermissionOrLeavesItToTheTerminal()
    {
        Rig rig(agents("blocked", "idle"));
        auto *stage = rig.find("askStage");
        {
            AskingHook hook(permissionInput(), "w1:p1", rig.askServer, rig.asker.get());
            QTRY_VERIFY(stage->isVisible());
            QCOMPARE(rig.find("stageDetail")->property("text").toString(), QString("rm -rf build"));
            QMetaObject::invokeMethod(rig.window, "toggle");
            QTRY_VERIFY(stage->property("hasKeyboard").toBool());
            QTest::keyClick(rig.window, Qt::Key_1);
            QTRY_VERIFY(hook.done);
            QCOMPARE(hook.result().value("decision").toObject().value("behavior").toString(), QString("allow"));
        }
        QTRY_VERIFY(!stage->isVisible());
        {
            AskingHook hook(permissionInput(), "w1:p1", rig.askServer, rig.asker.get());
            QTRY_VERIFY(stage->isVisible());
            QMetaObject::invokeMethod(rig.window, "toggle");
            QTRY_VERIFY(stage->property("hasKeyboard").toBool());
            QTest::keyClick(rig.window, Qt::Key_Escape);
            QTRY_VERIFY(hook.done);
            QVERIFY(hook.output.isEmpty());  // Claude's own prompt answers it
            // Esc on the stage doesn't close anything else.
            QVERIFY(!rig.window->property("expanded").toBool());
        }
    }

    void theStageNeverCoversThePill()
    {
        Rig rig(agents("working", "idle"));
        AskingHook hook(questionInput(false), "w1:p1", rig.askServer, rig.asker.get());
        auto *stage = rig.find("askStage");
        QTRY_VERIFY(stage->isVisible());
        const QRectF pill(rig.window->property("pillX").toReal(), rig.window->property("pillY").toReal(),
                          rig.window->property("pillWidth").toReal(), rig.window->property("pillHeight").toReal());
        for (const QPointF offset : {QPointF(12, 12), QPointF(1700, 12), QPointF(12, 1100), QPointF(900, 600)}) {
            rig.surface->setOffset(offset);
            QTest::qWait(20);
            const QRectF p(rig.window->property("pillX").toReal(), rig.window->property("pillY").toReal(),
                           pill.width(), pill.height());
            const QRectF s(stage->x(), stage->y(), stage->width(), stage->height());
            QVERIFY2(!s.intersects(p), qPrintable(QString("offset %1,%2").arg(offset.x()).arg(offset.y())));
            QVERIFY(s.left() >= 0 && s.right() <= rig.window->width() && s.top() >= 0 && s.bottom() <= rig.window->height());
        }
        rig.asker->pass(rig.asker->current().value("id").toInt());
    }

    void aTurnsLastQuestionIsAnsweredFromTheStage()
    {
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_status":"done","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1","agent_session":{"value":"s1"}},
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2","agent_session":{"value":"s2"}}]}})");
        QSignalSpy typed(rig.asker.get(), &Asker::typeRequested);
        auto *stage = rig.find("askStage");
        const auto ask = [&] {
            const QJsonObject stop{{"hook_event_name", "Stop"}, {"session_id", "s1"},
                                   {"last_assistant_message", "The beds are planted.\n\nShall I water them too?"}};
            QVERIFY(Hook::run(QJsonDocument(stop).toJson(), {{"HERDR_PANE_ID", "w1:p1"}}, rig.askServer).isEmpty());
            QTRY_VERIFY(stage->isVisible());
            QTRY_VERIFY(rig.find("stageQuestion")->property("text").toString().endsWith("Shall I water them too?"));
            QMetaObject::invokeMethod(rig.window, "toggle");
            QTRY_VERIFY(stage->property("hasKeyboard").toBool());
        };
        ask();
        QTRY_COMPARE(rig.backend->blocked(), 1);
        QTest::keyClick(rig.window, Qt::Key_1);
        QTRY_COMPARE(typed.size(), 1);
        QCOMPARE(typed.at(0).at(0).toString(), QString("w1:p1"));
        QCOMPARE(typed.at(0).at(2).toString(), QString("Yes"));
        QTRY_VERIFY(!stage->isVisible());

        ask();
        QTest::keyClick(rig.window, Qt::Key_2);
        QTRY_VERIFY(rig.find("stageOwnField")->hasActiveFocus());
        rig.type("only the tomatoes");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_COMPARE(typed.size(), 2);
        QCOMPARE(typed.at(1).at(2).toString(), QString("only the tomatoes"));
        QTRY_VERIFY(!stage->isVisible());

        // Esc leaves it for later: nothing is typed.
        ask();
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY(!stage->isVisible());
        QCOMPARE(typed.size(), 2);
    }

    void aTurnsOpenQuestionTakesOnlyWords()
    {
        Rig rig(R"({"result":{"agents":[
          {"agent":"claude","agent_status":"done","cwd":"/a","pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the beds","workspace_id":"w1","agent_session":{"value":"s1"}},
          {"agent":"claude","agent_status":"idle","cwd":"/b","pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Fix the gate","workspace_id":"w2","agent_session":{"value":"s2"}}]}})");
        QSignalSpy typed(rig.asker.get(), &Asker::typeRequested);
        auto *stage = rig.find("askStage");
        const QJsonObject stop{{"hook_event_name", "Stop"}, {"session_id", "s1"},
                               {"last_assistant_message", QString("word ").repeated(110) + "\n\nWhich bed is next?"}};
        QVERIFY(Hook::run(QJsonDocument(stop).toJson(), {{"HERDR_PANE_ID", "w1:p1"}}, rig.askServer).isEmpty());
        QTRY_VERIFY(stage->isVisible());
        // No "Yes" to "which": only your own answer, and the question shows.
        QVERIFY(!rig.find("stageOption0"));
        auto *question = rig.find("stageQuestion");
        const QRectF shown = question->parentItem()->mapRectToScene(QRectF(0, 0, question->parentItem()->width(),
                                                                           question->parentItem()->height()));
        const QRectF last = question->mapRectToScene(QRectF(0, question->height() - 4, question->width(), 4));
        QVERIFY(shown.contains(last));
        QMetaObject::invokeMethod(rig.window, "toggle");
        QTRY_VERIFY(rig.find("stageOwnField")->hasActiveFocus());
        rig.type("the north one");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTRY_COMPARE(typed.size(), 1);
        QCOMPARE(typed.at(0).at(2).toString(), QString("the north one"));
    }

    void aClickIntoTheOwnAnswerTakesTheKeyboard()
    {
        Rig rig(agents("working", "idle"));
        auto *stage = rig.find("askStage");
        QSignalSpy wants(stage, SIGNAL(wantsKeyboard()));
        AskingHook hook(questionInput(false), "w1:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(stage->isVisible());
        QTRY_COMPARE(stage->property("opacity").toReal(), 1.0);
        QTest::qWait(700);  // the entrance
        // Right into the field, not its row: drover must ask the compositor
        // for the keyboard, or the typing lands in the window beneath.
        auto *field = rig.find("stageOwnField");
        const QPointF center = field->mapToScene(QPointF(field->width() / 2, field->height() / 2));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center.toPoint());
        QTRY_VERIFY(field->hasActiveFocus());
        QVERIFY(wants.size() >= 1);
        QVERIFY(stage->property("hasKeyboard").toBool());
    }

    void nothingComesOnStageWhileThePanelIsOpen()
    {
        Rig rig(agents("working", "idle"));
        rig.expand();
        auto *stage = rig.find("askStage");
        AskingHook hook(questionInput(false), "w2:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(rig.asker->current().contains("id"));
        QTest::qWait(300);
        QVERIFY(!stage->isVisible());
        // It waits, marked in the list, and comes once the panel closes.
        QMetaObject::invokeMethod(rig.window, "setExpanded", Q_ARG(QVariant, false));
        QTRY_VERIFY(stage->isVisible());
        rig.asker->pass(rig.asker->current().value("id").toInt());
    }

    void pickingTheAgentThatAsksGivesItsTerminalTheQuestion()
    {
        Rig rig(agents("working", "idle"));
        rig.asker->setWatching([&rig](const QString &pane) {
            return rig.window->property("expanded").toBool() && rig.backend->selected() == pane;
        });
        rig.expand();
        AskingHook hook(questionInput(false), "w2:p1", rig.askServer, rig.asker.get());
        QTRY_VERIFY(rig.asker->current().contains("id"));
        QCOMPARE(rig.backend->selected(), QString("w1:p1"));
        rig.backend->select("w2:p1");
        QTRY_VERIFY(rig.asker->current().isEmpty());
        QTRY_VERIFY(hook.done);
        QVERIFY(hook.output.isEmpty());
    }

    void thePanelNeverCoversThePill()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        const int width = rig.window->width(), height = rig.window->height();
        for (const QPointF offset : {QPointF(12, 12), QPointF(width / 2.0, height / 2.0), QPointF(width - 200, 12),
                                     QPointF(800, height - 60), QPointF(width - 188, height - 48)}) {
            rig.surface->setOffset(offset);
            QTest::qWait(10);
            const QRectF panel = rig.window->property("panelRect").toRectF();
            const QRectF pill(rig.window->property("pillX").toReal(), rig.window->property("pillY").toReal(), 176, 36);
            QVERIFY2(!panel.intersects(pill), qPrintable(QString("offset %1,%2").arg(offset.x()).arg(offset.y())));
            QVERIFY(panel.left() >= 0 && panel.top() >= 0 && panel.right() <= width && panel.bottom() <= height);
        }
    }
};

QTEST_MAIN(DroverUiTests)
#include "drover_ui_tests.moc"
