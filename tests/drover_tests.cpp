#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "agentmodel.h"
#include "backend.h"
#include "terminalfont.h"
#include "terminalview.h"
#include "theme.h"
#include "translator.h"

class DroverTests : public QObject {
    Q_OBJECT

private slots:
    void readsTheTerminalFont()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath(".config/ghostty");
        QDir(dir.path()).mkpath(".config/foot");
        write(dir.path() + "/.config/xdg-terminals.list", "# preference\ncom.mitchellh.ghostty.desktop\n");
        write(dir.path() + "/.config/ghostty/config", "font-family = \"Fira Code\"\nfont-size = 11.5\n");
        write(dir.path() + "/.config/foot/foot.ini", "font=Iosevka:size=9\n");
        QFont font = TerminalFont::read(dir.path());
        QCOMPARE(font.family(), QString("Fira Code"));
        QCOMPARE(font.pointSizeF(), 11.5);

        write(dir.path() + "/.config/xdg-terminals.list", "foot.desktop\n");
        font = TerminalFont::read(dir.path());
        QCOMPARE(font.family(), QString("Iosevka"));
        QCOMPARE(font.pointSizeF(), 9.0);

        QTemporaryDir empty;
        QCOMPARE(TerminalFont::read(empty.path()).pointSizeF(), 10.0);
    }

    void parsesAgents()
    {
        const auto agents = Backend::parseAgents(agentsJson(), {{"w1", "garden"}}, "/home/ann");
        QCOMPARE(agents.size(), 3);
        QCOMPARE(agents[0].paneId, QString("w1:p1"));
        QCOMPARE(agents[0].title, QString("Plant the tomatoes"));
        QCOMPARE(agents[0].status, QString("working"));
        QCOMPARE(agents[0].workspace, QString("garden"));
        QCOMPARE(agents[0].cwd, QString("~/garden"));
        // A renamed agent shows its name; an untitled one what it is and where.
        QCOMPARE(agents[1].title, QString("reviewer"));
        QCOMPARE(agents[1].workspace, QString("w2"));
        QCOMPARE(agents[2].title, QString("codex · shed"));
        QCOMPARE(agents[2].cwd, QString("/srv/shed"));
    }

    void reportsBadListings()
    {
        QString error;
        QVERIFY(Backend::parseAgents("not json", {}, {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void readsCommandErrors()
    {
        QCOMPARE(Backend::commandError(R"({"error":{"code":"agent_blocked","message":"agent is blocked"}})", {}, 0),
                 QString("agent is blocked"));
        QCOMPARE(Backend::commandError({}, "no server\nmore", 1), QString("no server"));
        const QByteArray busy = R"({"error":{"code":"agent_not_idle","message":"cannot read 400 lines"},"id":"x"})";
        QCOMPARE(Backend::commandError({}, busy, 1), QString("cannot read 400 lines"));
        QCOMPARE(Backend::errorCode({}, busy), QString("agent_not_idle"));
        QVERIFY(Backend::commandError(R"({"result":{}})", {}, 0).isEmpty());
    }

    void modelKeepsRowsInPlace()
    {
        AgentModel model;
        auto agents = Backend::parseAgents(agentsJson(), {}, {});
        model.setAgents(agents);
        QSignalSpy resets(&model, &AgentModel::modelReset);
        QSignalSpy changes(&model, &AgentModel::dataChanged);
        agents[1].status = "done";
        model.setAgents(agents);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(changes.count(), 1);
        // Agents come, go, and move without a reset, which would drop the
        // list's scroll position.
        QSignalSpy removed(&model, &AgentModel::rowsRemoved);
        QSignalSpy inserted(&model, &AgentModel::rowsInserted);
        QSignalSpy moved(&model, &AgentModel::rowsMoved);
        agents.removeFirst();
        model.setAgents(agents);
        QCOMPARE(model.count(), 2);
        Agent fresh;
        fresh.paneId = "w9:p1";
        agents.prepend(fresh);
        agents.swapItemsAt(1, 2);
        model.setAgents(agents);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(model.agents(), agents);
    }

    void followsHerdr()
    {
        QTemporaryDir dir;
        const QString herdr = fakeHerdr(dir);
        Backend backend(herdr, dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.agents()->count(), 3);
        QVERIFY(backend.error().isEmpty());
        QCOMPARE(backend.working(), 1);
        QCOMPARE(backend.blocked(), 1);
        QCOMPARE(backend.idle(), 1);
        QCOMPARE(backend.agents()->agents().first().workspace, QString("garden"));

        // The first agent is picked, with its terminal to attach to.
        QCOMPARE(backend.selected(), QString("w1:p1"));
        QCOMPARE(backend.selectedTerminal(), QString("term_1"));

        backend.selectNext(1);
        QCOMPARE(backend.selectedStatus(), QString("blocked"));
        backend.selectNext(5);
        QCOMPARE(backend.selected(), QString("w3:p2"));
        QCOMPARE(backend.selectedTerminal(), QString("term_3"));
    }

    void keepsTheListWhenAPollFails()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.agents()->count(), 3);
        backend.select("w3:p2");
        write(dir.filePath("agent-list.fail"), "connection refused\n");
        QTRY_VERIFY(backend.error().contains("connection refused"));
        QCOMPARE(backend.agents()->count(), 3);
        QCOMPARE(backend.selected(), QString("w3:p2"));
        QFile::remove(dir.filePath("agent-list.fail"));
        QTRY_VERIFY(backend.error().isEmpty());
    }

    void selectsNothingWhenThePaneCloses()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.agents()->count(), 3);
        backend.select("w3:p2");
        write(dir.filePath("agents.json"), agentsJson().replace("\"pane_id\":\"w3:p2\"", "\"pane_id\":\"w3:p9\""));
        QTRY_VERIFY(backend.selected().isEmpty());
        QVERIFY(backend.selectionLost());
        // Later polls don't pick another agent on their own, so the
        // terminal never attaches to an agent the user didn't choose.
        QTest::qWait(1200);
        QVERIFY(backend.selected().isEmpty());
        QVERIFY(backend.selectedTerminal().isEmpty());
        backend.select("w1:p1");
        QVERIFY(!backend.selectionLost());
    }

    void doesOneThingAtATime()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        write(dir.filePath("agent-focus.sleep"), "0.4");
        backend.jump();
        QCOMPARE(backend.busy(), QString("open"));
        backend.jump();
        backend.startAgent("w1");
        QTRY_COMPARE(backend.busy(), QString());
        QCOMPARE(log(dir).count("agent focus"), 1);
        QVERIFY(!log(dir).contains("tab create"));
    }

    void saysWhenACommandHangs()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        backend.setTimeout(300);
        write(dir.filePath("agent-focus.sleep"), "2");
        backend.jump();
        QTRY_VERIFY(backend.notice().contains("no answer"));
        QCOMPARE(backend.busy(), QString());
    }

    void bringsHerdrForward()
    {
        QTemporaryDir dir;
        const QString hyprctl = dir.filePath("hyprctl");
        write(dir.filePath("clients.json"), R"([{"class":"firefox","address":"0x1"},{"class":"org.omarchy.herdr","address":"0x2"}])");
        write(hyprctl, QString(R"(#!/bin/sh
echo "$@" >> '%1/hyprctl.log'
[ "$1" = clients ] && cat '%1/clients.json' && exit 0
echo ok
)").arg(dir.path()).toUtf8());
        QFile::setPermissions(hyprctl, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        Backend backend(fakeHerdr(dir), hyprctl);
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        QSignalSpy jumped(&backend, &Backend::jumped);
        backend.jump();
        QTRY_COMPARE(jumped.count(), 1);
        QVERIFY(log(dir).contains("agent focus w1:p1\n"));
        QFile file(dir.filePath("hyprctl.log"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("dispatch focuswindow address:0x2"));

        // Without a herdr window the panel stays open and says why.
        write(dir.filePath("clients.json"), "[]");
        backend.jump();
        QTRY_COMPARE(backend.notice(), QString("No herdr window is open."));
        QCOMPARE(jumped.count(), 1);
    }

    void saysWhenHyprctlIsMissing()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        QSignalSpy jumped(&backend, &Backend::jumped);
        backend.jump();
        QTRY_VERIFY(backend.noticeIsError() && !backend.notice().isEmpty());
        QCOMPARE(jumped.count(), 0);
    }

    void startsAnAgentInAWorkspace()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        backend.setAgentKind("codex");
        QTRY_COMPARE(backend.workspaces().size(), 1);
        QCOMPARE(backend.workspaces().first().toMap().value("label").toString(), QString("garden"));
        QCOMPARE(backend.workspaces().first().toMap().value("agents").toInt(), 1);

        write(dir.filePath("pane-list.out"),
              R"({"result":{"panes":[{"pane_id":"w1:p1","cwd":"/home/ann/garden","focused":true}]}})");
        write(dir.filePath("tab-create.out"), R"({"result":{"type":"tab_created","root_pane":{"pane_id":"w1:p7"}}})");
        backend.startAgent("w1");
        QCOMPARE(backend.busy(), QString("start"));
        QTRY_VERIFY(log(dir).contains("agent start garden-"));
        QVERIFY(log(dir).contains("tab create --workspace w1 --label codex --no-focus --cwd /home/ann/garden\n"));
        QVERIFY(log(dir).contains("--kind codex --pane w1:p7\n"));
        // The new agent is selected once herdr lists it.
        write(dir.filePath("agents.json"), agentsJson().replace("\"pane_id\":\"w3:p2\"", "\"pane_id\":\"w1:p7\""));
        QTRY_COMPARE(backend.selected(), QString("w1:p7"));
        QCOMPARE(backend.busy(), QString());
        QVERIFY(backend.notice().startsWith("Started codex in garden"));
    }

    void createsAWorkspaceAndStartsAnAgentInIt()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        backend.setAgentKind("codex");
        QTRY_COMPARE(backend.workspaces().size(), 1);
        write(dir.filePath("workspace-create.out"),
              R"({"result":{"type":"workspace_created","root_pane":{"pane_id":"w9:p1"},"workspace":{"workspace_id":"w9"}}})");
        backend.createWorkspace("orchard");
        QCOMPARE(backend.busy(), QString("start"));
        QTRY_VERIFY(log(dir).contains("--kind codex --pane w9:p1\n"));
        QVERIFY(log(dir).contains("workspace create --label orchard --cwd " + QDir::homePath() + " --no-focus\n"));
        QVERIFY(log(dir).contains("agent start orchard-"));
        // The workspace's own first pane takes the agent; no extra tab.
        QVERIFY(!log(dir).contains("tab create"));
    }

    void createsAWorkspaceInTheFolderPicked()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QSignalSpy opened(&backend, &Backend::folderPickerOpened);
        QSignalSpy closed(&backend, &Backend::folderPickerClosed);
        QString title;
        backend.setFolderChooser([&](const QString &, const QString &t, auto done) {
            title = t;
            QCOMPARE(backend.busy(), QString("folder"));
            done("/srv/orchard", {});
        });
        backend.chooseFolderAndCreate("Orchard");
        QCOMPARE(opened.count(), 1);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(0).toBool(), true);
        QCOMPARE(title, QString("Folder for Orchard"));
        QTRY_VERIFY(log(dir).contains("workspace create --label Orchard --cwd /srv/orchard --no-focus\n"));
    }

    void makesTheFolderATypedPathNames()
    {
        QTemporaryDir dir, home;
        QVERIFY(QDir(home.path()).mkpath("Work"));
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        backend.setHome(home.path());
        backend.createWorkspace("Work/meadow");
        QVERIFY(QFileInfo(home.filePath("Work/meadow")).isDir());
        QTRY_VERIFY(log(dir).contains("workspace create --label meadow --cwd " + home.filePath("Work/meadow") + " --no-focus\n"));
    }

    void saysWhenTheFolderPickerFails()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QSignalSpy closed(&backend, &Backend::folderPickerClosed);
        backend.setFolderChooser([](const QString &, const QString &, auto done) { done({}, "no portal"); });
        backend.chooseFolderAndCreate("orchard");
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(0).toBool(), false);
        QCOMPARE(closed.first().at(1).toString(), QString("orchard"));
        QCOMPARE(backend.busy(), QString());
        QVERIFY(backend.noticeIsError());
        QCOMPARE(backend.notice(), QString("Couldn't open the folder picker: no portal"));
        QTest::qWait(100);
        QVERIFY(!log(dir).contains("workspace create"));
    }

    void saysWhenAWorkspaceCantBeCreated()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        write(dir.filePath("workspace-create.fail"), R"({"error":{"code":"bad_cwd","message":"no such folder"}})");
        backend.createWorkspace("orchard");
        QTRY_COMPARE(backend.busy(), QString());
        QVERIFY(backend.noticeIsError());
        QCOMPARE(backend.notice(), QString("Couldn't create orchard: no such folder"));
        QVERIFY(!log(dir).contains("agent start"));
    }

    void resolvesWhereANewWorkspaceLives()
    {
        QTemporaryDir home;
        QVERIFY(QDir(home.path()).mkpath("Work/orchard"));
        const auto resolve = [&](const QString &text) {
            const auto map = Backend::resolveNewWorkspace(text, home.path());
            return map.value("label").toString() + " | " + map.value("folder").toString();
        };
        QVERIFY(Backend::resolveNewWorkspace("  ", home.path()).isEmpty());
        // A plain name lives in the home folder, even when a folder has it.
        QCOMPARE(resolve(" orchard "), QString("orchard | ~"));
        QCOMPARE(resolve("My Plans"), QString("My Plans | ~"));
        // A path to a folder is used, and names the workspace.
        QCOMPARE(resolve("~/Work/orchard/"), QString("orchard | ~/Work/orchard"));
        QCOMPARE(resolve("Work/orchard"), QString("orchard | ~/Work/orchard"));
        QCOMPARE(resolve(home.path() + "/Work"), QString("Work | ~/Work"));
        QCOMPARE(Backend::resolveNewWorkspace(home.path() + "/Work", home.path()).value("cwd").toString(),
                 home.path() + "/Work");
        // A path to a new folder in an existing one is made there.
        QCOMPARE(resolve("~/Work/meadow"), QString("meadow | ~/Work/meadow"));
        QVERIFY(Backend::resolveNewWorkspace("Work/meadow", home.path()).value("makeFolder").toBool());
        QVERIFY(!Backend::resolveNewWorkspace("Work/orchard", home.path()).value("makeFolder").toBool());
        QVERIFY(!Backend::resolveNewWorkspace("meadow", home.path()).value("makeFolder").toBool());
        // Deeper than one new folder, or a plain name: the folder picker decides.
        QCOMPARE(resolve("~/Nope/meadow"), QString("meadow | ~"));
        QVERIFY(!Backend::resolveNewWorkspace("~/Nope/meadow", home.path()).value("makeFolder").toBool());
    }

    void closesAWorkspace()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.workspaces().size(), 1);
        backend.closeWorkspace("w1");
        QCOMPARE(backend.busy(), QString("close"));
        QTRY_COMPARE(backend.busy(), QString());
        QVERIFY(log(dir).contains("workspace close w1\n"));
        QVERIFY(!backend.noticeIsError());
        QCOMPARE(backend.notice(), QString("Closed garden."));
    }

    void readsTheCloseAction()
    {
        QTemporaryDir dir;
        QCOMPARE(Backend::readCloseAction(dir.filePath("config")), QString());
        write(dir.filePath("config"), "agent = codex\nclose_action = petrzpav.persistent-spaces.delete\n");
        QCOMPARE(Backend::readCloseAction(dir.filePath("config")), QString("petrzpav.persistent-spaces.delete"));
        write(dir.filePath("config"), "close_action = delete\n"); // no plugin id
        QCOMPARE(Backend::readCloseAction(dir.filePath("config")), QString());
    }

    void closesAWorkspaceThroughAPluginAction()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.workspaces().size(), 1);
        QLocalServer server;
        QVERIFY(server.listen(dir.filePath("herdr.sock")));
        QJsonObject request;
        QByteArray answer = R"({"id":"drover:close","result":{"type":"plugin_action_invoked"}})";
        connect(&server, &QLocalServer::newConnection, this, [&] {
            auto *client = server.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, client, [&, client] {
                if (!client->canReadLine())
                    return;
                request = QJsonDocument::fromJson(client->readLine()).object();
                client->write(answer + "\n");
                client->flush();
            });
        });
        backend.setSocketPath(dir.filePath("herdr.sock"));
        backend.setCloseAction("petrzpav.persistent-spaces.delete");

        backend.closeWorkspace("w1");
        QCOMPARE(backend.busy(), QString("close"));
        QTRY_COMPARE(backend.busy(), QString());
        QCOMPARE(request.value("method").toString(), QString("plugin.action.invoke"));
        const auto params = request.value("params").toObject();
        QCOMPARE(params.value("plugin_id").toString(), QString("petrzpav.persistent-spaces"));
        QCOMPARE(params.value("action_id").toString(), QString("delete"));
        QCOMPARE(params.value("context").toObject().value("workspace_id").toString(), QString("w1"));
        QCOMPARE(backend.notice(), QString("Closed garden."));
        QVERIFY(!log(dir).contains("workspace close"));

        answer = R"({"id":"drover:close","error":{"code":"plugin_action_not_found","message":"no such action"}})";
        backend.closeWorkspace("w1");
        QTRY_COMPARE(backend.busy(), QString());
        QVERIFY(backend.noticeIsError());
        QCOMPARE(backend.notice(), QString("Couldn't close garden: no such action"));

        server.close();
        backend.closeWorkspace("w1");
        QTRY_COMPARE(backend.busy(), QString());
        QVERIFY(backend.noticeIsError());
        QVERIFY(backend.notice().startsWith("Couldn't close garden: herdr's socket:"));
    }

    void saysWhenAWorkspaceCantBeClosed()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.workspaces().size(), 1);
        write(dir.filePath("workspace-close.fail"),
              R"({"error":{"code":"workspace_not_found","message":"workspace w1 not found"},"id":"cli:workspace:close"})");
        backend.closeWorkspace("w1");
        QTRY_COMPARE(backend.busy(), QString());
        QVERIFY(backend.noticeIsError());
        QCOMPARE(backend.notice(), QString("Couldn't close garden: workspace w1 not found"));
    }

    void namesTheTabAfterTheFirstQuestion()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        backend.setAgentKind("claude");
        QTRY_COMPARE(backend.workspaces().size(), 1);
        write(dir.filePath("pane-list.out"), R"({"result":{"panes":[]}})");
        write(dir.filePath("tab-create.out"), R"({"result":{"root_pane":{"pane_id":"w1:p7"}}})");
        backend.startAgent("w1");
        QTRY_COMPARE(backend.busy(), QString());
        const QString name = log(dir).section("agent start ", 1).section(' ', 0, 0);
        QVERIFY(name.startsWith("garden-"));

        const auto agentsWith = [&](const QString &title) {
            return QString(R"({"result":{"agents":[{"agent":"claude","agent_status":"idle","cwd":"/g","pane_id":"w1:p7",
                "tab_id":"w1:t2","name":"%1","terminal_id":"term_7","terminal_title_stripped":"%2","workspace_id":"w1"}]}})")
                .arg(name, title).toUtf8();
        };
        // Before the first question the title only says which agent it is.
        write(dir.filePath("agents.json"), agentsWith("Claude Code"));
        QTest::qWait(1500);
        QVERIFY(!log(dir).contains("rename"));

        write(dir.filePath("agents.json"), agentsWith("Fix the fence by the old apple tree before the spring rains"));
        QTRY_VERIFY(log(dir).contains("agent rename w1:p7 --clear\n"));
        QVERIFY(log(dir).contains(QString("tab rename w1:t2 Fix the fence by the old apple tree bef") + QChar(0x2026) + "\n"));
        // Only once.
        QTest::qWait(1500);
        QCOMPARE(log(dir).count("tab rename"), 1);
    }

    void tellsMeaningfulTitles()
    {
        QVERIFY(Backend::meaningfulTitle("Fix the gate", "claude"));
        QVERIFY(!Backend::meaningfulTitle("Claude Code", "claude"));
        QVERIFY(!Backend::meaningfulTitle("codex", "codex"));
        QVERIFY(!Backend::meaningfulTitle("  ", "claude"));
        QVERIFY(!Backend::meaningfulTitle("~/Work/drover", "claude"));
        QVERIFY(!Backend::meaningfulTitle("ann@garden: /srv", "claude"));
        QCOMPARE(Backend::tabLabel("  Fix   the gate "), QString("Fix the gate"));
        QCOMPARE(Backend::tabLabel(QString(45, 'a')).size(), 40);
    }

    void namesAgentsTheWayHerdrWants()
    {
        QCOMPARE(Backend::agentName("Ak Beránek!", "x1"), QString("ak-ber-nek-x1"));
        QCOMPARE(Backend::agentName("42", "x1"), QString("agent-42-x1"));
        QVERIFY(Backend::agentName(QString(60, 'a'), "zz").size() <= 32);
    }

    void readsTheAgentKind()
    {
        QTemporaryDir dir;
        QCOMPARE(Backend::readAgentKind(dir.filePath("config")), QString("claude"));
        write(dir.filePath("config"), "# drover\nagent = codex\n");
        QCOMPARE(Backend::readAgentKind(dir.filePath("config")), QString("codex"));
    }

    void themeGivesTheTerminalPalette()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "red = \"#ff6188\"\nbright_cyan = \"#95e5ee\"\n");
        Theme theme(dir.path());
        const QStringList palette = theme.terminalPalette();
        QCOMPARE(palette.size(), 16);
        QCOMPARE(palette.at(1), QString("#ff6188"));
        QCOMPARE(palette.at(14), QString("#95e5ee"));
        QCOMPARE(palette.at(2), QString("#0dbc79"));
    }

    void terminalShowsWhatTheProgramPrints()
    {
        TerminalView view;
        view.setSize({800, 400});
        view.setColors(Ansi16());
        view.setForeground(QColor("#fcfcfa"));
        view.setProgram("/bin/sh");
        view.setArguments({"-c", "printf 'hello \\033[31mred\\033[0m \\033[38;2;1;2;3mrgb\\033[0m čaj\\n'; sleep 5"});
        view.start();
        QVERIFY(view.running());
        QTRY_COMPARE(view.screenText(), QString("hello red rgb čaj"));
        QCOMPARE(view.textColorAt(0, 0), QColor("#fcfcfa"));
        QCOMPARE(view.textColorAt(0, 6), QColor("#ff6188"));
        QCOMPARE(view.textColorAt(0, 10), QColor(1, 2, 3));
    }

    void terminalSendsKeysAsATerminalDoes()
    {
        QTemporaryDir dir;
        TerminalView view;
        view.setSize({800, 400});
        view.setProgram("/bin/sh");
        view.setArguments({"-c", QString("stty raw -echo; while b=$(dd bs=1 count=1 2>/dev/null | od -An -tx1); [ -n \"$b\" ]; do echo $b >> '%1'; done").arg(dir.filePath("keys"))});
        view.start();
        QTest::qWait(300);
        auto press = [&](int key, Qt::KeyboardModifiers mods, const QString &text = {}) {
            QKeyEvent event(QEvent::KeyPress, key, mods, text);
            QCoreApplication::sendEvent(&view, &event);
        };
        press(Qt::Key_Backtab, Qt::ShiftModifier);      // 1b 5b 5a
        press(Qt::Key_C, Qt::ControlModifier, "\x03");  // 03
        press(Qt::Key_Escape, Qt::NoModifier, "\x1b");  // 1b
        press(Qt::Key_X, Qt::AltModifier, "x");         // 1b 78
        press(Qt::Key_Return, Qt::NoModifier, "\r");    // 0d
        press(Qt::Key_unknown, Qt::NoModifier, "č");    // c4 8d
        press(Qt::Key_Up, Qt::NoModifier);              // 1b 5b 41
        const QString expected = "1b\n5b\n5a\n03\n1b\n1b\n78\n0d\nc4\n8d\n1b\n5b\n41\n";
        auto keys = [&] {
            QFile file(dir.filePath("keys"));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        QTRY_COMPARE(keys(), expected);
    }

    void terminalHandsTheWheelToAProgramThatWantsTheMouse()
    {
        QTemporaryDir dir;
        TerminalView view;
        view.setSize({800, 400});
        view.setProgram("/bin/sh");
        // Like herdr's attach: ask for clicks in SGR encoding, then log every byte.
        view.setArguments({"-c", QString("printf '\\033[?1000h\\033[?1006h'; stty raw -echo; "
                                         "while b=$(dd bs=1 count=1 2>/dev/null); [ -n \"$b\" ]; do printf %s \"$b\" >> '%1'; done")
                                     .arg(dir.filePath("mouse"))});
        view.start();
        QTest::qWait(300);
        const QPointF at(3 * 8 + 1, 2 * 16 + 1);  // inside cell row 2, column 3 for an 8x16 cell or bigger
        QWheelEvent up(at, at, {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &up);
        QWheelEvent down(at, at, {}, {0, -120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &down);
        auto bytes = [&] {
            QFile file(dir.filePath("mouse"));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        // Wheel up is button 64 and wheel down 65 in SGR mouse reports.
        QTRY_VERIFY(QRegularExpression("^\\x1b\\[<64;\\d+;\\d+M\\x1b\\[<65;\\d+;\\d+M$").match(bytes()).hasMatch());
        // Nothing went into drover's own scrollback view instead.
        QVERIFY(view.screenText().isEmpty());
    }

    void terminalKeepsReservedShortcuts()
    {
        TerminalView view;
        view.setReservedShortcuts({"Alt+Up", "Ctrl+Shift+O"});
        auto overridden = [&](int key, Qt::KeyboardModifiers mods) {
            QKeyEvent event(QEvent::ShortcutOverride, key, mods);
            event.ignore();
            QCoreApplication::sendEvent(&view, &event);
            return event.isAccepted();
        };
        QVERIFY(overridden(Qt::Key_Escape, Qt::NoModifier));
        QVERIFY(overridden(Qt::Key_O, Qt::ControlModifier));
        QVERIFY(!overridden(Qt::Key_Up, Qt::AltModifier));
        QVERIFY(!overridden(Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier));
    }

    void terminalTellsTheProgramItsSize()
    {
        TerminalView view;
        view.setSize({800, 400});
        view.setProgram("/bin/sh");
        view.setArguments({"-c", "trap 'clear; stty size' WINCH; stty size; while :; do sleep 0.05; done"});
        view.start();
        QTRY_COMPARE(view.screenText(), QString("%1 %2").arg(view.rows()).arg(view.columns()));
        const int columns = view.columns();
        view.setSize({400, 200});
        QVERIFY(view.columns() < columns);
        QTRY_COMPARE(view.screenText(), QString("%1 %2").arg(view.rows()).arg(view.columns()));
    }

    void terminalSaysWhenTheProgramEnds()
    {
        TerminalView view;
        view.setSize({400, 200});
        view.setProgram("/bin/true");
        QSignalSpy finished(&view, &TerminalView::finished);
        view.start();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!view.running());
    }

    void saysWhenHerdrIsMissing()
    {
        QTemporaryDir dir;
        Backend backend(dir.filePath("herdr"));
        QTRY_VERIFY(!backend.error().isEmpty());
        QCOMPARE(backend.agents()->count(), 0);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\nbackground = \"#2d2a2e\"\nred = \"#ff6188\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#2d2a2e"));
        QCOMPARE(theme.urgent(), QString("#ff6188"));
        QCOMPARE(theme.foreground(), QString("#cacccc"));
        QCOMPARE(theme.border(), QString("#112233"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static QStringList Ansi16()
    {
        return {"#000000", "#ff6188", "#a9dc76", "#ffd866", "#fc9867", "#ab9df2", "#78dce8", "#c1c0c0",
                "#727072", "#ff7a9b", "#bce592", "#ffe28a", "#fdad86", "#c0b5f5", "#95e5ee", "#fcfcfa"};
    }

    static QByteArray agentsJson()
    {
        return R"({"id":"cli:agent:list","result":{"type":"agent_list","agents":[
          {"agent":"claude","agent_status":"working","cwd":"/home/ann/garden","foreground_cwd":"/home/ann/garden",
           "pane_id":"w1:p1","terminal_id":"term_1","terminal_title_stripped":"Plant the tomatoes","workspace_id":"w1"},
          {"agent":"claude","agent_status":"blocked","cwd":"/home/ann","name":"reviewer",
           "pane_id":"w2:p1","terminal_id":"term_2","terminal_title_stripped":"Review the fence","workspace_id":"w2"},
          {"agent":"codex","agent_status":"idle","cwd":"/srv/shed",
           "pane_id":"w3:p2","terminal_id":"term_3","terminal_title_stripped":"","workspace_id":"w3"}]}})";
    }

    static QString fakeHerdr(QTemporaryDir &dir)
    {
        write(dir.filePath("agents.json"), agentsJson());
        write(dir.filePath("workspaces.json"),
              R"({"result":{"type":"workspace_list","workspaces":[{"workspace_id":"w1","label":"garden"}]}})");
        const QString script = dir.filePath("herdr");
        write(script, QString(R"sh(#!/bin/sh
echo "$@" >> '%1/log'
key="$1-$2"
[ -e "%1/$key.sleep" ] && sleep "$(cat "%1/$key.sleep")"
[ -e "%1/$key.fail" ] && { cat "%1/$key.fail" >&2; exit 1; }
[ -e "%1/$key.out" ] && { cat "%1/$key.out"; exit 0; }
case "$1 $2" in
  "workspace list") cat '%1/workspaces.json' ;;
  "agent list") cat '%1/agents.json' ;;
  "agent read")
    case "$*" in *"source recent"*) [ -e '%1/recent.fail' ] && { cat '%1/recent.fail' >&2; exit 1; } ;; esac
    printf 'line one\n─────\n\n\nline two\n' ;;
  *) echo '{"result":{"type":"ok"}}' ;;
esac
)sh").arg(dir.path()).toUtf8());
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return script;
    }

    static QString log(QTemporaryDir &dir)
    {
        QFile file(dir.filePath("log"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(DroverTests)
#include "drover_tests.moc"
