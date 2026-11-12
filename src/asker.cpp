#include "asker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>

#include "hook.h"

Asker::Asker(QObject *parent) : QObject(parent) {}

Asker::~Asker() {
    // Hooks still waiting get no answer: Claude asks in the terminal.
    for (const auto &request : std::as_const(m_requests))
        if (request.socket)
            request.socket->abort();
}

bool Asker::listen(const QString &name) {
    m_server = new QLocalServer(this);
    QLocalServer::removeServer(name);
    if (!m_server->listen(name))
        return false;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket->property("accepted").toBool())
                    return;
                QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
                if (!buffer.contains('\n')) {
                    socket->setProperty("buffer", buffer);
                    return;
                }
                socket->setProperty("accepted", true);
                const QJsonObject ask = QJsonDocument::fromJson(buffer.left(buffer.indexOf('\n'))).object();
                accept(socket, ask);
            });
        }
    });
    return true;
}

int Asker::readHold(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*ask_hold\s*=\s*"?(\d+)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1).toInt();
    }
    return 180;
}

QString Asker::readLook(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(R"re(^\s*stage\s*=\s*"?(fence|spotlight)"?\s*$)re",
                                             QRegularExpression::MultilineOption);
        const auto match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return QStringLiteral("fence");
}

void Asker::accept(QLocalSocket *socket, const QJsonObject &ask) {
    const QString kind = ask.value("kind").toString();
    const QString pane = ask.value("pane").toString();
    const auto passNow = [socket] {
        socket->write("{}\n");
        socket->flush();
        socket->disconnectFromServer();
    };
    if ((kind != "question" && kind != "permission") || pane.isEmpty()) {
        passNow();
        return;
    }
    // Only an agent drover knows, and its own session: a Claude started
    // inside an agent's pane (a script, `claude -p`) inherits the pane.
    const auto agent = m_agents.constFind(pane);
    if (agent == m_agents.constEnd()) {
        passNow();
        return;
    }
    const QString session = ask.value("session").toString();
    if (!agent->sessionId.isEmpty() && !session.isEmpty() && agent->sessionId != session) {
        passNow();
        return;
    }
    // A question for the agent the user is looking at goes to its terminal.
    if (kind == "question" && m_watching && m_watching(pane)) {
        passNow();
        return;
    }

    const QVariantMap first = current();
    const int count = m_requests.size();
    const QSet<QString> asking = askingPanes();

    // A new request from the same agent means it no longer waits for the
    // older one (it was answered in the terminal).
    for (int i = m_requests.size() - 1; i >= 0; --i)
        if (m_requests.at(i).ask.value("pane").toString() == pane)
            remove(m_requests.at(i).id);

    Request request;
    request.id = m_nextId++;
    request.socket = socket;
    request.ask = ask;
    const int id = request.id;
    if (kind == "question" && m_hold > 0) {
        request.hold = new QTimer(this);
        request.hold->setSingleShot(true);
        request.hold->setInterval(m_hold * 1000);
        connect(request.hold, &QTimer::timeout, this, [this, id] { pass(id); });
        request.hold->start();
    }
    connect(socket, &QLocalSocket::disconnected, this, [this, id] {
        // The agent stopped waiting (Esc in the terminal, Claude's timeout).
        if (indexOf(id) < 0)
            return;
        const QVariantMap first = current();
        const int count = m_requests.size();
        const QSet<QString> asking = askingPanes();
        remove(id);
        changed(first, count, asking);
    });
    m_requests.append(request);
    changed(first, count, asking);
    emit arrived();
}

int Asker::indexOf(int id) const {
    for (int i = 0; i < m_requests.size(); ++i)
        if (m_requests.at(i).id == id)
            return i;
    return -1;
}

void Asker::remove(int id) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    Request request = m_requests.takeAt(i);
    if (request.hold)
        request.hold->deleteLater();
    if (request.socket) {
        request.socket->disconnect(this);
        if (request.socket->state() == QLocalSocket::ConnectedState) {
            // An unanswered hook prints nothing: Claude asks itself.
            request.socket->write("{}\n");
            request.socket->flush();
            request.socket->disconnectFromServer();
        }
    }
}

void Asker::reply(int id, const QJsonObject &output) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    const QVariantMap first = current();
    const int count = m_requests.size();
    const QSet<QString> asking = askingPanes();
    Request request = m_requests.takeAt(i);
    if (request.hold)
        request.hold->deleteLater();
    if (request.socket && request.socket->state() == QLocalSocket::ConnectedState) {
        request.socket->disconnect(this);
        request.socket->write(QJsonDocument(QJsonObject{{"output", output}}).toJson(QJsonDocument::Compact) + '\n');
        request.socket->flush();
        request.socket->disconnectFromServer();
    }
    changed(first, count, asking);
}

void Asker::changed(const QVariantMap &firstBefore, int countBefore, const QSet<QString> &askingBefore) {
    if (current() != firstBefore)
        emit currentChanged();
    if (qMax(0, countBefore - 1) != waiting())
        emit waitingChanged();
    if (askingPanes() != askingBefore)
        emit askingChanged();
}

void Asker::answer(int id, const QStringList &answers) {
    const int i = indexOf(id);
    if (i < 0 || m_requests.at(i).ask.value("kind").toString() != "question")
        return;
    reply(id, Hook::questionOutput(m_requests.at(i).ask.value("input").toObject(), answers));
}

