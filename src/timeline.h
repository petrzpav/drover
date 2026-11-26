#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
#include <memory>

// What a Claude Code session did, from its transcript (one JSON object per
// line): the user's prompts, each followed by the points worth knowing about
// what the agent did for it. Reading files and searching stay out; commands
// in a row become one point, failures, commits, pushes and releases a point
// of their own, as do the agent's questions with their answers, what it
// said, and Claude's own recaps.
class TimelineParser {
public:
    struct Item {
        // command, milestone, edit, question, agent, web, tool, said (the
        // agent's words), nudge (the user's words mid-turn), recap, compact,
        // interrupted
        QString kind;
        QString text;
        // An answer, or why it failed.
        QString detail;
        // "" (finished), "running", "failed" or "denied".
        QString state;
        // The commands of a run, or the files edited, oldest first.
        QStringList steps;
        int pending = 0;
        QDateTime time;
        // Stays with the point as points around it come and go.
        qint64 id = 0;
    };
    struct Turn {
        qint64 serial = 0;
        // Empty for what came before the first prompt in view.
        QString prompt;
        QDateTime time;
        QList<Item> items;
        qint64 durationMs = 0;
    };

    // Turns kept; older ones drop off the front.
    static constexpr int MaxTurns = 40;

    void feed(const QByteArray &line);
    const QList<Turn> &turns() const { return m_turns; }
    // What the agent does this moment (a tool it waits for), or empty.
    QString activity() const;
    // The last thing the agent said in the last turn.
    QString lastSaid() const;

    // The words a Bash command stands for: its description, or its first line.
    static QString commandText(const QJsonObject &input);
    // Whether a command changes the world beyond the folder: commits,
    // pushes, releases, pull requests, deploys.
    static bool isMilestone(const QString &command);
    // Such a command in words: its description, or what it commits, pushes
    // or releases.
    static QString milestoneText(const QJsonObject &input);
    // An MCP tool's name for people: "Linear: save issue".
    static QString toolLabel(const QString &name, const QJsonObject &input);

private:
    struct Pending {
        qint64 serial;
        int index;
        QString activity;
        // Which command of a run it is.
        int step = -1;
    };
    Turn &current(const QDateTime &time);
    void startTurn(const QString &prompt, const QDateTime &time);
    void addItem(Item item, const QString &toolId, const QString &activity);
    void toolUse(const QJsonObject &block, const QDateTime &time);
    void toolResult(const QString &id, bool error, const QString &content, const QJsonValue &result);
    Turn *turn(qint64 serial);
    void dropPending();

    QList<Turn> m_turns;
    qint64 m_nextSerial = 1, m_nextItem = 1;
    QHash<QString, Pending> m_pending;
    QStringList m_pendingOrder;
};

// The selected agent's timeline for the panel, one row per turn. It reads the
// end of the transcript once (off the main thread, as transcripts grow to
// hundreds of megabytes) and then only what is appended, while it is shown.
class Timeline : public QAbstractListModel {
    Q_OBJECT
    // The Claude session to follow; empty for none.
    Q_PROPERTY(QString session READ session WRITE setSession NOTIFY sessionChanged)
    // Whether anyone looks: only then is the transcript read.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // "none" (no session), "missing" (no transcript yet), "loading", "ready".
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY progressChanged)
    Q_PROPERTY(QString lastSaid READ lastSaid NOTIFY progressChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { SerialRole = Qt::UserRole + 1, PromptRole, TimeRole, ItemsRole, DurationRole };

    explicit Timeline(QObject *parent = nullptr);
    ~Timeline() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString session() const { return m_session; }
    void setSession(const QString &session);
    bool active() const { return m_active; }
    void setActive(bool active);
    QString state() const { return m_state; }
    QString activity() const { return m_activity; }
    QString lastSaid() const { return m_lastSaid; }
    int count() const { return m_rows.size(); }

    // Where Claude keeps its transcripts, ~/.claude/projects; tests use their own.
    void setProjectsDirectory(const QString &directory) { m_projects = directory; }
    // How often the transcript is checked for more, in milliseconds.
    void setInterval(int milliseconds) { m_poll.setInterval(milliseconds); }
    // How much of a transcript's end is read at first.
    void setTailBytes(qint64 bytes) { m_tailBytes = bytes; }

    // Reads the transcript again from its end (its texts in a new language).
    Q_INVOKABLE void reload() { restart(); }

    // The transcript of a session: <projects>/<any project>/<session>.jsonl.
    static QString findTranscript(const QString &projects, const QString &session);

signals:
    void sessionChanged();
    void activeChanged();
    void stateChanged();
    void progressChanged();
    void countChanged();

private:
    struct Loaded {
        int generation = 0;
        TimelineParser parser;
        qint64 offset = 0;
        QByteArray partial;
    };
    // Stops early once another load is wanted (latest is no longer generation).
    static Loaded loadTail(const QString &path, qint64 tailBytes, int generation,
                           std::shared_ptr<std::atomic_int> latest);
    void restart();
    void poll();
    void publish();
    void setState(const QString &state);
    static QVariantMap rowOf(const TimelineParser::Turn &turn);

    QString m_session, m_path, m_state = QStringLiteral("none");
    QString m_projects;
    bool m_active = false;
    int m_generation = 0;
    bool m_loading = false;
    qint64 m_offset = 0, m_tailBytes = 16 * 1024 * 1024;
    QByteArray m_partial;
    TimelineParser m_parser;
    QList<QVariantMap> m_rows;
    QString m_activity, m_lastSaid;
    QTimer m_poll;
    // Agents passed by quickly aren't read: only where the selection settles.
    QTimer m_settle;
    std::shared_ptr<std::atomic_int> m_latest = std::make_shared<std::atomic_int>(0);
    QFutureWatcher<Loaded> m_load;
};
