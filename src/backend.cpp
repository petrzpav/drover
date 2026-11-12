#include "backend.h"


#include <QDateTime>
#include <algorithm>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDir>
#include <QUrl>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStandardPaths>

Backend::Backend(const QString &herdr, const QString &hyprctl, QObject *parent)
    : QObject(parent), m_herdrOverride(herdr), m_hyprctlOverride(hyprctl), m_agentKind("claude") {
    // A fake herdr (the tests) gets no real config and no real herdr socket,
    // so nothing it does can reach the user's session.
    if (herdr.isEmpty()) {
        const QString config = QDir::homePath() + "/.config/drover/config";
        m_agentKind = readAgentKind(config);
        m_indicator = readIndicator(config);
        m_editor = readEditor(config);
        m_closeAction = readCloseAction(config);
        m_socketPath = qEnvironmentVariable("HERDR_SOCKET_PATH", QDir::homePath() + "/.config/herdr/herdr.sock");
    }
    // A listing takes a few milliseconds, so a one-second poll is cheap and far
    // simpler than herdr's per-pane event subscriptions.
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &Backend::refresh);
    m_poll.start();
    m_sheepClock.setInterval(50);
    m_eventRefresh.setSingleShot(true);
    m_eventRefresh.setInterval(15);
    connect(&m_eventRefresh, &QTimer::timeout, this, &Backend::refresh);
    m_vanishCheck.setSingleShot(true);
    m_vanishCheck.setInterval(3000);
    connect(&m_vanishCheck, &QTimer::timeout, this, &Backend::refresh);

    // Another agent, or none: the panel shows the agent again.
    connect(this, &Backend::selectedChanged, this, [this] {
        if (m_editing || !m_editorTerminal.isEmpty()) {
            m_editing = false;
            m_editorPane.clear();
            m_editorTerminal.clear();
            emit editorChanged();
        }
        shellClosed();
    });

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(5000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { setNotice({}, false); });

    refresh();
}

// Looked up on every use, so installing herdr or Hyprland later needs no restart.
QString Backend::herdr() const {
    return m_herdrOverride.isEmpty() ? QStandardPaths::findExecutable("herdr") : m_herdrOverride;
}

QString Backend::hyprctl() const {
    return m_hyprctlOverride.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : m_hyprctlOverride;
}

QString Backend::selectedTitle() const {
    const int row = selectedIndex();
    return row < 0 ? QString() : m_model.agents().at(row).title;
}

QString Backend::selectedTerminal() const {
    const int row = selectedIndex();
    return row < 0 ? QString() : m_model.agents().at(row).terminalId;
}

QString Backend::selectedStatus() const {
    const int row = selectedIndex();
    return row < 0 ? QString() : m_model.agents().at(row).status;
}

void Backend::run(const QString &program, const QStringList &arguments, Done done, int timeout) {
    auto *process = new QProcess(this);
    // herdr talks to the session of the pane it runs in; drover may be started
    // from inside a herdr pane, and must still see the whole local session.
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &name : {"HERDR_ENV", "HERDR_PANE_ID", "HERDR_TAB_ID", "HERDR_WORKSPACE_ID"})
        environment.remove(name);
    process->setProcessEnvironment(environment);

    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process] {
        process->setProperty("timedOut", true);
        process->kill();
    });
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        if (process->property("timedOut").toBool())
            done(TimedOut, {}, {});
        else
            done(status == QProcess::NormalExit ? code : -1, process->readAllStandardOutput(),
                 process->readAllStandardError());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done(-1, {}, process->errorString().toUtf8());
        process->deleteLater();
    });
    process->start(program, arguments);
    timer->start(timeout > 0 ? timeout : m_timeout);
}

// herdr reports failures as {"error": {"code", "message"}} on stdout or
// stderr, depending on the command.
static QJsonObject errorObject(const QByteArray &out, const QByteArray &err) {
    for (const auto &stream : {out, err}) {
        const auto error = QJsonDocument::fromJson(stream.trimmed()).object().value("error").toObject();
        if (!error.isEmpty())
            return error;
    }
    return {};
}

QString Backend::errorCode(const QByteArray &out, const QByteArray &err) {
    return errorObject(out, err).value("code").toString();
}

QString Backend::commandError(const QByteArray &out, const QByteArray &err, int code) {
    if (code == TimedOut)
        return tr("no answer within a few seconds.");
    const auto error = errorObject(out, err);
    if (!error.isEmpty())
        return error.value("message").toString(error.value("code").toString());
    if (code == 0)
        return {};
    const QString message = QString::fromUtf8(err).trimmed().section('\n', 0, 0);
    return message.isEmpty() ? tr("exited with code %1.").arg(code) : message;
}

