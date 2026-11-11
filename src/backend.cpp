#include "backend.h"

#include "ansi.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Backend::Backend(const QString &herdr, const QString &hyprctl, QObject *parent)
    : QObject(parent), m_herdrOverride(herdr), m_hyprctlOverride(hyprctl),
      m_agentKind(readAgentKind(QDir::homePath() + "/.config/drover/config")) {
    // A listing takes a few milliseconds, so a one-second poll is cheap and far
    // simpler than herdr's per-pane event subscriptions.
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &Backend::refresh);
    m_poll.start();

    m_readPoll.setInterval(1000);
    connect(&m_readPoll, &QTimer::timeout, this, &Backend::readSelected);

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(5000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { setNotice({}, false); });

    refresh();
}

// Looked up on every use, so installing herdr or Hyprland later needs no restart.
QString Backend::herdr() {
    return m_herdrOverride.isEmpty() ? QStandardPaths::findExecutable("herdr") : m_herdrOverride;
}

QString Backend::hyprctl() {
    return m_hyprctlOverride.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : m_hyprctlOverride;
}

QString Backend::selectedTitle() const {
    const int row = selectedIndex();
    return row < 0 ? QString() : m_model.agents().at(row).title;
}

QString Backend::selectedStatus() const {
    const int row = selectedIndex();
    return row < 0 ? QString() : m_model.agents().at(row).status;
}

void Backend::setActive(bool active) {
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        readSelected();
        m_readPoll.start();
    } else {
        m_readPoll.stop();
    }
    emit activeChanged();
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
    if (m_listing)
        return;
    m_listing = true;

    // Workspace labels first, so each agent says which workspace it lives in.
    run(program, {"workspace", "list"}, [this, program](int code, const QByteArray &out, const QByteArray &) {
        if (code == 0)
            m_workspaces = parseWorkspaces(out);
        run(program, {"agent", "list"}, [this](int code, const QByteArray &out, const QByteArray &err) {
            m_listing = false;
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
        });
    });
}

void Backend::applyAgents(const QList<Agent> &agents) {
    const QString before = selectedTitle() + '\n' + selectedStatus() + '\n' + QString::number(selectedIndex());
    m_model.setAgents(agents);

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
        setOutput({});
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
    setOutput({});
    emit selectedChanged();
    emit selectedInfoChanged();
    if (m_active)
        readSelected();
}

void Backend::selectNext(int step) {
    const int count = m_model.count();
    if (count == 0)
        return;
    const int row = selectedIndex();
    select(m_model.agents().at(qBound(0, row < 0 ? 0 : row + step, count - 1)).paneId);
}

void Backend::readSelected() {
    if (m_selected.isEmpty() || m_reading)
        return;
    // herdr can only scroll back through an agent's history while it is idle;
    // while it works, its visible screen is what there is.
    const bool busy = selectedStatus() == "working" || selectedStatus() == "blocked";
    readScreen(m_selected, busy ? "visible" : "recent");
}

void Backend::readScreen(const QString &pane, const QString &source) {
    const QString program = herdr();
    if (program.isEmpty())
        return;
    m_reading = true;
    // With its colors, so the panel shows the agent as the terminal does.
    QStringList arguments{"agent", "read", pane, "--source", source, "--format", "ansi"};
    if (source == "recent")
        arguments << "--lines" << "400";
    run(program, arguments, [this, pane, source](int code, const QByteArray &out, const QByteArray &err) {
        m_reading = false;
        // Another agent was picked meanwhile: read that one now rather than
        // leaving its screen empty until the next poll.
        if (pane != m_selected) {
            if (m_active)
                readSelected();
            return;
        }
        const QString error = commandError(out, err, code);
        if (!error.isEmpty()) {
            // The agent started working between the listing and the read.
            if (source == "recent" && errorCode(out, err) == "agent_not_idle") {
                readScreen(pane, "visible");
                return;
            }
            // A failed read keeps the last screen, and says it is stale.
            if (m_output.isEmpty())
                setOutput(tr("Couldn't read the agent: %1").arg(error));
            else
                setNotice(tr("The screen isn't updating: %1").arg(error), true);
            return;
        }
        // The CLI prints the screen as plain text, or wrapped in JSON.
        const auto result = QJsonDocument::fromJson(out).object().value("result").toObject();
        const QString text = result.isEmpty() ? QString::fromUtf8(out)
                                              : result.value("text").toString(result.value("output").toString());
        setOutput(cleanOutput(text, 400));
    });
}

