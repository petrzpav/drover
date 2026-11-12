// Drives the real Main.qml offscreen against a fake herdr, for the behavior
// that lives in QML: where the keyboard goes, what a click can reach, and
// where the panel opens.

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "surface.h"
#include "terminalview.h"
#include "theme.h"
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

// One drover with a fake herdr in a temporary directory.
struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Translator> translator;
    std::unique_ptr<Theme> theme;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    int quits = 0;

    explicit Rig(const QByteArray &list) {
        write(dir.filePath("agents.json"), list);
        write(dir.filePath("ws.json"), R"({"result":{"workspaces":[{"workspace_id":"w1","label":"garden"},{"workspace_id":"w2","label":"gate"}]}})");
        const QString herdr = dir.filePath("herdr");
        write(herdr, QString(R"sh(#!/bin/sh
echo "$@" >> '%1/log'
case "$1 $2" in
  "workspace list") cat '%1/ws.json' ;;
  "agent list") cat '%1/agents.json' ;;
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
        engine.reset(new QQmlApplicationEngine);
        QObject::connect(engine.get(), &QQmlEngine::quit, engine.get(), [this] { ++quits; });
        engine->rootContext()->setContextProperty("translator", translator.get());
        engine->rootContext()->setContextProperty("theme", theme.get());
        engine->rootContext()->setContextProperty("backend", backend.get());
        engine->rootContext()->setContextProperty("surface", surface.get());
        engine->load(QUrl("qrc:/Main.qml"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        QVERIFY(window);
        window->resize(1920, 1174);
        window->show();
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTRY_COMPARE(backend->agents()->count(), 2);
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
        QVERIFY(!rig.find("help")->isVisible());
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
        QTest::keyClick(rig.window, Qt::Key_Down);
        QTest::keyClick(rig.window, Qt::Key_Return);
        // "garden" and "gate" both start with "ga"; Down picked the second.
        QTRY_VERIFY(rig.log().contains("tab create --workspace w2"));
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
