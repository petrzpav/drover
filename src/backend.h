#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "agentmodel.h"

// Watches the agents of the local herdr session through the herdr CLI, starts
// new ones, and brings the herdr window to one. Talking to an agent happens in
// the panel's terminal, attached to the agent's pane (`herdr terminal attach`).
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(AgentModel *agents READ agents CONSTANT)
    Q_PROPERTY(int working READ working NOTIFY countsChanged)
    Q_PROPERTY(int blocked READ blocked NOTIFY countsChanged)
    Q_PROPERTY(int done READ done NOTIFY countsChanged)
    Q_PROPERTY(int idle READ idle NOTIFY countsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedInfoChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectedInfoChanged)
    Q_PROPERTY(QString selectedStatus READ selectedStatus NOTIFY selectedInfoChanged)
    // The herdr terminal of the selected agent's pane, for attaching to it.
    Q_PROPERTY(QString selectedTerminal READ selectedTerminal NOTIFY selectedInfoChanged)
    // herdr's full path, for the terminal to run; empty when it isn't installed.
    Q_PROPERTY(QString herdrPath READ herdr NOTIFY errorChanged)
    // The selected agent's pane closed; nothing is selected until the user
    // picks another, so a draft never goes to an agent they didn't choose.
    Q_PROPERTY(bool selectionLost READ selectionLost NOTIFY selectedChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)
    // The action waiting for herdr: "open", "start", "close", or empty.
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    // herdr's workspaces, by label: [{id, label, agents}], for the picker.
    Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY workspacesChanged)
    // The agent the picker starts, from ~/.config/drover/config (agent = ...).
    Q_PROPERTY(QString agentKind READ agentKind CONSTANT)

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
    QString selectedTerminal() const;
    bool selectionLost() const { return m_selectionLost; }
    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeIsError; }
    QString busy() const { return m_busy; }
    QVariantList workspaces() const { return m_workspaceList; }
    QString agentKind() const { return m_agentKind; }
    void setAgentKind(const QString &kind) { m_agentKind = kind; }
    static QString readAgentKind(const QString &configFile);
    // A name herdr accepts for an agent in this workspace.
    static QString agentName(const QString &label, const QString &suffix);

    // How long a herdr or hyprctl call may take; tests shorten it.
    void setTimeout(int milliseconds) { m_timeout = milliseconds; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString &paneId);
    Q_INVOKABLE void selectNext(int step);
    Q_INVOKABLE void jump();
    // Opens a tab in the workspace and starts the default agent in it, then
    // selects that agent.
    Q_INVOKABLE void startAgent(const QString &workspaceId);
    // What the picker would create for this text: {label, cwd, folder}, with
    // folder shown as ~/… ; an empty map for blank text.
    Q_INVOKABLE QVariantMap newWorkspace(const QString &text) const;
    // Creates a workspace from the picker's text and starts the default agent
    // in its first pane, then selects that agent.
    Q_INVOKABLE void createWorkspace(const QString &text);
    // Closes a workspace and everything running in it.
    Q_INVOKABLE void closeWorkspace(const QString &workspaceId);

    // A typed path that is a folder becomes the workspace's folder, named
    // after it; anything else is a name for a workspace in the home folder.
    static QVariantMap resolveNewWorkspace(const QString &text, const QString &home);

    static QHash<QString, QString> parseWorkspaces(const QByteArray &json);
    static QList<Agent> parseAgents(const QByteArray &json, const QHash<QString, QString> &workspaces,
                                    const QString &home, QString *error = nullptr);
    static QString commandError(const QByteArray &out, const QByteArray &err, int code);
    static QString errorCode(const QByteArray &out, const QByteArray &err);

    // Exit code run() reports when a command took too long and was stopped.
    static constexpr int TimedOut = -2;

signals:
    void countsChanged();
    void errorChanged();
    // A different pane is selected.
    void selectedChanged();
    // The selected agent's title, status, or place in the list changed.
    void selectedInfoChanged();
    void noticeChanged();
    void busyChanged();
    void workspacesChanged();
    void jumped();

private:
    using Done = std::function<void(int code, const QByteArray &out, const QByteArray &err)>;
    void run(const QString &program, const QStringList &arguments, Done done, int timeout = -1);
    void launchAgent(const QString &pane, const QString &label, int attempt);
    void updateWorkspaceList();
    void setError(const QString &error);
    void setNotice(const QString &notice, bool isError);
    void setBusy(const QString &busy);
    void applyAgents(const QList<Agent> &agents);
    void raiseHerdr();
    QString herdr() const;
    QString hyprctl() const;

    QString m_herdrOverride, m_hyprctlOverride;
    AgentModel m_model;
    QHash<QString, int> m_counts;
    QHash<QString, QString> m_workspaces;
    QString m_error, m_selected, m_notice, m_busy;
    QVariantList m_workspaceList;
    QString m_agentKind, m_pendingSelect;
    bool m_noticeIsError = false, m_listing = false;
    bool m_selectionLost = false;
    int m_timeout = 4000;
    QTimer m_poll, m_noticeTimer;
};
