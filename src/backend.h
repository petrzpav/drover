#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
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
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedInfoChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectedInfoChanged)
    Q_PROPERTY(QString selectedStatus READ selectedStatus NOTIFY selectedInfoChanged)
    // The selected agent's pane closed; nothing is selected until the user
    // picks another, so a draft never goes to an agent they didn't choose.
    Q_PROPERTY(bool selectionLost READ selectionLost NOTIFY selectedChanged)
    // The selected agent's screen as plain text, and as rich text in its
    // terminal colors.
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString outputHtml READ outputHtml NOTIFY outputChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)
    // The action waiting for herdr: "send", "keys", "open", or empty.
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
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
    bool selectionLost() const { return m_selectionLost; }
    QString output() const { return m_output; }
    QString outputHtml() const { return m_outputHtml; }
    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeIsError; }
    QString busy() const { return m_busy; }
    bool active() const { return m_active; }
    QVariantList workspaces() const { return m_workspaceList; }
    QString agentKind() const { return m_agentKind; }
    void setAgentKind(const QString &kind) { m_agentKind = kind; }
    static QString readAgentKind(const QString &configFile);
    // A name herdr accepts for an agent in this workspace.
    static QString agentName(const QString &label, const QString &suffix);

    // While active (the panel is open) the selected agent's screen is read too.
    void setActive(bool active);
    // How long a herdr or hyprctl call may take; tests shorten it.
    void setTimeout(int milliseconds) { m_timeout = milliseconds; }
    // The terminal's 16 basic colors, from the theme.
    void setPalette(const QStringList &palette);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString &paneId);
    Q_INVOKABLE void selectNext(int step);
    Q_INVOKABLE void prompt(const QString &text);
    Q_INVOKABLE void sendKeys(const QStringList &keys);
    Q_INVOKABLE void jump();
    // Opens a tab in the workspace and starts the default agent in it, then
    // selects that agent.
    Q_INVOKABLE void startAgent(const QString &workspaceId);

    static QHash<QString, QString> parseWorkspaces(const QByteArray &json);
    static QList<Agent> parseAgents(const QByteArray &json, const QHash<QString, QString> &workspaces,
                                    const QString &home, QString *error = nullptr);
    static QString cleanOutput(const QString &screen, int maxLines = 60);
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
    void outputChanged();
    void noticeChanged();
    void busyChanged();
    void activeChanged();
    void workspacesChanged();
    void jumped();
    // herdr took the message; the text is what was sent.
    void promptSent(const QString &text);

private:
    using Done = std::function<void(int code, const QByteArray &out, const QByteArray &err)>;
    void run(const QString &program, const QStringList &arguments, Done done, int timeout = -1);
    void launchAgent(const QString &pane, const QString &label, int attempt);
    void updateWorkspaceList();
    void setError(const QString &error);
    void setNotice(const QString &notice, bool isError);
    void setOutput(const QString &output);
    void setBusy(const QString &busy);
    void readSelected();
    void readScreen(const QString &pane, const QString &source);
    void applyAgents(const QList<Agent> &agents);
    void raiseHerdr();
    QString herdr();
    QString hyprctl();

    QString m_herdrOverride, m_hyprctlOverride;
    AgentModel m_model;
    QHash<QString, int> m_counts;
    QHash<QString, QString> m_workspaces;
    QString m_error, m_selected, m_output, m_outputRaw, m_outputHtml, m_notice, m_busy;
    QStringList m_palette;
    QVariantList m_workspaceList;
    QString m_agentKind, m_pendingSelect;
    bool m_noticeIsError = false, m_active = false, m_listing = false, m_reading = false;
    bool m_selectionLost = false;
    int m_timeout = 4000;
    QTimer m_poll, m_readPoll, m_noticeTimer;
};
