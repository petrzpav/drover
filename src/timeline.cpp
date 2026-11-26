#include "timeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("Timeline", text); }

QString shortened(QString text, int length) {
    text = text.trimmed();
    return text.size() > length ? text.left(length - 1).trimmed() + QStringLiteral("…") : text;
}

QString firstLine(const QString &text) {
    for (const QString &line : text.split('\n')) {
        if (!line.trimmed().isEmpty())
            return line.trimmed();
    }
    return {};
}

QString fileName(const QString &path) { return QFileInfo(path).fileName(); }

// The text of a message's content: a string, or its text blocks.
QString textOf(const QJsonValue &content) {
    if (content.isString())
        return content.toString();
    QStringList texts;
    for (const QJsonValue &block : content.toArray())
        if (block.toObject().value("type").toString() == QLatin1String("text"))
            texts << block.toObject().value("text").toString();
    return texts.join('\n');
}

// What went wrong, in a line: the first one that isn't an exit code.
QString errorLine(const QString &content) {
    static const QRegularExpression exitCode("^Exit code \\d+$");
    for (const QString &raw : content.split('\n')) {
        const QString line = raw.trimmed();
        if (!line.isEmpty() && !exitCode.match(line).hasMatch())
            return shortened(line, 200);
    }
    return shortened(content, 200);
}

// Tools that only look around; they show as what the agent does now, never
// as a point of their own.
bool quietTool(const QString &name) {
    static const QStringList quiet{"Read", "Grep", "Glob", "LS", "ToolSearch", "TodoWrite", "TaskOutput", "TaskStop",
                                   "TaskGet", "TaskList", "TaskCreate", "TaskUpdate", "Monitor", "ScheduleWakeup",
                                   "LSP", "NotebookRead", "EnterPlanMode", "CronList", "ListAgents"};
    return quiet.contains(name);
}

QString editText(const QStringList &files) {
    QStringList names;
    for (const QString &file : files)
        names << fileName(file);
    QString text = names.mid(0, 3).join(QStringLiteral(", "));
    if (names.size() > 3)
        text += QStringLiteral(" +%1").arg(names.size() - 3);
    return text;
}

} // namespace

QString TimelineParser::commandText(const QJsonObject &input) {
    const QString description = input.value("description").toString().trimmed();
    return shortened(description.isEmpty() ? firstLine(input.value("command").toString()) : description, 160);
}

bool TimelineParser::isMilestone(const QString &command) {
    static const QRegularExpression milestone(
        "\\bgit(\\s+-C\\s+\\S+)?\\s+(commit|push|tag|merge|cherry-pick|revert)\\b"
        "|\\bgh\\s+(release\\s+(create|upload|edit)|pr\\s+(create|merge|close|comment|review)"
        "|issue\\s+(create|close|comment)|repo\\s+create)\\b"
        "|\\bflow\\b[^|;&\\n]*(\\s-r\\b|--release|--hotfix|\\brelease\\b|\\bhotfix\\b)"
        "|\\b(npm|pnpm|yarn)\\s+publish\\b|\\bdocker\\s+push\\b|(?<![\\w/.-])deploy(?![\\w/.-])");
    return milestone.match(command).hasMatch();
}

QString TimelineParser::milestoneText(const QJsonObject &input) {
    const QString description = input.value("description").toString().trimmed();
    if (!description.isEmpty())
        return shortened(description, 160);
    const QString command = input.value("command").toString();
    static const QRegularExpression commit("\\bgit(\\s+-C\\s+\\S+)?\\s+commit\\b");
    static const QRegularExpression message("-[a-zA-Z]*m\\s+(?:\"\\$\\(cat\\s+<<-?\\s*'?\\w+'?\\s*\\n([^\\n]+)|\"([^\"\\n]+)|'([^'\\n]+))");
    static const QRegularExpression push("\\bgit(\\s+-C\\s+\\S+)?\\s+push\\b");
    static const QRegularExpression release("\\bgh\\s+release\\s+create\\s+(\\S+)");
    static const QRegularExpression pr("\\bgh\\s+pr\\s+(create|merge)\\b");
    QStringList parts;
    if (const auto found = commit.match(command); found.hasMatch()) {
        const auto said = message.match(command, found.capturedEnd());
        const QString text = said.hasMatch() ? said.captured(1) + said.captured(2) + said.captured(3) : QString();
        parts << (text.trimmed().isEmpty() ? tr("Commit") : tr("Commit: %1").arg(text.trimmed()));
    }
    if (push.match(command).hasMatch())
        parts << tr("Push");
    if (const auto found = release.match(command); found.hasMatch())
        parts << tr("Release %1").arg(found.captured(1));
    if (const auto found = pr.match(command); found.hasMatch())
        parts << (found.captured(1) == QLatin1String("create") ? tr("A pull request") : tr("A pull request merged"));
    return shortened(parts.isEmpty() ? firstLine(command) : parts.join(QStringLiteral(" · ")), 160);
}

