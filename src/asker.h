#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <functional>

#include "agentmodel.h"

class QLocalServer;
class QLocalSocket;
class QTimer;

// The agents' questions on drover's stage. Each `drover hook` (see hook.h)
// that reaches drover is one request, shown one at a time, oldest first,
// until the user answers it in drover, it goes back to the terminal, or the
// agent stops waiting for it.
//
// A question (Claude's AskUserQuestion) waits only here: Claude shows no
// dialog meanwhile. So it goes straight back to the terminal when the user
// is already looking at the agent, and after a while (ask_hold) if nobody
// answers, so the herdr window and its remote clients can answer it too.
//
// A permission prompt shows in the terminal as well; when it is answered
// there, herdr sees the agent working again and the stage lets it go.
class Asker : public QObject {
    Q_OBJECT
    // The request on stage: {id, kind ("question", "permission" or "reply"),
    // pane, title, workspace, questions [{question, header, multiSelect,
    // options [{label, description}]}], tool, detail, always, text (the end
    // of the agent's message)}; empty when nothing asks.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    // How many wait behind it.
    Q_PROPERTY(int waiting READ waiting NOTIFY waitingChanged)
    // How a sheep comes on stage: "fence" or "spotlight" (stage = ... in the config).
    Q_PROPERTY(QString look READ look CONSTANT)

public:
    explicit Asker(QObject *parent = nullptr);
    ~Asker() override;

    // Listens for hooks; false when another drover already does.
    bool listen(const QString &name);

    // Whether the user is looking at an agent's pane right now (the panel
    // shows it, or the herdr window does), so a question goes there.
    void setWatching(std::function<bool(const QString &pane)> watching) { m_watching = std::move(watching); }
    // Seconds a question waits here before it goes back to the terminal; 0
    // keeps it until it is answered.
    void setHold(int seconds) { m_hold = seconds; }
    int hold() const { return m_hold; }
    // ask_hold = ... in the config; 180 seconds when it isn't there.
    static int readHold(const QString &configFile);
    QString look() const { return m_look; }
    void setLook(const QString &look) { m_look = look; }
    // stage = ... in the config; "fence" when it isn't there.
    static QString readLook(const QString &configFile);

    QVariantMap current() const;
    int waiting() const { return qMax(0, int(m_requests.size()) - 1); }
    // Panes whose question waits here (not permission prompts, which the
    // terminal shows too).
    QSet<QString> askingPanes() const;

    // Answers, one per question, in order; a multi-select answer is its
    // labels joined by ", ".
    Q_INVOKABLE void answer(int id, const QStringList &answers);
    // Lets the tool run, and with `always` stops asking about it as Claude
    // proposes.
    Q_INVOKABLE void allow(int id, bool always);
    // Refuses the tool. With a message the agent goes on with it; without,
    // it stops and waits, as after "No" in Claude's own prompt.
    Q_INVOKABLE void deny(int id, const QString &message);
    // Back to the terminal: Claude asks there itself. For a turn's last
    // question, just off the stage.
    Q_INVOKABLE void pass(int id);
    // Answers a turn's last question: the text, as one line, is typed into
    // the agent's pane (typeRequested).
    Q_INVOKABLE void replyWith(int id, const QString &text);
    // Whether turns ending with a question come on stage (ask_replies = no
    // turns it off).
    void setReplies(bool on) { m_replies = on; }
    static bool readReplies(const QString &configFile);

    // What a permission is about, in a line or a few (a command, a file).
    static QString detail(const QString &tool, const QJsonObject &input);
    // What "always" would allow from Claude's suggestions, or empty.
    static QString alwaysWhat(const QJsonArray &suggestions);

public slots:
    // Every listing of the agents, as herdr reported it.
    void agentsListed(const QList<Agent> &agents);

signals:
    void currentChanged();
    void waitingChanged();
    void askingChanged();
    // A new request arrived for the stage.
    void arrived();
    // A request came from an agent the last listing doesn't know: list again.
    void listingWanted();
    // Type this answer into the agent's pane, if it still is that session.
    void typeRequested(const QString &pane, const QString &session, const QString &text);

private:
    struct Request {
        int id = 0;
        QPointer<QLocalSocket> socket;
        QJsonObject ask;
        bool seenBlocked = false;
        qint64 missingSince = 0;
        QTimer *hold = nullptr;
    };
    void accept(QLocalSocket *socket, const QJsonObject &ask);
    void admit(QLocalSocket *socket, const QJsonObject &ask);
    bool known(const QJsonObject &ask) const;
    struct Unsure {
        int token = 0;
        QPointer<QLocalSocket> socket;
        QJsonObject ask;
    };
    QList<Unsure> m_unsure;
    void reply(int id, const QJsonObject &output);
    void remove(int id);
    int indexOf(int id) const;
    void changed(const QVariantMap &firstBefore, int countBefore, const QSet<QString> &askingBefore);

    QLocalServer *m_server = nullptr;
    QList<Request> m_requests;
    QHash<QString, Agent> m_agents;
    std::function<bool(const QString &)> m_watching;
    int m_hold = 180;
    QString m_look = QStringLiteral("fence");
    bool m_replies = true;
    int m_nextId = 1;
};