void Backend::refresh() {
    const QString program = herdr();
    if (program.isEmpty()) {
        setError(tr("herdr is not installed."));
        return;
    }
    // One listing at a time; a change meanwhile asks for another after it.
    if (m_listing) {
        m_listAgain = true;
        return;
    }
    m_listing = true;
    m_listAgain = false;

    // Workspace labels first, so each agent says which workspace it lives in.
    run(program, {"workspace", "list"}, [this, program](int code, const QByteArray &out, const QByteArray &) {
        if (code == 0)
            m_workspaces = parseWorkspaces(out);
        run(program, {"agent", "list"}, [this](int code, const QByteArray &out, const QByteArray &err) {
            m_listing = false;
            if (m_listAgain)
                QTimer::singleShot(0, this, &Backend::refresh);
            QString error = commandError(out, err, code);
            QList<Agent> agents;
            if (error.isEmpty())
                agents = parseAgents(out, m_workspaces, QDir::homePath(), &error);
            // A failed poll keeps the last list and selection and only says
            // so; a one-off hiccup must not clear the panel or move a draft
            // to another agent.
            if (!error.isEmpty()) {
                setError(tr("herdr: %1").arg(error));
                return;
            }
            setError({});
            applyAgents(agents);
            updateWorkspaceList();
            nameStartedAgents(agents);
        });
    });
}

void Backend::setAsking(const QSet<QString> &panes) {
    if (panes == m_asking)
        return;
    m_asking = panes;
    applyAgents(m_listed);
}

void Backend::applyAgents(const QList<Agent> &listedAgents) {
    // An agent whose question waits in drover asks you, whatever herdr sees
    // (Claude shows no dialog while drover holds the question).
    m_listed = listedAgents;
    QList<Agent> agents = listedAgents;
    for (auto &agent : agents)
        if (m_asking.contains(agent.paneId))
            agent.status = QStringLiteral("blocked");
    emit listed(listedAgents);
    const QString before = selectedTitle() + '\n' + selectedStatus() + '\n' + QString::number(selectedIndex());
    // An agent counts as ended once it has been missing for three seconds,
    // so a moment herdr doesn't see it never closes its editor.
    const auto listed = [&](const QString &paneId) {
        return std::any_of(agents.begin(), agents.end(), [&](const Agent &a) { return a.paneId == paneId; });
    };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &agent : m_model.agents()) {
        if (!listed(agent.paneId))
            m_vanished.insert(agent.paneId, {agent, now});
    }
    QList<Agent> gone;
    for (auto it = m_vanished.begin(); it != m_vanished.end();) {
        if (listed(it.key())) {
            it = m_vanished.erase(it);
        } else if (now - it->second >= 2900) {
            gone << it->first;
            it = m_vanished.erase(it);
        } else {
            ++it;
        }
    }
    m_model.setAgents(agents);
    if (!gone.isEmpty())
        closeEditorsOf(gone);
    // Missing agents are looked at again once their time is up.
    if (!m_vanished.isEmpty() && !m_vanishCheck.isActive())
        m_vanishCheck.start();
    watchEvents();

    QHash<QString, int> counts;
    for (const auto &agent : agents)
        ++counts[agent.status];
    if (counts != m_counts) {
        m_counts = counts;
        emit countsChanged();
    }

    if (!m_selected.isEmpty() && m_model.indexOf(m_selected) < 0) {
        // The agent's pane closed. Select nothing rather than another agent,
        // which would receive whatever is in the message box.
        m_selected.clear();
        m_selectionLost = true;
        emit selectedChanged();
        emit selectedInfoChanged();
        return;
    }
    // An agent the picker just started is selected as soon as it shows up.
    if (!m_pendingSelect.isEmpty() && m_model.indexOf(m_pendingSelect) >= 0) {
        select(m_pendingSelect);
        m_pendingSelect.clear();
        return;
    }
    // At start, the first agent is picked for you.
    if (m_selected.isEmpty() && !m_selectionLost && !agents.isEmpty()) {
        select(agents.first().paneId);
        return;
    }
    if (before != selectedTitle() + '\n' + selectedStatus() + '\n' + QString::number(selectedIndex()))
        emit selectedInfoChanged();
}

void Backend::select(const QString &paneId) {
    if (paneId == m_selected)
        return;
    m_selected = paneId;
    m_selectionLost = false;
    emit selectedChanged();
    emit selectedInfoChanged();
}

void Backend::selectNext(int step) {
    const int count = m_model.count();
    if (count == 0)
        return;
    const int row = selectedIndex();
    select(m_model.agents().at(qBound(0, row < 0 ? 0 : row + step, count - 1)).paneId);
}

