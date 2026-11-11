#include "agentmodel.h"

#include <QSet>

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
    // Rows are removed, moved, inserted, and updated in place rather than
    // reset, so the list keeps its scroll position and nothing flickers.
    const int before = m_agents.size();
    QSet<QString> wanted;
    for (const auto &agent : agents)
        wanted.insert(agent.paneId);
    for (int i = m_agents.size() - 1; i >= 0; --i) {
        if (wanted.contains(m_agents.at(i).paneId))
            continue;
        beginRemoveRows({}, i, i);
        m_agents.removeAt(i);
        endRemoveRows();
    }

    for (int i = 0; i < agents.size(); ++i) {
        const Agent &agent = agents.at(i);
        if (i >= m_agents.size() || m_agents.at(i).paneId != agent.paneId) {
            int from = -1;
            for (int j = i + 1; j < m_agents.size(); ++j)
                if (m_agents.at(j).paneId == agent.paneId) {
                    from = j;
                    break;
                }
            if (from >= 0) {
                beginMoveRows({}, from, from, {}, i);
                m_agents.move(from, i);
                endMoveRows();
            } else {
                beginInsertRows({}, i, i);
                m_agents.insert(i, agent);
                endInsertRows();
                continue;
            }
        }
        if (!(m_agents.at(i) == agent)) {
            m_agents[i] = agent;
            emit dataChanged(index(i), index(i));
        }
    }

    if (m_agents.size() != before)
        emit countChanged();
}
