#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

#include "agentmodel.h"
#include "ansi.h"
#include "backend.h"
#include "terminalfont.h"
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

    void cleansTheScreen()
    {
        const QString screen = "\n\nfirst   \n────────────\n\n\nsecond\n╭──╮\n❯ \n\n";
        QCOMPARE(Backend::cleanOutput(screen), QString("first\n\nsecond\n\n❯"));
        QCOMPARE(Backend::cleanOutput("a\nb\nc", 2), QString("b\nc"));
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

        // The first agent is picked; its screen is read while the panel is open.
        QCOMPARE(backend.selected(), QString("w1:p1"));
        backend.setActive(true);
        QTRY_COMPARE(backend.output(), QString("line one\n\nline two"));
        // A working agent has no history to scroll; its visible screen is read.
        QVERIFY(log(dir).contains("agent read w1:p1 --source visible --format ansi\n"));

        backend.selectNext(1);
        QCOMPARE(backend.selectedStatus(), QString("blocked"));
        backend.selectNext(5);
        QCOMPARE(backend.selected(), QString("w3:p2"));

        backend.prompt("  water them  ");
        QTRY_VERIFY(backend.notice().startsWith("Sent to"));
        QVERIFY(log(dir).contains("agent prompt w3:p2 water them\n"));
        backend.sendKeys({"1"});
        QTRY_VERIFY(log(dir).contains("agent send-keys w3:p2 1\n"));
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
        // Later polls don't pick another agent on their own, and a draft has
        // nowhere to go.
        QTest::qWait(1200);
        QVERIFY(backend.selected().isEmpty());
        backend.prompt("water");
        QTest::qWait(200);
        QVERIFY(!log(dir).contains("agent prompt"));
        backend.select("w1:p1");
        QVERIFY(!backend.selectionLost());
    }

    void reportsARejectedPrompt()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        write(dir.filePath("agent-prompt.fail"), R"({"error":{"code":"agent_blocked","message":"agent is blocked"}})");
        QSignalSpy sent(&backend, &Backend::promptSent);
        backend.prompt("water");
        QTRY_VERIFY(backend.notice().startsWith("Not sent"));
        QVERIFY(backend.noticeIsError());
        QCOMPARE(sent.count(), 0);
    }

    void sendsOneThingAtATime()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        write(dir.filePath("agent-prompt.sleep"), "0.4");
        QSignalSpy sent(&backend, &Backend::promptSent);
        backend.prompt("water");
        QCOMPARE(backend.busy(), QString("send"));
        backend.prompt("water");
        backend.sendKeys({"1"});
        backend.jump();
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.first().first().toString(), QString("water"));
        QCOMPARE(backend.busy(), QString());
        QCOMPARE(log(dir).count("agent prompt"), 1);
        QVERIFY(!log(dir).contains("send-keys"));
        QVERIFY(!log(dir).contains("agent focus"));
    }

    void keepsADashMessageAMessage()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        backend.prompt("--help me");
        QTRY_VERIFY(log(dir).contains("agent prompt w1:p1  --help me\n"));
    }

    void saysWhenACommandHangs()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.selected(), QString("w1:p1"));
        backend.setTimeout(300);
        write(dir.filePath("agent-prompt.sleep"), "2");
        backend.prompt("water");
        QTRY_VERIFY(backend.notice().contains("no answer"));
        QCOMPARE(backend.busy(), QString());
    }

    void readsTheVisibleScreenWhenHistoryIsBusy()
    {
        QTemporaryDir dir;
        Backend backend(fakeHerdr(dir), dir.filePath("no-hyprctl"));
        QTRY_COMPARE(backend.agents()->count(), 3);
        backend.select("w3:p2");  // idle, so its history is read first
        write(dir.filePath("recent.fail"), R"({"error":{"code":"agent_not_idle","message":"working"}})");
        backend.setActive(true);
        QTRY_COMPARE(backend.output(), QString("line one\n\nline two"));
        QVERIFY(log(dir).contains("agent read w3:p2 --source recent --format ansi --lines 400\n"));
        QVERIFY(log(dir).contains("agent read w3:p2 --source visible --format ansi\n"));
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

    void keepsTerminalColors()
    {
        const QStringList palette = Ansi::defaultPalette();
        const QString screen = "\x1b[0m\x1b[38;2;255;193;7mauto <on>\x1b[0m plain \x1b[1;31mbold red\x1b[0m\r";
        QCOMPARE(Ansi::strip(screen), QString("auto <on> plain bold red"));
        const QString html = Ansi::toHtml(screen, palette, "Mono");
        QVERIFY(html.contains("<span style=\"color:#ffc107\">auto &lt;on&gt;</span> plain "));
        QVERIFY(html.contains("<span style=\"color:" + palette.at(1) + ";font-weight:bold\">bold red</span>"));
        QVERIFY(html.startsWith("<pre style=\"margin:0; font-family:'Mono'\">"));
        // Rules drawn in color are still dropped as drawing.
        QCOMPARE(Backend::cleanOutput("a\n\x1b[38;2;1;2;3m────\x1b[0m\nb   \x1b[0m"), QString("a\n\nb"));
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
        QCOMPARE(palette.at(2), Ansi::defaultPalette().at(2));
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
    static QByteArray agentsJson()
    {
        return R"({"id":"cli:agent:list","result":{"type":"agent_list","agents":[
          {"agent":"claude","agent_status":"working","cwd":"/home/ann/garden","foreground_cwd":"/home/ann/garden",
           "pane_id":"w1:p1","terminal_title_stripped":"Plant the tomatoes","workspace_id":"w1"},
          {"agent":"claude","agent_status":"blocked","cwd":"/home/ann","name":"reviewer",
           "pane_id":"w2:p1","terminal_title_stripped":"Review the fence","workspace_id":"w2"},
          {"agent":"codex","agent_status":"idle","cwd":"/srv/shed",
           "pane_id":"w3:p2","terminal_title_stripped":"","workspace_id":"w3"}]}})";
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