void Backend::jump() {
    if (!m_busy.isEmpty() || m_selected.isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    setBusy("open");
    run(program, {"agent", "focus", m_selected}, [this](int code, const QByteArray &out, const QByteArray &err) {
        const QString error = commandError(out, err, code);
        if (!error.isEmpty()) {
            setBusy({});
            setNotice(tr("Couldn't open the agent: %1").arg(error), true);
            return;
        }
        raiseHerdr();
    });
}

// herdr now shows the agent; bring its window forward, wherever it is. Any
// failure keeps the panel open and says what went wrong.
void Backend::raiseHerdr() {
    const QString program = hyprctl();
    if (program.isEmpty()) {
        setBusy({});
        setNotice(tr("herdr shows the agent, but hyprctl isn't available to bring its window forward."), true);
        return;
    }
    run(program, {"clients", "-j"}, [this, program](int code, const QByteArray &out, const QByteArray &err) {
        const QString error = commandError(out, err, code);
        if (!error.isEmpty()) {
            setBusy({});
            setNotice(tr("Couldn't find the herdr window: hyprctl %1").arg(error), true);
            return;
        }
        QString address;
        for (const auto &value : QJsonDocument::fromJson(out).array()) {
            const auto client = value.toObject();
            if (client.value("class").toString().contains("herdr", Qt::CaseInsensitive)) {
                address = client.value("address").toString();
                break;
            }
        }
        if (address.isEmpty()) {
            setBusy({});
            setNotice(tr("No herdr window is open."), true);
            return;
        }
        run(program, {"dispatch", "focuswindow", "address:" + address},
            [this](int code, const QByteArray &out, const QByteArray &err) {
                setBusy({});
                const QString error = commandError(out, err, code);
                // hyprctl dispatch prints "ok" on success and the reason otherwise.
                const QString reply = QString::fromUtf8(out).trimmed();
                if (!error.isEmpty() || (!reply.isEmpty() && reply != "ok")) {
                    setNotice(tr("Couldn't bring herdr forward: %1").arg(error.isEmpty() ? reply : error), true);
                    return;
                }
                emit jumped();
            });
    });
}

void Backend::updateWorkspaceList() {
    QHash<QString, int> agents;
    for (const auto &agent : m_model.agents())
        ++agents[agent.workspaceId];
    QList<QPair<QString, QString>> sorted;
    for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it)
        sorted.append({it.value(), it.key()});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0;
    });
    QVariantList list;
    for (const auto &[label, id] : sorted)
        list.append(QVariantMap{{"id", id}, {"label", label}, {"agents", agents.value(id)}});
    if (list != m_workspaceList) {
        m_workspaceList = list;
        emit workspacesChanged();
    }
}

QString Backend::readAgentKind(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*agent\s*=\s*"?([a-z0-9_-]+)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return QStringLiteral("claude");
}

QString Backend::readIndicator(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*indicator\s*=\s*"?(dots|bars|sheep)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return QStringLiteral("bars");
}

void Backend::setIndicator(const QString &style) {
    const QString known = style == "dots" || style == "sheep" ? style : QStringLiteral("bars");
    if (known == m_indicator)
        return;
    m_indicator = known;
    emit indicatorChanged();
}

QString Backend::readEditor(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*editor\s*=\s*"?([A-Za-z0-9_.-]+)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return QStringLiteral("fresh");
}

void Backend::editorClosed() {
    if (!m_editing && m_editorTerminal.isEmpty())
        return;
    m_editing = false;
    m_editorPane.clear();
    m_editorTerminal.clear();
    emit editorChanged();
    // Its terminal stays in herdr; it is found again with the next editor.
    shellClosed();
}

void Backend::shellClosed() {
    if (!m_shellShown && m_shellTerminal.isEmpty())
        return;
    m_shellShown = false;
    m_shellPane.clear();
    m_shellTerminal.clear();
    emit shellChanged();
}

