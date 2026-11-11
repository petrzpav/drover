#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

#include "agentmodel.h"

// Watches the agents of the local herdr session through the herdr CLI and
// talks to them: read their screen, send a prompt, answer a question, or bring
// the herdr window to the agent.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(AgentModel *agents READ agents CONSTANT)
    Q_PROPERTY(int working READ working NOTIFY countsChanged)
    Q_PROPERTY(int blocked READ blocked NOTIFY countsChanged)
    Q_PROPERTY(int done READ done NOTIFY countsChanged)
    Q_PROPERTY(int idle READ idle NOTIFY countsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedStatus READ selectedStatus NOTIFY selectionChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    // Tests pass fake herdr and hyprctl scripts; empty means the ones on PATH.
    explicit Backend(const QString &herdr = {}, const QString &hyprctl = {}, QObject *parent = nullptr);

    AgentModel *agents() { return &m_model; }
    int working() const { return m_counts.value("working"); }
    int blocked() const { return m_counts.value("blocked"); }
    int done() const { return m_counts.value("done"); }
    int idle() const { return m_counts.value("idle") + m_counts.value("unknown"); }
    QString error() const { return m_error; }
    QString selected() const { return m_selected; }
    int selectedIndex() const { return m_model.indexOf(m_selected); }
    QString selectedTitle() const;
    QString selectedStatus() const;
    QString output() const { return m_output; }
    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeIsError; }
    bool active() const { return m_active; }

    // While active (the panel is open) the selected agent's screen is read too.
    void setActive(bool active);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString &paneId);
    Q_INVOKABLE void selectNext(int step);
    Q_INVOKABLE void prompt(const QString &text);
    Q_INVOKABLE void sendKeys(const QStringList &keys);
    Q_INVOKABLE void jump();

    static QHash<QString, QString> parseWorkspaces(const QByteArray &json);
    static QList<Agent> parseAgents(const QByteArray &json, const QHash<QString, QString> &workspaces,
                                    const QString &home, QString *error = nullptr);
    static QString cleanOutput(const QString &screen, int maxLines = 60);
    static QString commandError(const QByteArray &out, const QByteArray &err, int code);
    static QString errorCode(const QByteArray &out, const QByteArray &err);

signals:
    void countsChanged();
    void errorChanged();
    void selectionChanged();
    void outputChanged();
    void noticeChanged();
    void activeChanged();
    void jumped();
    void promptSent();

private:
    using Done = std::function<void(int code, const QByteArray &out, const QByteArray &err)>;
    void run(const QString &program, const QStringList &arguments, Done done, int timeout = 4000);
    void setError(const QString &error);
    void setNotice(const QString &notice, bool isError);
    void setOutput(const QString &output);
    void readSelected();
    void readScreen(const QString &pane, const QString &source);
    void applyAgents(const QList<Agent> &agents);

    QString m_herdr, m_hyprctl;
    AgentModel m_model;
    QHash<QString, int> m_counts;
    QHash<QString, QString> m_workspaces;
    QString m_error, m_selected, m_output, m_notice;
    bool m_noticeIsError = false, m_active = false, m_listing = false, m_reading = false;
    QTimer m_poll, m_readPoll, m_noticeTimer;
};
