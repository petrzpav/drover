#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// One coding agent herdr recognized in one of its panes.
struct Agent {
    QString paneId;
    QString title;      // the agent's terminal title, or its name when it has one
    QString kind;       // claude, codex, ...
    QString status;     // working, blocked, done, idle, unknown
    QString workspace;  // the herdr workspace label
    QString workspaceId;
    QString terminalId; // herdr's id of the pane's terminal, for attaching
    QString cwd;        // with the home directory shortened to ~
    QString tabId;
    QString name;          // given with `herdr agent start` or `rename`
    QString terminalTitle; // what the agent itself calls its terminal
    QString sessionId;     // the agent's own session (Claude's session id)
    bool focused = false;  // herdr's focused pane

    bool operator==(const Agent &other) const = default;
};

// The live agent list for QML. Updates are applied row by row (removed,
// moved, inserted, changed), so the list doesn't flicker or lose its scroll
// position on any poll.
class AgentModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { PaneIdRole = Qt::UserRole + 1, TitleRole, KindRole, StatusRole, WorkspaceRole, CwdRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_agents.size(); }
    const QList<Agent> &agents() const { return m_agents; }
    int indexOf(const QString &paneId) const;

    void setAgents(const QList<Agent> &agents);

signals:
    void countChanged();

private:
    QList<Agent> m_agents;
};