QString TimelineParser::toolLabel(const QString &name, const QJsonObject &input) {
    if (name.startsWith(QLatin1String("mcp__"))) {
        const QStringList parts = name.mid(5).split(QStringLiteral("__"));
        QString server = parts.value(0);
        if (server.startsWith(QLatin1String("claude_ai_")))
            server = server.mid(10);
        if (server == QLatin1String("claude-in-chrome"))
            return QStringLiteral("Chrome");
        server.replace('_', ' ').replace('-', ' ');
        QString tool = parts.mid(1).join(QStringLiteral("__"));
        tool.replace('_', ' ');
        return tool.isEmpty() ? server : server + QStringLiteral(": ") + tool;
    }
    if (name == QLatin1String("Skill"))
        return QStringLiteral("Skill: ") + input.value("skill").toString();
    if (name == QLatin1String("SendMessage"))
        return tr("Message to %1").arg(input.value("to").toString());
    if (name == QLatin1String("Artifact"))
        return QStringLiteral("Artifact: ") + input.value("action").toString(QStringLiteral("publish"));
    return name;
}

TimelineParser::Turn *TimelineParser::turn(qint64 serial) {
    for (auto it = m_turns.rbegin(); it != m_turns.rend(); ++it)
        if (it->serial == serial)
            return &*it;
    return nullptr;
}

TimelineParser::Turn &TimelineParser::current(const QDateTime &time) {
    if (m_turns.isEmpty())
        startTurn(QString(), time);
    return m_turns.last();
}

