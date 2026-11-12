#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QString>

// The Claude Code hook that brings an agent's question to drover.
//
// Claude Code runs `drover hook` before it shows a question dialog
// (PreToolUse for AskUserQuestion) and while it shows a permission prompt
// (PermissionRequest). The hook hands the request to the running drover over
// a local socket and waits; drover answers with what the hook prints back
// (the user's answers, or allow/deny), or with nothing, and Claude shows its
// own dialog as if drover weren't there.
//
// The hook lives in the global Claude settings, so it runs in every Claude
// session, also those no one watches (scripts, `claude -p`). It always fails
// open: no herdr pane, no drover, an unexpected input, drover going away —
// it prints nothing and exits 0.
namespace Hook {

// The local socket drover listens on for questions.
QString serverName();

// Reads the hook input Claude Code gives on stdin and returns what to print
// on stdout (empty: let Claude ask itself). `env` is the hook's environment.
QByteArray run(const QByteArray &input, const QHash<QString, QString> &env, const QString &server = serverName(),
               int connectTimeout = 300);

// The request drover gets for a hook input, or an empty object when drover
// has nothing to do with it.
QJsonObject request(const QJsonObject &input, const QHash<QString, QString> &env);

// What the hook prints for an answer.
QJsonObject questionOutput(const QJsonObject &toolInput, const QStringList &answers);
QJsonObject allowOutput(const QJsonArray &permissions = {});
QJsonObject denyOutput(const QString &message, bool interrupt);

// Adds drover's hooks to a Claude settings file (or takes them out), keeping
// everything else in it. Returns an error message, or empty on success.
QString install(const QString &settingsFile);
QString uninstall(const QString &settingsFile);
// Whether the settings file has drover's hooks.
bool installed(const QString &settingsFile);

// The command Claude runs; a missing drover makes it do nothing.
inline constexpr const char *command = "command -v drover >/dev/null 2>&1 && exec drover hook; exit 0";

} // namespace Hook
