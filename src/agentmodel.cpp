#include "agentmodel.h"

int AgentModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_agents.size();
}

QVariant AgentModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_agents.size())
        return {};
    const Agent &agent = m_agents.at(index.row());
    switch (role) {
    case PaneIdRole: return agent.paneId;
    case TitleRole: return agent.title;
    case KindRole: return agent.kind;
    case StatusRole: return agent.status;
    case WorkspaceRole: return agent.workspace;
    case CwdRole: return agent.cwd;
    }
    return {};
}

QHash<int, QByteArray> AgentModel::roleNames() const {
    return {{PaneIdRole, "paneId"}, {TitleRole, "title"},         {KindRole, "kind"},
            {StatusRole, "status"}, {WorkspaceRole, "workspace"}, {CwdRole, "cwd"}};
}

int AgentModel::indexOf(const QString &paneId) const {
    for (int i = 0; i < m_agents.size(); ++i)
        if (m_agents.at(i).paneId == paneId)
            return i;
    return -1;
}

void AgentModel::setAgents(const QList<Agent> &agents) {
    bool samePanes = agents.size() == m_agents.size();
    for (int i = 0; samePanes && i < agents.size(); ++i)
        samePanes = agents.at(i).paneId == m_agents.at(i).paneId;

    if (samePanes) {
        // The common case: statuses and titles change, the panes stay.
        for (int i = 0; i < agents.size(); ++i) {
            if (agents.at(i) == m_agents.at(i))
                continue;
            m_agents[i] = agents.at(i);
            emit dataChanged(index(i), index(i));
        }
        return;
    }

    // Agents came or went, which is rare enough that a reset is fine.
    beginResetModel();
    m_agents = agents;
    endResetModel();
    emit countChanged();
}
