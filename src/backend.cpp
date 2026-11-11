#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Backend::Backend(const QString &herdr, const QString &hyprctl, QObject *parent)
    : QObject(parent),
      m_herdr(herdr.isEmpty() ? QStandardPaths::findExecutable("herdr") : herdr),
      m_hyprctl(hyprctl.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : hyprctl) {
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
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
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
    timer->start(timeout);
}

QString Backend::commandError(const QByteArray &out, const QByteArray &err, int code) {
    const auto object = QJsonDocument::fromJson(out).object();
    const auto error = object.value("error").toObject();
    if (!error.isEmpty())
        return error.value("message").toString(error.value("code").toString());
    if (code == 0)
        return {};
    const QString message = QString::fromUtf8(err).trimmed().section('\n', 0, 0);
    return message.isEmpty() ? tr("herdr exited with code %1.").arg(code) : message;
}

void Backend::refresh() {
    if (m_herdr.isEmpty()) {
        setError(tr("herdr is not installed."));
        return;
    }
    if (m_listing)
        return;
    m_listing = true;

    // Workspace labels first, so each agent says which workspace it lives in.
    run(m_herdr, {"workspace", "list"}, [this](int code, const QByteArray &out, const QByteArray &) {
        if (code == 0)
            m_workspaces = parseWorkspaces(out);
        run(m_herdr, {"agent", "list"}, [this](int code, const QByteArray &out, const QByteArray &err) {
            m_listing = false;
            QString error = commandError(out, err, code);
            QList<Agent> agents;
            if (error.isEmpty())
                agents = parseAgents(out, m_workspaces, QDir::homePath(), &error);
            if (!error.isEmpty()) {
                setError(tr("herdr: %1").arg(error));
                applyAgents({});
                return;
            }
            setError({});
            applyAgents(agents);
        });
    });
}

void Backend::applyAgents(const QList<Agent> &agents) {
    const QString before = selectedTitle() + '\n' + selectedStatus();
    const int beforeIndex = selectedIndex();
    m_model.setAgents(agents);

    QHash<QString, int> counts;
    for (const auto &agent : agents)
        ++counts[agent.status];
    if (counts != m_counts) {
        m_counts = counts;
        emit countsChanged();
    }

    // Keep the selection on the same pane; pick the first agent when it went.
    if (m_model.indexOf(m_selected) < 0) {
        select(agents.isEmpty() ? QString() : agents.first().paneId);
        return;
    }
    if (before != selectedTitle() + '\n' + selectedStatus() || beforeIndex != selectedIndex())
        emit selectionChanged();
}

void Backend::select(const QString &paneId) {
    if (paneId == m_selected)
        return;
    m_selected = paneId;
    setOutput({});
    emit selectionChanged();
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
    if (m_selected.isEmpty() || m_herdr.isEmpty() || m_reading)
        return;
    m_reading = true;
    const QString pane = m_selected;
    run(m_herdr, {"agent", "read", pane, "--source", "recent", "--lines", "80"},
        [this, pane](int code, const QByteArray &out, const QByteArray &err) {
            m_reading = false;
            if (pane != m_selected)
                return;
            // The CLI wraps the screen in JSON when asked for it, but prints
            // plain text by default; take either.
            const auto object = QJsonDocument::fromJson(out).object();
            const QString error = commandError(out, err, code);
            if (!error.isEmpty()) {
                setOutput(tr("Couldn't read the agent: %1").arg(error));
                return;
            }
            const auto result = object.value("result").toObject();
            const QString text = result.isEmpty() ? QString::fromUtf8(out)
                                                  : result.value("text").toString(result.value("output").toString());
            setOutput(cleanOutput(text));
        });
}

void Backend::prompt(const QString &text) {
    const QString message = text.trimmed();
    if (m_selected.isEmpty() || message.isEmpty())
        return;
    const QString title = selectedTitle();
    run(m_herdr, {"agent", "prompt", m_selected, message},
        [this, title](int code, const QByteArray &out, const QByteArray &err) {
            const QString error = commandError(out, err, code);
            if (error.isEmpty()) {
                setNotice(tr("Sent to %1.").arg(title), false);
                emit promptSent();
            } else
                setNotice(tr("Not sent: %1").arg(error), true);
            readSelected();
        });
}

void Backend::sendKeys(const QStringList &keys) {
    if (m_selected.isEmpty() || keys.isEmpty())
        return;
    run(m_herdr, QStringList{"agent", "send-keys", m_selected} + keys,
        [this](int code, const QByteArray &out, const QByteArray &err) {
            const QString error = commandError(out, err, code);
            if (!error.isEmpty())
                setNotice(tr("Keys not sent: %1").arg(error), true);
            readSelected();
        });
}

void Backend::jump() {
    if (m_selected.isEmpty())
        return;
    run(m_herdr, {"agent", "focus", m_selected}, [this](int code, const QByteArray &out, const QByteArray &err) {
        const QString error = commandError(out, err, code);
        if (!error.isEmpty()) {
            setNotice(tr("Couldn't open the agent: %1").arg(error), true);
            return;
        }
        if (m_hyprctl.isEmpty()) {
            emit jumped();
            return;
        }
        // herdr now shows the agent; bring its window forward, wherever it is.
        run(m_hyprctl, {"clients", "-j"}, [this](int, const QByteArray &out, const QByteArray &) {
            QString address;
            for (const auto &value : QJsonDocument::fromJson(out).array()) {
                const auto client = value.toObject();
                if (client.value("class").toString().contains("herdr", Qt::CaseInsensitive)) {
                    address = client.value("address").toString();
                    break;
                }
            }
            if (address.isEmpty()) {
                setNotice(tr("No herdr window is open."), true);
                return;
            }
            run(m_hyprctl, {"dispatch", "focuswindow", "address:" + address},
                [](int, const QByteArray &, const QByteArray &) {});
            emit jumped();
        });
    });
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
        agent.workspace = workspaces.value(object.value("workspace_id").toString(),
                                           object.value("workspace_id").toString());
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
    // Agent TUIs draw rules and boxes across the full terminal width; in a
    // narrow panel they wrap into noise, so drop lines that are only drawing.
    static const QRegularExpression drawing(QStringLiteral("^[\\s─━═│┃╭╮╰╯┌┐└┘├┤┬┴┼▔▁-]*$"));
    QStringList lines;
    bool lastBlank = true;
    for (QString line : screen.split('\n')) {
        while (!line.isEmpty() && line.back().isSpace())
            line.chop(1);
        const bool blank = drawing.match(line).hasMatch();
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
    if (output == m_output)
        return;
    m_output = output;
    emit outputChanged();
}