void Asker::allow(int id, bool always) {
    const int i = indexOf(id);
    if (i < 0 || m_requests.at(i).ask.value("kind").toString() != "permission")
        return;
    reply(id, Hook::allowOutput(always ? m_requests.at(i).ask.value("suggestions").toArray() : QJsonArray()));
}

void Asker::deny(int id, const QString &message) {
    const int i = indexOf(id);
    if (i < 0 || m_requests.at(i).ask.value("kind").toString() != "permission")
        return;
    const QString text = message.trimmed();
    reply(id, Hook::denyOutput(text.isEmpty() ? QStringLiteral("The user doesn't want to proceed with this tool use.")
                                              : text,
                               text.isEmpty()));
}

void Asker::pass(int id) {
    if (indexOf(id) < 0)
        return;
    const QVariantMap first = current();
    const int count = m_requests.size();
    const QSet<QString> asking = askingPanes();
    remove(id);
    changed(first, count, asking);
}

QSet<QString> Asker::askingPanes() const {
    QSet<QString> panes;
    for (const auto &request : m_requests)
        if (request.ask.value("kind").toString() == "question")
            panes.insert(request.ask.value("pane").toString());
    return panes;
}

QVariantMap Asker::current() const {
    if (m_requests.isEmpty())
        return {};
    const Request &request = m_requests.first();
    const QJsonObject &ask = request.ask;
    const QString pane = ask.value("pane").toString();
    const Agent agent = m_agents.value(pane);
    const QString tool = ask.value("tool").toString();
    const QJsonObject input = ask.value("input").toObject();
    QVariantMap map{
        {"id", request.id},
        {"kind", ask.value("kind").toString()},
        {"pane", pane},
        {"title", agent.title},
        {"workspace", agent.workspace},
        {"cwd", agent.cwd},
        {"tool", tool},
    };
    if (ask.value("kind").toString() == "question") {
        map.insert("questions", input.value("questions").toArray().toVariantList());
    } else {
        map.insert("detail", detail(tool, input));
        map.insert("always", alwaysWhat(ask.value("suggestions").toArray()));
    }
    return map;
}

QString Asker::detail(const QString &tool, const QJsonObject &input) {
    const auto text = [&](const char *key) { return input.value(key).toString(); };
    QString home = QDir::homePath();
    const auto shortPath = [&](QString path) {
        if (path == home || path.startsWith(home + '/'))
            path = '~' + path.mid(home.size());
        return path;
    };
    if (tool == "Bash")
        return text("command");
    if (!text("file_path").isEmpty())
        return shortPath(text("file_path"));
    if (!text("notebook_path").isEmpty())
        return shortPath(text("notebook_path"));
    if (!text("url").isEmpty())
        return text("url");
    if (!text("path").isEmpty())
        return shortPath(text("path"));
    if (!text("pattern").isEmpty())
        return text("pattern");
    if (!text("command").isEmpty())
        return text("command");
    if (!text("description").isEmpty())
        return text("description");
    return QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)).left(400);
}

QString Asker::alwaysWhat(const QJsonArray &suggestions) {
    QStringList parts;
    const QString home = QDir::homePath();
    for (const auto &value : suggestions) {
        const QJsonObject suggestion = value.toObject();
        const QString type = suggestion.value("type").toString();
        if (type == "addRules" || type == "replaceRules") {
            if (suggestion.value("behavior").toString("allow") != "allow")
                continue;
            for (const auto &rule : suggestion.value("rules").toArray()) {
                const QJsonObject r = rule.toObject();
                const QString content = r.value("ruleContent").toString();
                parts << (content.isEmpty() ? r.value("toolName").toString()
                                            : r.value("toolName").toString() + '(' + content + ')');
            }
        } else if (type == "setMode") {
            const QString mode = suggestion.value("mode").toString();
            parts << (mode == "acceptEdits" ? tr("file edits") : mode);
        } else if (type == "addDirectories") {
            for (const auto &dir : suggestion.value("directories").toArray()) {
                QString path = dir.toString();
                if (path == home || path.startsWith(home + '/'))
                    path = '~' + path.mid(home.size());
                parts << path;
            }
        }
    }
    return parts.join(", ");
}

void Asker::agentsListed(const QList<Agent> &agents) {
    m_agents.clear();
    for (const auto &agent : agents)
        m_agents.insert(agent.paneId, agent);

    const QVariantMap first = current();
    const int count = m_requests.size();
    const QSet<QString> asking = askingPanes();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<int> gone;
    for (auto &request : m_requests) {
        const QString pane = request.ask.value("pane").toString();
        const auto agent = m_agents.constFind(pane);
        if (agent == m_agents.constEnd()) {
            // A pane missing for a few seconds has closed.
            if (!request.missingSince)
                request.missingSince = now;
            else if (now - request.missingSince >= 2900)
                gone << request.id;
            continue;
        }
        request.missingSince = 0;
        if (request.ask.value("kind").toString() != "permission")
            continue;
        // herdr sees the prompt in the terminal; once it doesn't any more,
        // someone answered it there.
        if (agent->status == "blocked")
            request.seenBlocked = true;
        else if (request.seenBlocked)
            gone << request.id;
    }
    for (int id : gone)
        remove(id);
    // Also when only the title or workspace on stage changed.
    changed(first, count, asking);
}