// The pane labelled `label` in the selected agent's tab, looked up each time,
// so it survives a drover restart and is gone once it closes. A tab without
// one gets it: `from` (the agent's pane, or the editor's) is split in the
// agent's folder, and `command` runs in the new pane. `found` gets its pane
// and terminal, unless another agent was picked meanwhile.
void Backend::openBeside(const QString &label, const QString &from, const QString &direction, const QString &command,
                         const std::function<void(const QString &, const QString &)> &found) {
    const int row = selectedIndex();
    if (row < 0 || !m_busy.isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    const Agent agent = m_model.agents().at(row);
    const auto failed = [this, label](const QString &error) {
        setBusy({});
        setNotice((label == "editor" ? tr("Couldn't open the editor: %1") : tr("Couldn't open the terminal: %1")).arg(error), true);
    };
    const auto done = [this, agent, found](const QString &paneId, const QString &terminalId) {
        setBusy({});
        if (m_selected == agent.paneId)
            found(paneId, terminalId);
    };
    setBusy(label);
    run(program, {"pane", "list"}, [=, this](int code, const QByteArray &out, const QByteArray &err) {
        if (m_selected != agent.paneId) {
            setBusy({});
            return;
        }
        const QString error = commandError(out, err, code);
        if (!error.isEmpty())
            return failed(error);
        const QJsonArray panes = QJsonDocument::fromJson(out).object().value("result").toObject().value("panes").toArray();
        QString tab = agent.tabId, cwd;
        for (const auto &value : panes) {
            const auto pane = value.toObject();
            if (pane.value("pane_id").toString() == agent.paneId) {
                tab = pane.value("tab_id").toString();
                cwd = pane.value("cwd").toString();
            }
        }
        for (const auto &value : panes) {
            const auto pane = value.toObject();
            if (!tab.isEmpty() && pane.value("tab_id").toString() == tab && pane.value("label").toString() == label
                && pane.value("pane_id").toString() != agent.paneId)
                return done(pane.value("pane_id").toString(), pane.value("terminal_id").toString());
        }
        if (cwd.isEmpty())
            cwd = agent.cwd.startsWith('~') ? QDir::homePath() + agent.cwd.mid(1) : agent.cwd;
        run(program, {"pane", "split", from, "--direction", direction, "--cwd", cwd, "--no-focus"},
            [=, this](int code, const QByteArray &out, const QByteArray &err) {
                const QString error = commandError(out, err, code);
                const auto pane = QJsonDocument::fromJson(out).object().value("result").toObject().value("pane").toObject();
                const QString paneId = pane.value("pane_id").toString();
                if (!error.isEmpty() || paneId.isEmpty())
                    return failed(error.isEmpty() ? tr("herdr gave no pane.") : error);
                const QString terminalId = pane.value("terminal_id").toString();
                run(program, {"pane", "rename", paneId, label}, [](int, const QByteArray &, const QByteArray &) {});
                if (command.isEmpty())
                    return done(paneId, terminalId);
                run(program, {"pane", "run", paneId, command},
                    [=, this](int code, const QByteArray &out, const QByteArray &err) {
                        const QString error = commandError(out, err, code);
                        if (!error.isEmpty())
                            return failed(error);
                        done(paneId, terminalId);
                    });
            });
    });
}

// The editor goes right of the agent, exec'd, so its pane closes when it quits.
void Backend::toggleEditor() {
    if (m_editing || !m_editorTerminal.isEmpty()) {
        // Still attached from before: back and forth at once.
        m_editing = !m_editing;
        emit editorChanged();
        return;
    }
    if (m_selected.isEmpty())
        return;
    if (QStandardPaths::findExecutable(m_editor).isEmpty()) {
        setNotice(tr("%1 is not installed.").arg(m_editor), true);
        return;
    }
    openBeside("editor", m_selected, "right", "exec " + m_editor, [this](const QString &pane, const QString &terminal) {
        m_editorPane = pane;
        m_editorTerminal = terminal;
        m_editing = true;
        emit editorChanged();
    });
}

// The terminal goes below the editor, the full width of the editor's side.
void Backend::toggleShell() {
    if (!m_editing)
        return;
    if (m_shellShown || !m_shellTerminal.isEmpty()) {
        m_shellShown = !m_shellShown;
        emit shellChanged();
        return;
    }
    openBeside("shell", m_editorPane, "down", QString(), [this](const QString &pane, const QString &terminal) {
        m_shellPane = pane;
        m_shellTerminal = terminal;
        m_shellShown = true;
        emit shellChanged();
    });
}

void Backend::closeShell() {
    if (m_shellPane.isEmpty())
        return;
    if (const QString program = herdr(); !program.isEmpty())
        run(program, {"pane", "close", m_shellPane}, [](int, const QByteArray &, const QByteArray &) {});
    shellClosed();
}

// An agent that ended takes its editor along: the panes labelled "editor"
// and "shell" in its tab close, and so does the agent's own pane if only a shell is left in
// it (herdr names no agent there). Tabs without an editor stay as they are.
void Backend::closeEditorsOf(const QList<Agent> &gone) {
    const QString program = herdr();
    if (program.isEmpty())
        return;
    run(program, {"pane", "list"}, [this, program, gone](int code, const QByteArray &out, const QByteArray &err) {
        if (!commandError(out, err, code).isEmpty())
            return;
        const QJsonArray panes = QJsonDocument::fromJson(out).object().value("result").toObject().value("panes").toArray();
        for (const auto &agent : gone) {
            QJsonObject own, editor;
            QStringList shells;
            bool otherAgent = false;
            for (const auto &value : panes) {
                const auto pane = value.toObject();
                if (pane.value("pane_id").toString() == agent.paneId)
                    own = pane;
            }
            const QString tab = !own.isEmpty() ? own.value("tab_id").toString() : agent.tabId;
            for (const auto &value : panes) {
                const auto pane = value.toObject();
                if (tab.isEmpty() || pane.value("tab_id").toString() != tab || pane.value("pane_id").toString() == agent.paneId)
                    continue;
                if (!pane.value("agent").toString().isEmpty())
                    otherAgent = true;
                if (pane.value("label").toString() == "editor")
                    editor = pane;
                else if (pane.value("label").toString() == "shell")
                    shells << pane.value("pane_id").toString();
            }
            // Another agent still works in this tab: its editor stays.
            if (editor.isEmpty() || otherAgent)
                continue;
            for (const QString &shell : shells)
                run(program, {"pane", "close", shell}, [](int, const QByteArray &, const QByteArray &) {});
            run(program, {"pane", "close", editor.value("pane_id").toString()}, [](int, const QByteArray &, const QByteArray &) {});
            if (!own.isEmpty() && own.value("agent").toString().isEmpty())
                run(program, {"pane", "close", agent.paneId}, [](int, const QByteArray &, const QByteArray &) {});
        }
    });
}

// One connection to herdr's socket at a time, subscribed to the agents'
// states and to panes, tabs and workspaces coming and going; any event
// lists the agents again at once (bursts make one listing). Another set of
// agents gets a new subscription, which replaces the old once it runs. A lost
// connection falls back to the one-second poll and tries again.
void Backend::watchEvents() {
    if (m_socketPath.isEmpty())
        return;
    QStringList panes;
    for (const auto &agent : m_model.agents())
        panes << agent.paneId;
    panes.sort();
    if (m_events && panes == m_watched)
        return;
    m_watched = panes;

    QJsonArray subscriptions;
    for (const char *type : {"pane.created", "pane.closed", "pane.exited", "pane.agent_detected", "tab.created",
                             "tab.closed", "tab.renamed", "workspace.created", "workspace.closed", "workspace.renamed"})
        subscriptions.append(QJsonObject{{"type", type}});
    for (const QString &pane : panes)
        subscriptions.append(QJsonObject{{"type", "pane.agent_status_changed"}, {"pane_id", pane}});
    const QByteArray request = QJsonDocument(QJsonObject{{"id", "drover:events"}, {"method", "events.subscribe"},
                                                         {"params", QJsonObject{{"subscriptions", subscriptions}}}})
                                   .toJson(QJsonDocument::Compact) + '\n';

    QPointer<QLocalSocket> previous = m_events;
    auto *socket = new QLocalSocket(this);
    m_events = socket;
    connect(socket, &QLocalSocket::connected, socket, [socket, request] { socket->write(request); });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket, previous] {
        while (socket->canReadLine()) {
            const QJsonObject message = QJsonDocument::fromJson(socket->readLine()).object();
            if (message.contains("event")) {
                m_eventRefresh.start();
            } else if (message.value("result").toObject().value("type").toString() == "subscription_started") {
                if (previous && previous != socket) {
                    previous->disconnect(this);
                    previous->abort();
                    previous->deleteLater();
                }
                m_poll.setInterval(5000);
                m_eventRefresh.start();
            }
        }
    });
    const auto lost = [this, socket] {
        if (m_events == socket) {
            m_events = nullptr;
            m_watched.clear();
            m_poll.setInterval(1000);
            QTimer::singleShot(2000, this, &Backend::watchEvents);
        }
        socket->deleteLater();
    };
    connect(socket, &QLocalSocket::errorOccurred, this, lost);
    connect(socket, &QLocalSocket::disconnected, this, lost);
    socket->connectToServer(m_socketPath);
}

