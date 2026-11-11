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
      {"agent":"claude","agent_status":")") + first + R"(","cwd":"/a","pane_id":"w1:p1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"},
      {"agent":"claude","agent_status":")" + second + R"(","cwd":"/b","pane_id":"w2:p1","terminal_title_stripped":"Fix the gate","workspace_id":"w2"}]}})";
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
  "agent read") printf 'line one\nline two\n' ;;
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
    template <typename T = QQuickItem> T *find(const char *name) { return window->findChild<T *>(name); }
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
    void initTestCase() { QQuickStyle::setStyle("Basic"); }

    void anAgentThatStartsAskingTakesNoKeys()
    {
        Rig rig(agents("working", "idle"));
        rig.expand();
        auto *message = rig.find("messageField");
        QVERIFY(message->hasActiveFocus());
        rig.type("ab");

        // The agent asks something mid-typing: the next keys answer nothing.
        write(rig.dir.filePath("agents.json"), agents("blocked", "idle"));
        QTRY_COMPARE(rig.backend->selectedStatus(), QString("blocked"));
        QTest::qWait(50);
        rig.type("1q ");
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("send-keys"));
        QCOMPARE(rig.quits, 0);
        QVERIFY(rig.window->property("expanded").toBool());

        // Tab hands the keyboard to the answer keys on purpose.
        QTest::keyClick(rig.window, Qt::Key_Tab);
        QVERIFY(rig.find("answerBar")->hasActiveFocus());
        rig.type("2");
        QTRY_VERIFY(rig.log().contains("agent send-keys w1:p1 2\n"));

        // Once it stops asking, the message box has the keyboard again, with
        // the draft still there.
        write(rig.dir.filePath("agents.json"), agents("working", "idle"));
        QTRY_VERIFY(message->hasActiveFocus());
        QCOMPARE(message->property("text").toString(), QString("ab"));
    }

    void aDraftNeverMovesToAnotherAgent()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        rig.backend->select("w2:p1");
        rig.find("messageField")->forceActiveFocus();
        rig.type("water");
        write(rig.dir.filePath("agents.json"),
              R"({"result":{"agents":[{"agent":"claude","agent_status":"idle","cwd":"/a","pane_id":"w1:p1","terminal_title_stripped":"Plant the beds","workspace_id":"w1"}]}})");
        QTRY_VERIFY(rig.backend->selected().isEmpty());
        QTest::keyClick(rig.window, Qt::Key_Return);
        QTest::qWait(300);
        QVERIFY(!rig.log().contains("agent prompt"));
    }

    void helpCoversWhatIsBehindIt()
    {
        Rig rig(agents("blocked", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_F1);
        auto *help = rig.find("help");
        QTRY_VERIFY(help->isVisible());
        // A click where the answer keys are only closes the help.
        const QPointF bottom = help->mapToScene(QPointF(help->width() / 2, help->height() - 40));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, bottom.toPoint());
        QTest::qWait(300);
        QVERIFY(!help->isVisible());
        QVERIFY(!rig.log().contains("send-keys"));
        QCOMPARE(rig.quits, 0);
    }

    void escapeClosesOneThingAtATime()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_F1);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QVERIFY(!rig.find("help")->isVisible());
        QVERIFY(rig.window->property("expanded").toBool());
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QVERIFY(!rig.window->property("expanded").toBool());
    }

    void quitNeedsControl()
    {
        Rig rig(agents("idle", "idle"));
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_Escape);
        rig.expand();
        QTest::keyClick(rig.window, Qt::Key_Q);
        QCOMPARE(rig.quits, 0);
        QTest::keyClick(rig.window, Qt::Key_Q, Qt::ControlModifier);
        QCOMPARE(rig.quits, 1);
    }

    void pickerStartsTheDefaultAgent()
    {
        Rig rig(agents("idle", "idle"));
        rig.backend->setAgentKind("claude");
        write(rig.dir.filePath("tab.json"), "");
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
