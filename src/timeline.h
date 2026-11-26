#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QProcess>
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
        // The prompt line's own id, which stays when the transcript is read again.
        QString uuid;
        // The agent finished it (it may still add a point or two after).
        bool ended = false;
    };

    // Turns kept; older ones drop off the front.
    static constexpr int MaxTurns = 40;

    void feed(const QByteArray &line);
    const QList<Turn> &turns() const { return m_turns; }
    // What the agent does this moment (a tool it waits for), or empty.
    QString activity() const;
    // The last thing the agent said in the last turn.
    QString lastSaid() const;
    // Words the agent's terminal shows for a point (index -1: the turn's
    // prompt), to find it there. The terminal folds commands and edits into
    // lines like "Ran 3 shell commands", so for those it is the nearest of
    // the agent's words before them. A short prompt said again later ("ok")
    // gives way to the agent's first words after it.
    QString needle(qint64 serial, int index) const;
    // The start of a Markdown text as the terminal shows it: its first 40
    // characters, without emphasis, code marks, list marks or link targets.
    static QString shownStart(const QString &markdown);
    // A turn's points from `from` up to `to`, numbered from 1, as plain text
    // for a model to summarize.
    QString digest(qint64 serial, int from, int to) const;
    const Turn *find(qint64 serial) const;

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
    void startTurn(const QString &prompt, const QDateTime &time, const QString &uuid = QString());
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
    enum Role { SerialRole = Qt::UserRole + 1, PromptRole, TimeRole, ItemsRole, DurationRole, PointsRole,
                CoveredRole, SummarizingRole };

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
    // See TimelineParser::needle.
    Q_INVOKABLE QString needle(qint64 serial, int index) const { return m_parser.needle(serial, index); }

    // The transcript of a session: <projects>/<any project>/<session>.jsonl.
    static QString findTranscript(const QString &projects, const QString &session);

    // Summaries: a model (claude -p) sums up each turn in a few points, in
    // batches as it goes on, each batch once (kept in the cache folder).
    // The model, "" for none; summary = ... in the config, sonnet when unset.
    void setSummaryModel(const QString &model) { m_model = model; }
    QString summaryModel() const { return m_model; }
    static QString readSummaryModel(const QString &configFile);
    // The claude program, and where summaries are kept; tests use their own.
    void setSummaryProgram(const QString &program) { m_program = program; }
    void setCacheDirectory(const QString &directory) { m_cache = directory; }
    // How many new points and seconds a turn still going on waits for its
    // next summary.
    void setBatching(int points, int seconds) { m_batchPoints = points; m_batchSeconds = seconds; }
    // A turn came into view: it is summed up too, not only the newest ones.
    Q_INVOKABLE void want(qint64 serial);
    // The instructions the model gets.
    static QString summaryInstructions(bool more);

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
    QVariantMap rowOf(const TimelineParser::Turn &turn) const;

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
    // Summaries.
    struct Batch {
        int to = 0;
        QVariantList points;
    };
    void loadSummaries();
    void saveSummaries(const QString &session, const QHash<QString, QList<Batch>> &summaries) const;
    void summarizeNext();
    void summaryDone(int code);
    void stopSummary();
    QString m_model, m_program = QStringLiteral("claude"), m_cache;
    QHash<QString, QList<Batch>> m_summaries;  // by prompt uuid, this session's
    QHash<QString, qint64> m_batchStarted;     // by prompt uuid
    QSet<QString> m_wanted;                    // prompt uuids of turns seen
    struct Job {
        QString session, uuid;
        qint64 serial = 0;
        int from = 0, to = 0;
        int generation = 0;
    } m_job;
    QPointer<QProcess> m_child;
    QTimer m_childTimeout;
    qint64 m_backoffUntil = 0;
    int m_batchPoints = 4, m_batchSeconds = 60;

    QTimer m_poll;
    // Agents passed by quickly aren't read: only where the selection settles.
    QTimer m_settle;
    std::shared_ptr<std::atomic_int> m_latest = std::make_shared<std::atomic_int>(0);
    QFutureWatcher<Loaded> m_load;
};