QString Backend::readCloseAction(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*close_action\s*=\s*"?([A-Za-z0-9_.-]+\.[A-Za-z0-9_-]+)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

QString Backend::agentName(const QString &label, const QString &suffix) {
    QString slug;
    for (const QChar c : label.toLower()) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            slug += c;
        else if (!slug.isEmpty() && !slug.endsWith('-'))
            slug += '-';
    }
    while (slug.endsWith('-'))
        slug.chop(1);
    if (slug.isEmpty() || !slug.at(0).isLetter())
        slug.prepend("agent-");
    return slug.left(31 - suffix.size() - 1) + '-' + suffix;
}

void Backend::startAgent(const QString &workspaceId) {
    if (!m_busy.isEmpty() || workspaceId.isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    const QString label = m_workspaces.value(workspaceId, workspaceId);
    setBusy("start");
    setNotice(tr("Starting %1 in %2…").arg(m_agentKind, label), false);
    // The new tab opens where the workspace already works.
    run(program, {"pane", "list", "--workspace", workspaceId},
        [this, program, workspaceId, label](int, const QByteArray &out, const QByteArray &) {
            QString cwd;
            for (const auto &value : QJsonDocument::fromJson(out).object().value("result").toObject()
                                         .value("panes").toArray()) {
                const auto pane = value.toObject();
                const QString paneCwd = pane.value("foreground_cwd").toString(pane.value("cwd").toString());
                if (cwd.isEmpty() || pane.value("focused").toBool())
                    cwd = paneCwd;
            }
            QStringList arguments{"tab", "create", "--workspace", workspaceId, "--label", m_agentKind, "--no-focus"};
            if (!cwd.isEmpty())
                arguments << "--cwd" << cwd;
            run(program, arguments, [this, label](int code, const QByteArray &out, const QByteArray &err) {
                const QString error = commandError(out, err, code);
                const QString pane = QJsonDocument::fromJson(out).object().value("result").toObject()
                                         .value("root_pane").toObject().value("pane_id").toString();
                if (!error.isEmpty() || pane.isEmpty()) {
                    setBusy({});
                    setNotice(tr("Couldn't open a tab in %1: %2")
                                  .arg(label, error.isEmpty() ? tr("herdr gave no pane.") : error), true);
                    return;
                }
                launchAgent(pane, label, 1);
            });
        });
}

QVariantMap Backend::resolveNewWorkspace(const QString &text, const QString &home) {
    QString typed = text.trimmed();
    while (typed.size() > 1 && typed.endsWith('/'))
        typed.chop(1);
    if (typed.isEmpty())
        return {};
    QString path = typed;
    if (path == "~" || path.startsWith("~/"))
        path = home + path.mid(1);
    else if (!QDir::isAbsolutePath(path))
        path = home + '/' + path;
    const QFileInfo info(path);
    const bool isFolder = info.isDir();
    // A typed path to a folder that isn't there yet, in one that is, can be
    // made; anything deeper is more likely a typo.
    const bool isPath = typed.contains('/') || typed.startsWith('~');
    const bool makeFolder = isPath && !info.exists() && QFileInfo(info.absolutePath()).isDir();
    const QString cwd = isFolder || makeFolder ? QDir::cleanPath(info.absoluteFilePath()) : home;
    // A path names the workspace after its last folder; a name is kept as typed.
    QString label = typed.contains('/') || typed == "~" ? QFileInfo(QDir::cleanPath(path)).fileName() : typed;
    if (label.isEmpty())
        label = typed;
    QString folder = cwd;
    if (!home.isEmpty() && (folder == home || folder.startsWith(home + '/')))
        folder = '~' + folder.mid(home.size());
    return {{"label", label}, {"cwd", cwd}, {"folder", folder}, {"found", isFolder}, {"makeFolder", makeFolder}};
}

QVariantMap Backend::newWorkspace(const QString &text) const {
    return resolveNewWorkspace(text, m_home);
}

void Backend::createWorkspace(const QString &text, const QString &folder) {
    QVariantMap workspace = newWorkspace(text);
    if (!m_busy.isEmpty() || workspace.isEmpty())
        return;
    if (!folder.isEmpty())
        workspace.insert("cwd", folder);
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    const QString label = workspace.value("label").toString();
    if (folder.isEmpty() && workspace.value("makeFolder").toBool()) {
        const QString path = workspace.value("cwd").toString();
        if (!QDir().mkdir(path)) {
            setNotice(tr("Couldn't make the folder %1.").arg(workspace.value("folder").toString()), true);
            return;
        }
    }
    setBusy("start");
    setNotice(tr("Creating %1 and starting %2 there…").arg(label, m_agentKind), false);
    run(program, {"workspace", "create", "--label", label, "--cwd", workspace.value("cwd").toString(), "--no-focus"},
        [this, label](int code, const QByteArray &out, const QByteArray &err) {
            const QString error = commandError(out, err, code);
            // The new workspace's first pane is a shell, ready for the agent.
            const QString pane = QJsonDocument::fromJson(out).object().value("result").toObject()
                                     .value("root_pane").toObject().value("pane_id").toString();
            if (!error.isEmpty() || pane.isEmpty()) {
                setBusy({});
                setNotice(tr("Couldn't create %1: %2")
                              .arg(label, error.isEmpty() ? tr("herdr gave no pane.") : error), true);
                return;
            }
            refresh();
            launchAgent(pane, label, 1);
        });
}

void Backend::chooseFolderAndCreate(const QString &text) {
    const QVariantMap workspace = newWorkspace(text);
    if (!m_busy.isEmpty() || workspace.isEmpty())
        return;
    const QString label = workspace.value("label").toString();
    setBusy("folder");
    emit folderPickerOpened();
    auto done = [this, text](const QString &folder, const QString &error) {
        setBusy({});
        if (!error.isEmpty())
            setNotice(tr("Couldn't open the folder picker: %1").arg(error), true);
        emit folderPickerClosed(!folder.isEmpty(), text);
        if (!folder.isEmpty())
            createWorkspace(text, folder);
    };
    const QString title = tr("Folder for %1").arg(label);
    const QString start = workspace.value("cwd").toString();
    if (m_folderChooser)
        m_folderChooser(start, title, done);
    else if (!m_herdrOverride.isEmpty())
        done({}, tr("no folder picker in tests."));
    else
        choosePortalFolder(start, title, done);
}

// org.freedesktop.portal.FileChooser, which Omarchy routes to its own picker.
// The answer comes later as a Response signal on a request object whose path
// follows from our bus name and a token, so we listen before asking.
void Backend::choosePortalFolder(const QString &start, const QString &title,
                                 std::function<void(const QString &, const QString &)> done) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        done({}, tr("no session bus."));
        return;
    }
    const QString token = "drover" + QUuid::createUuid().toString(QUuid::Id128);
    const QString sender = bus.baseService().mid(1).replace('.', '_');
    const QString requestPath = "/org/freedesktop/portal/desktop/request/" + sender + '/' + token;

    // One listener per request, gone once it answers.
    auto *listener = new PortalRequest(this);
    connect(listener, &PortalRequest::answered, this, [listener, done](uint response, const QVariantMap &results) {
        listener->deleteLater();
        QString folder;
        QStringList uris = results.value("uris").toStringList();
        if (uris.isEmpty() && results.value("uris").canConvert<QDBusArgument>())
            results.value("uris").value<QDBusArgument>() >> uris;
        if (response == 0 && !uris.isEmpty())
            folder = QUrl(uris.first()).toLocalFile();
        // 1 is a cancel, which needs no message.
        done(folder, response > 1 ? tr("the picker ended with an error.") : QString());
    });
    bus.connect("org.freedesktop.portal.Desktop", requestPath, "org.freedesktop.portal.Request", "Response",
                listener, SLOT(response(uint, QVariantMap)));

    auto message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.FileChooser", "OpenFile");
    QVariantMap options{{"handle_token", token}, {"directory", true}, {"modal", true},
                        {"current_folder", QByteArray(start.toUtf8() + '\0')}};
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, listener, done] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            listener->deleteLater();
            done({}, reply.error().message());
        }
    });
}