void Backend::prompt(const QString &text) {
    if (!m_busy.isEmpty() || m_selected.isEmpty() || text.trimmed().isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    // herdr reads a message starting with "--" as an option of its own; a
    // leading space keeps it a message.
    QString message = text.trimmed();
    if (message.startsWith('-'))
        message.prepend(' ');
    const QString title = selectedTitle();
    setBusy("send");
    run(program, {"agent", "prompt", m_selected, message},
        [this, title, text](int code, const QByteArray &out, const QByteArray &err) {
            setBusy({});
            const QString error = commandError(out, err, code);
            if (error.isEmpty()) {
                setNotice(tr("Sent to %1.").arg(title), false);
                emit promptSent(text);
            } else {
                setNotice(tr("Not sent: %1").arg(error), true);
            }
            readSelected();
        });
}

void Backend::sendKeys(const QStringList &keys) {
    if (!m_busy.isEmpty() || m_selected.isEmpty() || keys.isEmpty())
        return;
    const QString program = herdr();
    if (program.isEmpty()) {
        setNotice(tr("herdr is not installed."), true);
        return;
    }
    setBusy("keys");
    run(program, QStringList{"agent", "send-keys", m_selected} + keys,
        [this](int code, const QByteArray &out, const QByteArray &err) {
            setBusy({});
            const QString error = commandError(out, err, code);
            if (!error.isEmpty())
                setNotice(tr("Keys not sent: %1").arg(error), true);
            readSelected();
        });
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

void Backend::launchAgent(const QString &pane, const QString &label, int attempt) {
    const QString name = agentName(label, QString::number(QDateTime::currentMSecsSinceEpoch() % 46656, 36));
    // herdr waits until the agent is ready for input, up to 30 seconds.
    run(herdr(), {"agent", "start", name, "--kind", m_agentKind, "--pane", pane},
        [this, pane, label, attempt](int code, const QByteArray &out, const QByteArray &err) {
            const QString error = commandError(out, err, code);
            // Blocked at startup (a trust prompt, say) still means started:
            // its question shows in the panel.
            if (error.isEmpty() || errorCode(out, err) == "agent_not_ready") {
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

QString Backend::cleanOutput(const QString &screen, int maxLines) {
    // Agent TUIs draw rules and boxes across the full terminal width; they
    // carry nothing outside the terminal, so lines that are only drawing go.
    // The screen keeps its color codes; they are ignored for that test.
    static const QRegularExpression drawing(QStringLiteral("^[\\s─━═│┃╭╮╰╯┌┐└┘├┤┬┴┼▔▁-]*$"));
    static const QRegularExpression trailing(QStringLiteral("(?:\\s|\\x1b\\[[0-9;:]*m)+$"));
    QStringList lines;
    bool lastBlank = true;
    for (QString line : screen.split('\n')) {
        line.remove('\r');
        line.remove(trailing);
        const bool blank = drawing.match(Ansi::strip(line)).hasMatch();
        if (blank && lastBlank)
            continue;
        lines.append(blank ? QString() : line);
        lastBlank = blank;
    }
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join('\n');
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

void Backend::setOutput(const QString &output) {
    if (output == m_outputRaw)
        return;
    m_outputRaw = output;
    m_output = Ansi::strip(output);
    m_outputHtml = Ansi::toHtml(output, m_palette, QGuiApplication::font().family());
    emit outputChanged();
}

void Backend::setPalette(const QStringList &palette) {
    if (palette == m_palette)
        return;
    m_palette = palette;
    m_outputHtml = Ansi::toHtml(m_outputRaw, m_palette, QGuiApplication::font().family());
    emit outputChanged();
}

void Backend::setBusy(const QString &busy) {
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