void TimelineParser::startTurn(const QString &prompt, const QDateTime &time, const QString &uuid) {
    if (!m_turns.isEmpty())
        m_turns.last().ended = true;
    Turn turn;
    turn.uuid = uuid;
    turn.serial = m_nextSerial++;
    turn.prompt = prompt;
    turn.time = time;
    m_turns.append(turn);
    while (m_turns.size() > MaxTurns) {
        const qint64 gone = m_turns.takeFirst().serial;
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->serial == gone) {
                m_pendingOrder.removeAll(it.key());
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void TimelineParser::dropPending() {
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (Turn *owner = turn(it->serial); owner && it->index >= 0 && it->index < owner->items.size()) {
            Item &item = owner->items[it->index];
            item.pending = 0;
            if (item.state == QLatin1String("running"))
                item.state.clear();
        }
    }
    m_pending.clear();
    m_pendingOrder.clear();
}

void TimelineParser::addItem(Item item, const QString &toolId, const QString &activity) {
    Turn &turn = current(item.time);
    if (item.steps.isEmpty())
        item.steps << item.text;
    int index = -1, step = -1;
    // Commands, edits, web lookups and the same tool again, one after the
    // other, are one point; a failure ends the run.
    if (!turn.items.isEmpty()) {
        Item &last = turn.items.last();
        const bool runs = item.kind == QLatin1String("command") || item.kind == QLatin1String("edit")
                          || item.kind == QLatin1String("web")
                          || (item.kind == QLatin1String("tool") && last.text == item.text);
        if (runs && last.kind == item.kind && (last.state.isEmpty() || last.state == QLatin1String("running"))) {
            if (item.kind == QLatin1String("edit")) {
                for (const QString &file : item.steps)
                    if (!last.steps.contains(file))
                        last.steps << file;
                last.text = editText(last.steps);
            } else {
                last.steps << item.text;
                last.text = item.text;
                step = last.steps.size() - 1;
            }
            index = turn.items.size() - 1;
        }
    }
    if (index < 0) {
        if (item.kind == QLatin1String("edit"))
            item.text = editText(item.steps);
        else
            step = 0;
        item.id = m_nextItem++;
        turn.items.append(item);
        index = turn.items.size() - 1;
    }
    if (!toolId.isEmpty()) {
        Item &added = turn.items[index];
        ++added.pending;
        added.state = QStringLiteral("running");
        m_pending.insert(toolId, {turn.serial, index, activity, step});
        m_pendingOrder << toolId;
    }
}

void TimelineParser::toolUse(const QJsonObject &block, const QDateTime &time) {
    const QString name = block.value("name").toString();
    const QString id = block.value("id").toString();
    const QJsonObject input = block.value("input").toObject();
    if (quietTool(name)) {
        QString activity;
        if (name == QLatin1String("Read"))
            activity = tr("Reads %1").arg(fileName(input.value("file_path").toString()));
        else if (name == QLatin1String("Grep") || name == QLatin1String("Glob") || name == QLatin1String("LS"))
            activity = tr("Searches the files");
        else if (name == QLatin1String("Monitor") || name == QLatin1String("ScheduleWakeup"))
            activity = tr("Waits");
        if (!id.isEmpty()) {
            m_pending.insert(id, {current(time).serial, -1, activity});
            m_pendingOrder << id;
        }
        return;
    }
    Item item;
    item.time = time;
    QString activity;
    if (name == QLatin1String("Bash")) {
        const bool milestone = isMilestone(input.value("command").toString());
        item.text = milestone ? milestoneText(input) : commandText(input);
        item.kind = milestone ? QStringLiteral("milestone") : QStringLiteral("command");
        activity = item.text;
    } else if (name == QLatin1String("Edit") || name == QLatin1String("Write") || name == QLatin1String("MultiEdit")
               || name == QLatin1String("NotebookEdit")) {
        QString path = input.value(input.contains("notebook_path") ? "notebook_path" : "file_path").toString();
        if (path.startsWith(QDir::homePath() + '/'))
            path = '~' + path.mid(QDir::homePath().size());
        item.kind = QStringLiteral("edit");
        item.steps << path;
        activity = tr("Edits %1").arg(fileName(path));
    } else if (name == QLatin1String("AskUserQuestion")) {
        const QJsonArray questions = input.value("questions").toArray();
        item.kind = QStringLiteral("question");
        item.text = shortened(questions.first().toObject().value("question").toString(), 2000);
        if (questions.size() > 1)
            item.text += QStringLiteral(" (+%1)").arg(questions.size() - 1);
        activity = tr("Asks you");
    } else if (name == QLatin1String("ExitPlanMode")) {
        item.kind = QStringLiteral("question");
        item.text = tr("A plan for your approval");
        activity = tr("Asks you");
    } else if (name == QLatin1String("Agent") || name == QLatin1String("Task")) {
        item.kind = QStringLiteral("agent");
        item.text = shortened(input.value("description").toString(), 160);
        activity = tr("A subagent: %1").arg(item.text);
    } else if (name == QLatin1String("WebSearch")) {
        item.kind = QStringLiteral("web");
        item.text = shortened(input.value("query").toString(), 160);
        activity = tr("Searches the web");
    } else if (name == QLatin1String("WebFetch")) {
        item.kind = QStringLiteral("web");
        item.text = shortened(input.value("url").toString(), 160);
        activity = tr("Reads a web page");
    } else {
        item.kind = QStringLiteral("tool");
        item.text = toolLabel(name, input);
        activity = item.text;
    }
    addItem(item, id, activity);
}

void TimelineParser::toolResult(const QString &id, bool error, const QString &content, const QJsonValue &result) {
    if (!m_pending.contains(id))
        return;
    const Pending pending = m_pending.take(id);
    m_pendingOrder.removeAll(id);
    Turn *owner = turn(pending.serial);
    if (pending.index < 0 || !owner || pending.index >= owner->items.size())
        return;
    Item &item = owner->items[pending.index];
    item.pending = qMax(0, item.pending - 1);

    if (item.kind == QLatin1String("question")) {
        QStringList answers;
        const QJsonObject given = result.toObject().value("answers").toObject();
        for (auto it = given.begin(); it != given.end(); ++it)
            answers << it.value().toString();
        item.detail = shortened(!answers.isEmpty() ? answers.join(QStringLiteral("; ")) : firstLine(content), 2000);
    }
    if (!error) {
        if (item.pending == 0 && item.state == QLatin1String("running"))
            item.state.clear();
        return;
    }
    const bool denied = content.startsWith(QLatin1String("Permission for this action was denied"))
                        || content.contains(QLatin1String("doesn't want to proceed"))
                        || content.contains(QLatin1String("user denied"));
    const QString state = denied ? QStringLiteral("denied") : QStringLiteral("failed");
    const QString detail = item.kind == QLatin1String("question") && !denied ? item.detail : errorLine(content);
    // A command of a run failed: it becomes a point of its own, after the
    // run, which goes on with the others.
    if (item.steps.size() > 1 && item.kind != QLatin1String("edit") && pending.step >= 0
        && pending.step < item.steps.size()) {
        Item failed = item;
        failed.text = item.steps.takeAt(pending.step);
        failed.steps = {failed.text};
        failed.state = state;
        failed.detail = detail;
        failed.pending = 0;
        failed.id = m_nextItem++;
        item.text = item.steps.last();
        item.state = item.pending > 0 ? QStringLiteral("running") : QString();
        owner->items.insert(pending.index + 1, failed);
        for (auto &other : m_pending) {
            if (other.serial != pending.serial)
                continue;
            if (other.index > pending.index)
                ++other.index;
            else if (other.index == pending.index && other.step > pending.step)
                --other.step;
        }
        return;
    }
    item.state = state;
    item.detail = detail;
}

void TimelineParser::feed(const QByteArray &raw) {
    const QByteArray line = raw.trimmed();
    if (line.isEmpty())
        return;
    // Most lines are bookkeeping (requests, titles, modes) and some are
    // megabytes long: skip them before parsing.
    if (line.startsWith("{\"type\":\"")) {
        const QByteArray type = line.mid(9, 10);
        if (!type.startsWith("user\"") && !type.startsWith("assistant\"") && !type.startsWith("system\"")
            && !type.startsWith("attachment"))
            return;
    }
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    if (object.isEmpty() || object.value("isSidechain").toBool())
        return;
    const QString type = object.value("type").toString();
    const QDateTime time = QDateTime::fromString(object.value("timestamp").toString(), Qt::ISODateWithMs);
    const QJsonValue content = object.value("message").toObject().value("content");

    if (type == QLatin1String("assistant")) {
        for (const QJsonValue &value : content.toArray()) {
            const QJsonObject block = value.toObject();
            const QString kind = block.value("type").toString();
            if (kind == QLatin1String("tool_use")) {
                toolUse(block, time);
            } else if (kind == QLatin1String("text")) {
                const QString text = block.value("text").toString().trimmed();
                if (!text.isEmpty()) {
                    Item said;
                    said.kind = QStringLiteral("said");
                    said.text = shortened(text, 4000);
                    said.time = time;
                    addItem(said, QString(), QString());
                }
            }
        }
        return;
    }

    // What the user typed while the agent worked: it reads it mid-turn.
    if (type == QLatin1String("attachment")) {
        const QJsonObject attachment = object.value("attachment").toObject();
        if (attachment.value("type").toString() == QLatin1String("queued_command")
            && attachment.value("commandMode").toString() == QLatin1String("prompt")
            && attachment.value("origin").toObject().value("kind").toString() == QLatin1String("human")) {
            Item nudge;
            nudge.kind = QStringLiteral("nudge");
            nudge.text = shortened(textOf(attachment.value("prompt")), 4000);
            nudge.time = time;
            if (!nudge.text.isEmpty())
                addItem(nudge, QString(), QString());
        }
        return;
    }

    if (type == QLatin1String("system")) {
        const QString subtype = object.value("subtype").toString();
        if (subtype == QLatin1String("away_summary")) {
            static const QRegularExpression hint("\\s*\\([^()]*/config\\)\\s*$");
            Item recap;
            recap.kind = QStringLiteral("recap");
            recap.text = object.value("content").toString().remove(hint).trimmed();
            recap.time = time;
            if (!recap.text.isEmpty())
                addItem(recap, QString(), QString());
        } else if (subtype == QLatin1String("compact_boundary")) {
            Item compact;
            compact.kind = QStringLiteral("compact");
            compact.text = tr("The conversation was compacted");
            compact.time = time;
            addItem(compact, QString(), QString());
        } else if (subtype == QLatin1String("turn_duration") && !m_turns.isEmpty()) {
            m_turns.last().durationMs = qint64(object.value("durationMs").toDouble());
            m_turns.last().ended = true;
        }
        return;
    }

    if (type != QLatin1String("user") || object.value("isMeta").toBool() || object.value("isCompactSummary").toBool())
        return;

    if (content.isArray()) {
        bool results = false;
        for (const QJsonValue &value : content.toArray()) {
            const QJsonObject block = value.toObject();
            if (block.value("type").toString() == QLatin1String("tool_result")) {
                results = true;
                toolResult(block.value("tool_use_id").toString(), block.value("is_error").toBool(),
                           textOf(block.value("content")), object.value("toolUseResult"));
            }
        }
        if (results)
            return;
    }

    const QString text = textOf(content).trimmed();
    if (text.isEmpty())
        return;
    const QString uuid = object.value("uuid").toString();
    const QJsonObject origin = object.value("origin").toObject();
    const QString from = origin.value("kind").toString();
    if (!from.isEmpty() && from != QLatin1String("human"))
        return;
    if (text.startsWith(QLatin1String("[Request interrupted"))) {
        dropPending();
        Item stop;
        stop.kind = QStringLiteral("interrupted");
        stop.text = tr("You interrupted it");
        stop.time = time;
        addItem(stop, QString(), QString());
        return;
    }
    // Commands typed in Claude: /command, and ! for the shell.
    static const QRegularExpression command("^<command-name>(.*?)</command-name>.*?<command-args>(.*?)</command-args>",
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression shell("^<bash-input>(.*?)</bash-input>", QRegularExpression::DotMatchesEverythingOption);
    if (const auto match = command.match(text); match.hasMatch()) {
        startTurn((match.captured(1).trimmed() + ' ' + match.captured(2).trimmed()).trimmed(), time, uuid);
        return;
    }
    if (const auto match = shell.match(text); match.hasMatch()) {
        startTurn(QStringLiteral("! ") + match.captured(1).trimmed(), time, uuid);
        return;
    }
    // Everything else in angle brackets is Claude's own (command output,
    // reminders), unless the user typed it.
    if (text.startsWith('<') && from != QLatin1String("human"))
        return;
    startTurn(shortened(text, 4000), time, uuid);
}

QString TimelineParser::activity() const {
    for (auto it = m_pendingOrder.crbegin(); it != m_pendingOrder.crend(); ++it) {
        const QString text = m_pending.value(*it).activity;
        if (!text.isEmpty())
            return text;
    }
    return {};
}

QString TimelineParser::shownStart(const QString &markdown) {
    static const QRegularExpression link("\\[([^\\]]*)\\]\\([^)]*\\)");
    static const QRegularExpression marks("[*`]|(?<!\\w)_|_(?!\\w)");
    static const QRegularExpression lead("^\\s*(#+|>|[-*+]|\\d+\\.)\\s+");
    // Lines as far as 40 characters, up to a code block or a table, which
    // the terminal draws differently.
    QString text;
    for (QString line : markdown.split('\n')) {
        const QString bare = line.trimmed();
        if (bare.startsWith(QLatin1String("```")) || bare.startsWith('|'))
            break;
        line.replace(link, QStringLiteral("\\1"));
        line.remove(lead);
        line.remove(marks);
        text = (text + ' ' + line).simplified();
        if (text.size() >= 40)
            break;
    }
    return text.left(40).trimmed();
}

QString TimelineParser::needle(qint64 serial, int index) const {
    int at = -1;
    for (int i = 0; i < m_turns.size(); ++i)
        if (m_turns[i].serial == serial)
            at = i;
    if (at < 0)
        return {};
    const Turn &turn = m_turns[at];
    const auto words = [](const Item &item) {
        return item.kind == QLatin1String("said") || item.kind == QLatin1String("nudge");
    };
    for (int i = qMin(index, int(turn.items.size()) - 1); i >= 0; --i)
        if (words(turn.items[i]))
            return shownStart(turn.items[i].text);
    const QString prompt = shownStart(turn.prompt);
    bool again = false;
    for (int i = at + 1; i < m_turns.size() && prompt.size() < 16; ++i)
        again = again || shownStart(m_turns[i].prompt) == prompt;
    if (prompt.isEmpty() || again) {
        for (const Item &item : turn.items)
            if (words(item))
                return shownStart(item.text);
    }
    return prompt;
}

const TimelineParser::Turn *TimelineParser::find(qint64 serial) const {
    for (const Turn &turn : m_turns)
        if (turn.serial == serial)
            return &turn;
    return nullptr;
}

QString TimelineParser::digest(qint64 serial, int from, int to) const {
    const Turn *turn = find(serial);
    if (!turn)
        return {};
    QStringList lines;
    to = qMin(to, int(turn->items.size()));
    for (int i = qMax(0, from); i < to; ++i) {
        const Item &item = turn->items[i];
        QString line;
        if (item.kind == QLatin1String("command") || item.kind == QLatin1String("web") || item.kind == QLatin1String("tool")) {
            QStringList steps = item.steps;
            if (steps.size() > 12)
                steps = steps.mid(0, 4) + QStringList{QStringLiteral("…")} + steps.mid(steps.size() - 7);
            line = QStringLiteral("%1: %2").arg(item.kind == QLatin1String("command") ? "ran" : item.kind == QLatin1String("web") ? "looked up" : "used",
                                                steps.join(QStringLiteral("; ")));
        } else if (item.kind == QLatin1String("milestone")) {
            line = QStringLiteral("commit/push/release: ") + item.text;
        } else if (item.kind == QLatin1String("edit")) {
            QStringList names;
            for (const QString &file : item.steps)
                names << QFileInfo(file).fileName();
            line = QStringLiteral("edited: ") + names.join(QStringLiteral(", "));
        } else if (item.kind == QLatin1String("question")) {
            line = QStringLiteral("asked the user: ") + shortened(item.text, 300)
                   + (item.detail.isEmpty() ? QString() : QStringLiteral(" | answer: ") + shortened(item.detail, 200));
        } else if (item.kind == QLatin1String("said")) {
            line = QStringLiteral("said: ") + shortened(item.text, 700);
        } else if (item.kind == QLatin1String("nudge")) {
            line = QStringLiteral("the user added: ") + shortened(item.text, 300);
        } else if (item.kind == QLatin1String("recap")) {
            line = QStringLiteral("recap: ") + item.text;
        } else if (item.kind == QLatin1String("agent")) {
            line = QStringLiteral("started a subagent: ") + item.text;
        } else {
            line = item.kind + QStringLiteral(": ") + item.text;
        }
        if (item.state == QLatin1String("failed") || item.state == QLatin1String("denied"))
            line += QStringLiteral(" [%1: %2]").arg(item.state, item.detail);
        lines << QStringLiteral("%1. %2").arg(i - from + 1).arg(QString(line).replace('\n', ' '));
    }
    return lines.join('\n');
}

QString TimelineParser::lastSaid() const {
    if (m_turns.isEmpty())
        return {};
    const auto &items = m_turns.last().items;
    for (auto it = items.crbegin(); it != items.crend(); ++it)
        if (it->kind == QLatin1String("said"))
            return it->text;
    return {};
}

Timeline::Timeline(QObject *parent)
    : QAbstractListModel(parent), m_projects(QDir::homePath() + "/.claude/projects") {
    m_poll.setInterval(800);
    connect(&m_poll, &QTimer::timeout, this, &Timeline::poll);
    // A turn going on may come due by time alone.
    connect(&m_poll, &QTimer::timeout, this, &Timeline::summarizeNext);
    m_settle.setSingleShot(true);
    m_settle.setInterval(150);
    connect(&m_settle, &QTimer::timeout, this, &Timeline::poll);
    m_cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/drover/summaries";
    m_childTimeout.setSingleShot(true);
    m_childTimeout.setInterval(120 * 1000);
    connect(&m_childTimeout, &QTimer::timeout, this, [this] {
        if (m_child)
            m_child->kill();
    });
    connect(&m_load, &QFutureWatcher<Loaded>::finished, this, [this] {
        if (!m_load.future().isValid() || m_load.future().resultCount() == 0)
            return;
        Loaded loaded = m_load.result();
        if (loaded.generation != m_generation)
            return;
        m_loading = false;
        m_parser = std::move(loaded.parser);
        m_offset = loaded.offset;
        m_partial = loaded.partial;
        publish();
        setState(QStringLiteral("ready"));
        if (m_active)
            poll();
    });
}

Timeline::~Timeline() {
    stopSummary();
    m_load.waitForFinished();
}

int Timeline::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QHash<int, QByteArray> Timeline::roleNames() const {
    return {{SerialRole, "serial"}, {PromptRole, "prompt"}, {TimeRole, "time"}, {ItemsRole, "items"},
            {DurationRole, "duration"}, {PointsRole, "points"}, {CoveredRole, "covered"},
            {SummarizingRole, "summarizing"}};
}

QVariant Timeline::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const auto names = roleNames();
    return m_rows.at(index.row()).value(QString::fromLatin1(names.value(role)));
}

QString Timeline::findTranscript(const QString &projects, const QString &session) {
    static const QRegularExpression valid("^[A-Za-z0-9_-]+$");
    if (!valid.match(session).hasMatch())
        return {};
    const QDir dir(projects);
    for (const QString &project : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(project + '/' + session + ".jsonl");
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

void Timeline::setSession(const QString &session) {
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
    restart();
}

void Timeline::setActive(bool active) {
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    if (m_active) {
        m_poll.start();
        poll();
    } else {
        m_poll.stop();
        m_settle.stop();
    }
}

void Timeline::setState(const QString &state) {
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

// Everything from the start: another session, or the transcript was replaced.
void Timeline::restart() {
    *m_latest = ++m_generation;
    m_loading = false;
    m_path.clear();
    m_offset = 0;
    m_partial.clear();
    m_parser = TimelineParser();
    loadSummaries();
    if (!m_rows.isEmpty()) {
        beginResetModel();
        m_rows.clear();
        endResetModel();
        emit countChanged();
    }
    if (!m_activity.isEmpty() || !m_lastSaid.isEmpty()) {
        m_activity.clear();
        m_lastSaid.clear();
        emit progressChanged();
    }
    setState(m_session.isEmpty() ? QStringLiteral("none") : QStringLiteral("missing"));
    if (m_active)
        m_settle.start();
}

Timeline::Loaded Timeline::loadTail(const QString &path, qint64 tailBytes, int generation,
                                    std::shared_ptr<std::atomic_int> latest) {
    Loaded loaded;
    loaded.generation = generation;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return loaded;
    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - tailBytes);
    file.seek(start);
    QByteArray data = file.read(size - start);
    loaded.offset = start + data.size();
    // Read from the middle of a line: it starts after the next newline.
    if (start > 0) {
        const int newline = data.indexOf('\n');
        data = newline < 0 ? QByteArray() : data.mid(newline + 1);
    }
    const int end = data.lastIndexOf('\n');
    loaded.partial = data.mid(end + 1);
    data.truncate(end + 1);
    int lines = 0;
    for (const QByteArray &line : data.split('\n')) {
        if (++lines % 200 == 0 && latest->load() != generation)
            return loaded;
        loaded.parser.feed(line);
    }
    return loaded;
}

void Timeline::poll() {
    if (m_session.isEmpty() || m_loading || m_settle.isActive())
        return;
    if (m_path.isEmpty()) {
        m_path = findTranscript(m_projects, m_session);
        if (m_path.isEmpty()) {
            setState(QStringLiteral("missing"));
            return;
        }
    }
    const QFileInfo info(m_path);
    if (!info.exists()) {
        restart();
        return;
    }
    const qint64 size = info.size();
    // First, or after a gap too long to read through: the end afresh.
    if (m_state != QLatin1String("ready") || size < m_offset || size - m_offset > m_tailBytes) {
        m_loading = true;
        setState(QStringLiteral("loading"));
        m_load.setFuture(QtConcurrent::run(&Timeline::loadTail, m_path, m_tailBytes, m_generation, m_latest));
        return;
    }
    if (size == m_offset)
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(m_offset))
        return;
    QByteArray data = m_partial + file.read(size - m_offset);
    m_offset = size;
    const int end = data.lastIndexOf('\n');
    m_partial = data.mid(end + 1);
    data.truncate(end + 1);
    if (data.isEmpty())
        return;
    for (const QByteArray &line : data.split('\n'))
        m_parser.feed(line);
    publish();
}

QVariantMap Timeline::rowOf(const TimelineParser::Turn &turn) const {
    QVariantList items;
    for (const auto &item : turn.items)
        items << QVariantMap{{"id", item.id}, {"kind", item.kind}, {"text", item.text}, {"detail", item.detail},
                             {"state", item.state}, {"steps", item.steps}};
    // The summary's points stand in for the points they cover.
    QVariantList points;
    int covered = 0;
    for (const Batch &batch : m_summaries.value(turn.uuid)) {
        points += batch.points;
        covered = batch.to;
    }
    return {{"serial", turn.serial}, {"prompt", turn.prompt}, {"time", turn.time}, {"items", items},
            {"duration", turn.durationMs}, {"points", points}, {"covered", qMin(covered, int(turn.items.size()))},
            {"summarizing", m_child && m_job.uuid == turn.uuid && !turn.uuid.isEmpty()}};
}

QString Timeline::readSummaryModel(const QString &configFile) {
    QFile file(configFile);
    if (file.open(QIODevice::ReadOnly)) {
        for (const QString &raw : QString::fromUtf8(file.readAll()).split('\n')) {
            const QString line = raw.section('#', 0, 0).trimmed();
            if (line.section('=', 0, 0).trimmed() != QLatin1String("summary"))
                continue;
            const QString value = line.section('=', 1).trimmed();
            static const QRegularExpression model("^[A-Za-z0-9._-]+$");
            if (value == QLatin1String("off") || value == QLatin1String("no"))
                return {};
            if (model.match(value).hasMatch())
                return value;
        }
    }
    return QStringLiteral("sonnet");
}

QString Timeline::summaryInstructions(bool more) {
    return QStringLiteral(
        "You summarize what a coding agent did during one turn, for its user who was not watching. "
        "You get the user's prompt and the agent's steps, numbered.\n")
        + (more ? QStringLiteral("You also get the points already written for the turn's earlier steps. Write 1 or 2 "
                                 "points about the new steps only, without repeating those.\n")
                : QStringLiteral("Write 2 to 4 key points.\n"))
        + QStringLiteral("Merge related steps into one point; fewer, broader points are better than a detailed log.\n")
        + QStringLiteral(
              "Write in the language of the user's prompt. Each point is one plain sentence of at most 100 "
              "characters, no Markdown. Lead with outcomes: what was decided, done, changed or shipped, what failed, "
              "what is open or waits for the user. Leave out routine steps such as reading files or tests that passed.\n"
              "Reply with JSON only: {\"points\":[{\"text\":\"...\",\"kind\":\"done|change|ship|fail|ask|open|note\","
              "\"step\":N}]} where step is the number of the step the point is mostly about.");
}

void Timeline::loadSummaries() {
    m_summaries.clear();
    m_batchStarted.clear();
    m_wanted.clear();
    if (m_session.isEmpty() || m_cache.isEmpty())
        return;
    QFile file(m_cache + '/' + m_session + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject turns = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = turns.begin(); it != turns.end(); ++it) {
        QList<Batch> batches;
        for (const QJsonValue &value : it.value().toArray()) {
            const QJsonObject object = value.toObject();
            batches.append({object.value("to").toInt(), object.value("points").toArray().toVariantList()});
        }
        m_summaries.insert(it.key(), batches);
    }
}

void Timeline::saveSummaries(const QString &session, const QHash<QString, QList<Batch>> &summaries) const {
    if (m_cache.isEmpty() || session.isEmpty())
        return;
    QJsonObject turns;
    for (auto it = summaries.begin(); it != summaries.end(); ++it) {
        QJsonArray batches;
        for (const Batch &batch : it.value())
            batches.append(QJsonObject{{"to", batch.to}, {"points", QJsonArray::fromVariantList(batch.points)}});
        turns.insert(it.key(), batches);
    }
    QDir().mkpath(m_cache);
    QSaveFile file(m_cache + '/' + session + ".json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(turns).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

// The newest turn that has steps no summary covers yet, enough of them: all
// of an ended turn's, or a batch of a turn still going on (its newest step
// may still grow, so it waits). A short ended turn needs none: its points
// say it already.
void Timeline::summarizeNext() {
    if (m_model.isEmpty() || !m_active || m_child || m_state != QLatin1String("ready")
        || QDateTime::currentMSecsSinceEpoch() < m_backoffUntil)
        return;
    const auto &turns = m_parser.turns();
    // The newest turns, and older ones once they come into view.
    int considered = 0;
    for (int i = int(turns.size()) - 1; i >= 0; --i) {
        const auto &turn = turns[i];
        if (turn.uuid.isEmpty() || turn.prompt.isEmpty())
            continue;
        if (++considered > 8 && !m_wanted.contains(turn.uuid))
            continue;
        const QList<Batch> batches = m_summaries.value(turn.uuid);
        const int covered = batches.isEmpty() ? 0 : batches.last().to;
        const int total = int(turn.items.size());
        const int target = turn.ended ? total : total - 1;
        if (target <= covered)
            continue;
        if (turn.ended) {
            if (covered == 0 && total < 3)
                continue;
        } else {
            const qint64 since = QDateTime::currentMSecsSinceEpoch() - m_batchStarted.value(turn.uuid, 0);
            if (target - covered < m_batchPoints || since < qint64(m_batchSeconds) * 1000)
                continue;
        }
        m_job = {m_session, turn.uuid, turn.serial, covered, target, m_generation};
        m_batchStarted.insert(turn.uuid, QDateTime::currentMSecsSinceEpoch());

        QString input = QStringLiteral("The user's prompt: ") + turn.prompt.left(1500) + QStringLiteral("\n\n");
        if (covered > 0) {
            input += QStringLiteral("Points already written:\n");
            for (const Batch &batch : batches)
                for (const QVariant &point : batch.points)
                    input += QStringLiteral("- ") + point.toMap().value("text").toString() + '\n';
            input += QStringLiteral("\nNew steps:\n");
        } else {
            input += QStringLiteral("Steps:\n");
        }
        input += m_parser.digest(turn.serial, covered, target);

        m_child = new QProcess(this);
        // Not one of the agents: none of their panes' or sessions' variables.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString &name : environment.keys())
            if (name.startsWith(QLatin1String("HERDR_")) || name.startsWith(QLatin1String("CLAUDE")))
                environment.remove(name);
        m_child->setProcessEnvironment(environment);
        QDir().mkpath(m_cache);
        m_child->setWorkingDirectory(m_cache);
        connect(m_child, &QProcess::finished, this, [this](int code) { summaryDone(code); });
        connect(m_child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                summaryDone(-1);
        });
        m_child->start(m_program, {"-p", "--model", m_model, "--no-session-persistence", "--tools", "",
                                   "--setting-sources", "", "--output-format", "json",
                                   "--system-prompt", summaryInstructions(covered > 0)});
        m_child->write(input.toUtf8());
        m_child->closeWriteChannel();
        m_childTimeout.start();
        publish();
        return;
    }
}

void Timeline::want(qint64 serial) {
    const auto *turn = m_parser.find(serial);
    if (!turn || turn->uuid.isEmpty() || m_wanted.contains(turn->uuid))
        return;
    m_wanted.insert(turn->uuid);
    QTimer::singleShot(0, this, &Timeline::summarizeNext);
}

void Timeline::stopSummary() {
    m_childTimeout.stop();
    if (!m_child)
        return;
    QProcess *child = m_child;
    m_child = nullptr;
    child->disconnect(this);
    child->kill();
    child->waitForFinished(500);
    child->deleteLater();
}

void Timeline::summaryDone(int code) {
    if (!m_child)
        return;
    m_childTimeout.stop();
    QProcess *child = m_child;
    m_child = nullptr;
    const QByteArray out = child->readAllStandardOutput();
    child->disconnect(this);
    child->deleteLater();
    const Job job = m_job;
    m_job = Job();

    const QJsonObject reply = QJsonDocument::fromJson(out).object();
    const QString result = reply.value("result").toString();
    const int open = result.indexOf('{'), close = result.lastIndexOf('}');
    const QJsonArray points = QJsonDocument::fromJson(result.mid(open, close - open + 1).toUtf8())
                                  .object().value("points").toArray();
    if (code != 0 || reply.value("is_error").toBool() || open < 0 || points.isEmpty()) {
        // A usage limit, or anything else: summaries rest a while.
        const bool limit = QString::fromUtf8(out).contains(QLatin1String("limit"), Qt::CaseInsensitive);
        m_backoffUntil = QDateTime::currentMSecsSinceEpoch() + (limit ? 30 : 5) * 60 * 1000;
        if (job.generation == m_generation)
            publish();
        return;
    }
    static const QStringList kinds{"done", "change", "ship", "fail", "ask", "open", "note"};
    Batch batch;
    batch.to = job.to;
    for (const QJsonValue &value : points) {
        const QJsonObject point = value.toObject();
        const QString text = point.value("text").toString().simplified();
        if (text.isEmpty())
            continue;
        const QString kind = point.value("kind").toString();
        const int step = point.value("step").toInt(1);
        // Where to find it in the terminal, while the points are as numbered.
        const QString needle = job.generation == m_generation
            ? m_parser.needle(job.serial, qBound(job.from, job.from + step - 1, job.to - 1)) : QString();
        batch.points << QVariantMap{{"text", text}, {"kind", kinds.contains(kind) ? kind : QStringLiteral("note")},
                                    {"needle", needle}};
    }
    if (job.session == m_session) {
        m_summaries[job.uuid].append(batch);
        saveSummaries(m_session, m_summaries);
        publish();
    } else {
        // Another agent by now: its summary is kept for when it's back.
        Timeline other;
        other.m_cache = m_cache;
        other.m_session = job.session;
        other.loadSummaries();
        other.m_summaries[job.uuid].append(batch);
        saveSummaries(job.session, other.m_summaries);
    }
    QTimer::singleShot(0, this, &Timeline::summarizeNext);
}

// Row by row, so the list keeps its place: turns gone from the front are
// removed, changed ones updated, new ones added.
void Timeline::publish() {
    QList<QVariantMap> rows;
    for (const auto &turn : m_parser.turns())
        rows << rowOf(turn);
    const qint64 first = rows.isEmpty() ? std::numeric_limits<qint64>::max() : rows.first().value("serial").toLongLong();
    int drop = 0;
    while (drop < m_rows.size() && m_rows.at(drop).value("serial").toLongLong() < first)
        ++drop;
    const int before = m_rows.size();
    if (drop > 0) {
        beginRemoveRows({}, 0, drop - 1);
        m_rows.remove(0, drop);
        endRemoveRows();
    }
    bool same = m_rows.size() <= rows.size();
    for (int i = 0; same && i < m_rows.size(); ++i)
        same = m_rows.at(i).value("serial") == rows.at(i).value("serial");
    if (!same) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    } else {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(i), index(i));
            }
        }
        if (rows.size() > m_rows.size()) {
            beginInsertRows({}, m_rows.size(), rows.size() - 1);
            m_rows.append(rows.mid(m_rows.size()));
            endInsertRows();
        }
    }
    if (m_rows.size() != before)
        emit countChanged();
    const QString activity = m_parser.activity(), said = m_parser.lastSaid();
    if (activity != m_activity || said != m_lastSaid) {
        m_activity = activity;
        m_lastSaid = said;
        emit progressChanged();
    }
    if (!m_child)
        QTimer::singleShot(0, this, &Timeline::summarizeNext);
}