void Backend::closeWorkspace(const QString &workspaceId) {
    if (!m_busy.isEmpty() || workspaceId.isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    const QString label = m_workspaces.value(workspaceId, workspaceId);
    setBusy("close");
    if (!m_closeAction.isEmpty()) {
        invokeCloseAction(workspaceId, label);
        return;
    }
    run(program, {"workspace", "close", workspaceId}, [this, label](int code, const QByteArray &out, const QByteArray &err) {
        setBusy({});
        const QString error = commandError(out, err, code);
        if (!error.isEmpty()) {
            setNotice(tr("Couldn't close %1: %2").arg(label, error), true);
            return;
        }
        setNotice(tr("Closed %1.").arg(label), false);
        refresh();
    });
}

// herdr's CLI invokes a plugin action only for whatever is focused; its
// socket API takes the workspace as the action's context.
void Backend::invokeCloseAction(const QString &workspaceId, const QString &label) {
    const int dot = m_closeAction.lastIndexOf('.');
    const QJsonObject request{
        {"id", "drover:close"},
        {"method", "plugin.action.invoke"},
        {"params", QJsonObject{{"plugin_id", m_closeAction.left(dot)},
                               {"action_id", m_closeAction.mid(dot + 1)},
                               {"context", QJsonObject{{"workspace_id", workspaceId}, {"workspace_label", label}}}}}};

    auto *socket = new QLocalSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto finish = [this, socket, label](const QString &error) {
        if (socket->property("done").toBool())
            return;
        socket->setProperty("done", true);
        socket->deleteLater();
        setBusy({});
        if (!error.isEmpty()) {
            setNotice(tr("Couldn't close %1: %2").arg(label, error), true);
            return;
        }
        setNotice(tr("Closed %1.").arg(label), false);
        refresh();
    };
    connect(timer, &QTimer::timeout, socket, [finish] { finish(tr("no answer within a few seconds.")); });
    connect(socket, &QLocalSocket::errorOccurred, socket, [socket, finish](QLocalSocket::LocalSocketError) {
        finish(tr("herdr's socket: %1").arg(socket->errorString()));
    });
    connect(socket, &QLocalSocket::connected, socket, [socket, request] {
        socket->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(socket, &QLocalSocket::readyRead, socket, [socket, finish] {
        if (!socket->canReadLine())
            return;
        const QByteArray line = socket->readLine();
        const auto error = errorObject(line, {});
        finish(error.isEmpty() ? QString() : error.value("message").toString(error.value("code").toString()));
    });
    timer->start(m_timeout);
    socket->connectToServer(m_socketPath);
}

bool Backend::meaningfulTitle(const QString &title, const QString &kind) {
    const QString text = title.trimmed().toLower();
    if (text.isEmpty() || text.startsWith('~') || text.startsWith('/') || text.contains('@'))
        return false;
    // What agents call their terminal before they know the task.
    static const QStringList generic{"claude", "claude code", "codex", "opencode", "gemini", "gemini cli",
                                     "amp", "cursor", "cursor agent", "pi", "droid", "aider"};
    return text != kind.toLower() && !generic.contains(text);
}

QString Backend::tabLabel(const QString &title) {
    const QString text = title.simplified();
    return text.size() <= 40 ? text : text.left(39).trimmed() + QChar(0x2026);
}

// Agents drover started carry a made-up name (work-81t). Once the agent
// titles its terminal after the first question, the tab takes that title
// and the name goes, so herdr and drover show the title, as for any agent.
void Backend::nameStartedAgents(const QList<Agent> &agents) {
    if (m_unnamed.isEmpty())
        return;
    QSet<QString> live;
    for (const auto &agent : agents) {
        live.insert(agent.paneId);
        if (m_unnamed.contains(agent.paneId))
            m_unnamedSeen.insert(agent.paneId);
        const auto given = m_unnamed.constFind(agent.paneId);
        if (given == m_unnamed.constEnd() || !meaningfulTitle(agent.terminalTitle, agent.kind))
            continue;
        const QString name = *given;
        m_unnamed.remove(agent.paneId);
        m_unnamedSeen.remove(agent.paneId);
        if (!agent.tabId.isEmpty())
            run(herdr(), {"tab", "rename", agent.tabId, tabLabel(agent.terminalTitle)}, [](int, auto, auto) {});
        // Only the name drover gave goes; one the user chose stays.
        if (agent.name == name)
            run(herdr(), {"agent", "rename", agent.paneId, "--clear"}, [this](int, auto, auto) { refresh(); });
    }
    // An agent just started may not be listed yet; one listed and then gone
    // is forgotten.
    for (auto it = m_unnamed.begin(); it != m_unnamed.end();) {
        if (!live.contains(it.key()) && m_unnamedSeen.remove(it.key()))
            it = m_unnamed.erase(it);
        else
            ++it;
    }
}

void Backend::launchAgent(const QString &pane, const QString &label, int attempt) {
    const QString name = agentName(label, QString::number(QDateTime::currentMSecsSinceEpoch() % 46656, 36));
    // herdr waits until the agent is ready for input, up to 30 seconds.
    run(herdr(), {"agent", "start", name, "--kind", m_agentKind, "--pane", pane},
        [this, pane, label, name, attempt](int code, const QByteArray &out, const QByteArray &err) {
            const QString error = commandError(out, err, code);
            // Blocked at startup (a trust prompt, say) still means started:
            // its question shows in the panel.
            if (error.isEmpty() || errorCode(out, err) == "agent_not_ready") {
                m_unnamed.insert(pane, name);
                setBusy({});
                setNotice(tr("Started %1 in %2.").arg(m_agentKind, label), false);
                m_pendingSelect = pane;
                refresh();
                return;
            }
            // A brand-new tab's shell may not be at its prompt yet.
            if (attempt < 3 && code != TimedOut) {
                QTimer::singleShot(1000, this, [this, pane, label, attempt] { launchAgent(pane, label, attempt + 1); });
                return;
            }
            setBusy({});
            setNotice(tr("Couldn't start %1 in %2: %3").arg(m_agentKind, label, error), true);
        },
        45000);
}

QHash<QString, QString> Backend::parseWorkspaces(const QByteArray &json) {
    QHash<QString, QString> labels;
    const auto list = QJsonDocument::fromJson(json).object().value("result").toObject().value("workspaces").toArray();
    for (const auto &value : list) {
        const auto workspace = value.toObject();
        labels.insert(workspace.value("workspace_id").toString(), workspace.value("label").toString());
    }
    return labels;
}

QList<Agent> Backend::parseAgents(const QByteArray &json, const QHash<QString, QString> &workspaces,
                                  const QString &home, QString *error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    const auto result = document.object().value("result").toObject();
    if (parseError.error != QJsonParseError::NoError || !result.value("agents").isArray()) {
        if (error)
            *error = tr("unexpected answer from herdr agent list.");
        return {};
    }

    QList<Agent> agents;
    for (const auto &value : result.value("agents").toArray()) {
        const auto object = value.toObject();
        Agent agent;
        agent.paneId = object.value("pane_id").toString();
        agent.kind = object.value("agent").toString();
        agent.status = object.value("agent_status").toString("unknown");
        agent.terminalId = object.value("terminal_id").toString();
        agent.tabId = object.value("tab_id").toString();
        agent.name = object.value("name").toString();
        agent.terminalTitle = object.value("terminal_title_stripped").toString().trimmed();
        agent.sessionId = object.value("agent_session").toObject().value("value").toString();
        agent.focused = object.value("focused").toBool();
        agent.workspaceId = object.value("workspace_id").toString();
        agent.workspace = workspaces.value(agent.workspaceId, agent.workspaceId);
        QString cwd = object.value("foreground_cwd").toString(object.value("cwd").toString());
        if (!home.isEmpty() && (cwd == home || cwd.startsWith(home + '/')))
            cwd = '~' + cwd.mid(home.size());
        agent.cwd = cwd;
        // A name given with `herdr agent rename` wins, then the title the agent
        // sets for its terminal, then what it is and where.
        agent.title = object.value("name").toString();
        if (agent.title.isEmpty())
            agent.title = object.value("terminal_title_stripped").toString().trimmed();
        if (agent.title.isEmpty())
            agent.title = agent.kind + " · " + QFileInfo(cwd).fileName();
        if (!agent.paneId.isEmpty())
            agents.append(agent);
    }
    return agents;
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::setNotice(const QString &notice, bool isError) {
    m_notice = notice;
    m_noticeIsError = isError;
    if (!notice.isEmpty())
        m_noticeTimer.start();
    emit noticeChanged();
}

void Backend::setBusy(const QString &busy) {
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
