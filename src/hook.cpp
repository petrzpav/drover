#include "hook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSaveFile>
#include <unistd.h>

namespace Hook {

QString serverName() {
    // A full path in the user's runtime folder: a bare name would land in
    // the temporary folder, which an agent's environment may move elsewhere.
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/%1").arg(getuid());
    return runtime + QStringLiteral("/drover-ask");
}

QJsonObject request(const QJsonObject &input, const QHash<QString, QString> &env) {
    const QString pane = env.value("HERDR_PANE_ID");
    if (pane.isEmpty())
        return {};
    // A subagent's tool call: its parent agent is the one in the pane.
    if (!input.value("agent_id").toString().isEmpty())
        return {};
    const QString event = input.value("hook_event_name").toString();
    const QString tool = input.value("tool_name").toString();
    const QJsonObject toolInput = input.value("tool_input").toObject();
    QString kind;
    if (event == "PreToolUse" && tool == "AskUserQuestion") {
        const QJsonArray questions = toolInput.value("questions").toArray();
        if (questions.isEmpty())
            return {};
        for (const auto &question : questions) {
            const QJsonObject q = question.toObject();
            const QJsonArray options = q.value("options").toArray();
            if (q.value("question").toString().isEmpty() || options.isEmpty())
                return {};
            // Previews (mockups, code to compare) need Claude's own dialog.
            for (const auto &option : options)
                if (option.toObject().contains("preview"))
                    return {};
        }
        kind = "question";
    } else if (event == "PermissionRequest" && !tool.isEmpty()) {
        kind = "permission";
    } else {
        return {};
    }
    return QJsonObject{
        {"kind", kind},
        {"pane", pane},
        {"session", input.value("session_id").toString()},
        {"cwd", input.value("cwd").toString()},
        {"tool", tool},
        {"input", toolInput},
        {"suggestions", input.value("permission_suggestions").toArray()},
    };
}

QByteArray run(const QByteArray &input, const QHash<QString, QString> &env, const QString &server,
               int connectTimeout) {
    const QJsonObject hookInput = QJsonDocument::fromJson(input).object();
    const QJsonObject ask = request(hookInput, env);
    if (ask.isEmpty())
        return {};
    QLocalSocket socket;
    socket.connectToServer(server);
    if (!socket.waitForConnected(connectTimeout))
        return {};
    socket.write(QJsonDocument(ask).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(1000))
        return {};
    // Drover answers whenever the user does; Claude's own timeout or the user
    // pressing Esc in the terminal ends the wait from the other side.
    QByteArray reply;
    while (!reply.contains('\n')) {
        if (!socket.waitForReadyRead(-1))
            return {};
        reply += socket.readAll();
    }
    const QJsonObject answer = QJsonDocument::fromJson(reply.left(reply.indexOf('\n'))).object();
    const QJsonObject output = answer.value("output").toObject();
    if (output.isEmpty())
        return {};
    return QJsonDocument(output).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject questionOutput(const QJsonObject &toolInput, const QStringList &answers) {
    QJsonObject updated = toolInput;
    QJsonObject byQuestion;
    const QJsonArray questions = toolInput.value("questions").toArray();
    for (int i = 0; i < questions.size() && i < answers.size(); ++i)
        byQuestion.insert(questions.at(i).toObject().value("question").toString(), answers.at(i));
    updated.insert("answers", byQuestion);
    return QJsonObject{{"hookSpecificOutput", QJsonObject{
        {"hookEventName", "PreToolUse"},
        {"permissionDecision", "allow"},
        {"updatedInput", updated},
    }}};
}

QJsonObject allowOutput(const QJsonArray &permissions) {
    QJsonObject decision{{"behavior", "allow"}};
    if (!permissions.isEmpty())
        decision.insert("updatedPermissions", permissions);
    return QJsonObject{{"hookSpecificOutput", QJsonObject{
        {"hookEventName", "PermissionRequest"},
        {"decision", decision},
    }}};
}

QJsonObject denyOutput(const QString &message, bool interrupt) {
    return QJsonObject{{"hookSpecificOutput", QJsonObject{
        {"hookEventName", "PermissionRequest"},
        {"decision", QJsonObject{{"behavior", "deny"}, {"message", message}, {"interrupt", interrupt}}},
    }}};
}

namespace {

// How long Claude lets the hook wait, in seconds. Drover decides when to give
// a question back to the terminal; this only ends a hook nobody answers.
constexpr int waitLimit = 86400;

const char *const events[] = {"PreToolUse", "PermissionRequest"};

bool isOurs(const QJsonValue &group) {
    for (const auto &hook : group.toObject().value("hooks").toArray())
        if (hook.toObject().value("command").toString().contains("drover hook"))
            return true;
    return false;
}

QString load(const QString &path, QJsonObject *settings) {
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return QObject::tr("Can't read %1.").arg(path);
    const QByteArray data = file.readAll();
    if (data.trimmed().isEmpty())
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return QObject::tr("%1 isn't valid JSON, so drover leaves it alone.").arg(path);
    *settings = document.object();
    return {};
}

// Claude writes its settings with two spaces; Qt indents with four.
QByteArray twoSpaces(const QByteArray &json) {
    QByteArray out;
    for (const QByteArray &line : json.split('\n')) {
        int spaces = 0;
        while (spaces < line.size() && line.at(spaces) == ' ')
            ++spaces;
        out += QByteArray(spaces / 2, ' ') + line.mid(spaces) + '\n';
    }
    out.chop(1);
    return out;
}

QString save(const QString &path, const QJsonObject &settings) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFile::exists(path)) {
        QFile::remove(path + ".drover-backup");
        QFile::copy(path, path + ".drover-backup");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QObject::tr("Can't write %1.").arg(path);
    file.write(twoSpaces(QJsonDocument(settings).toJson(QJsonDocument::Indented)));
    return file.commit() ? QString() : QObject::tr("Can't write %1.").arg(path);
}

QJsonObject withoutOurs(QJsonObject settings) {
    QJsonObject hooks = settings.value("hooks").toObject();
    for (const char *event : events) {
        QJsonArray kept;
        for (const auto &group : hooks.value(event).toArray())
            if (!isOurs(group))
                kept.append(group);
        if (kept.isEmpty())
            hooks.remove(event);
        else
            hooks.insert(event, kept);
    }
    if (hooks.isEmpty())
        settings.remove("hooks");
    else
        settings.insert("hooks", hooks);
    return settings;
}

} // namespace

QString install(const QString &settingsFile) {
    QJsonObject settings;
    if (const QString error = load(settingsFile, &settings); !error.isEmpty())
        return error;
    settings = withoutOurs(settings);
    QJsonObject hooks = settings.value("hooks").toObject();
    const QJsonObject hook{{"type", "command"}, {"command", command}, {"timeout", waitLimit}};
    const QJsonObject matchers{{"PreToolUse", "AskUserQuestion"}, {"PermissionRequest", "*"}};
    for (const char *event : events) {
        QJsonArray groups = hooks.value(event).toArray();
        groups.append(QJsonObject{{"matcher", matchers.value(event)}, {"hooks", QJsonArray{hook}}});
        hooks.insert(event, groups);
    }
    settings.insert("hooks", hooks);
    return save(settingsFile, settings);
}

QString uninstall(const QString &settingsFile) {
    QJsonObject settings;
    if (const QString error = load(settingsFile, &settings); !error.isEmpty())
        return error;
    if (!installed(settingsFile))
        return {};
    return save(settingsFile, withoutOurs(settings));
}

bool installed(const QString &settingsFile) {
    QJsonObject settings;
    if (!load(settingsFile, &settings).isEmpty())
        return false;
    const QJsonObject hooks = settings.value("hooks").toObject();
    for (const char *event : events) {
        bool found = false;
        for (const auto &group : hooks.value(event).toArray())
            found = found || isOurs(group);
        if (!found)
            return false;
    }
    return true;
}

} // namespace Hook
