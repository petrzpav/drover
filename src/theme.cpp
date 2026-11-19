// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, and a full-screen panel
    // its outer gaps, which themes and personal overrides can both set, so
    // ask hyprctl rather than reading its config. Without Hyprland the
    // corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        if (code != 0 || status != QProcess::NormalExit)
            return;
        // A batch answers with one JSON object per option, one after another.
        const QByteArray output = m_roundingProcess.readAllStandardOutput();
        int radius = m_radius;
        QList<int> gaps = m_gaps;
        for (qsizetype start = output.indexOf('{'); start >= 0;) {
            int depth = 0;
            qsizetype end = start;
            for (; end < output.size(); ++end) {
                depth += output[end] == '{' ? 1 : output[end] == '}' ? -1 : 0;
                if (depth == 0)
                    break;
            }
            const auto object = QJsonDocument::fromJson(output.mid(start, end - start + 1)).object();
            const QString option = object.value("option").toString();
            if (option == "decoration:rounding" && object.value("int").toInt(-1) >= 0) {
                radius = object.value("int").toInt();
            } else if (option == "general:gaps_out") {
                // "css" is "top right bottom left" (or fewer, CSS style); an
                // older Hyprland answers a plain "int".
                QList<int> sides;
                for (const QString &part : object.value("css").toString().split(' ', Qt::SkipEmptyParts))
                    sides << part.toInt();
                if (sides.isEmpty() && object.value("int").isDouble())
                    sides << object.value("int").toInt();
                if (!sides.isEmpty() && sides.size() <= 4) {
                    while (sides.size() < 4)
                        sides << sides.value(sides.size() == 3 ? 1 : 0);
                    gaps = sides;
                }
            }
            start = output.indexOf('{', end + 1);
        }
        if (radius != m_radius || gaps != m_gaps) {
            m_radius = radius;
            m_gaps = gaps;
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "--batch", "getoption decoration:rounding; getoption general:gaps_out"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// Every `name = "#rrggbb"` line of colors.toml.
QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression line(R"re(^\s*([a-z_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                         QRegularExpression::MultilineOption);
    auto matches = line.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

QStringList Theme::terminalPalette() const {
    static const char *names[16] = {"dark_background", "red", "green", "yellow", "blue", "magenta", "cyan",
                                    "light_foreground", "muted", "bright_red", "bright_green", "bright_yellow",
                                    "bright_blue", "bright_magenta", "bright_cyan", "bright_foreground"};
    static const QStringList fallback = {"#000000", "#cd3131", "#0dbc79", "#e5e510", "#2472c8", "#bc3fbc",
                                         "#11a8cd", "#e5e5e5", "#666666", "#f14c4c", "#23d18b", "#f5f543",
                                         "#3b8eea", "#d670d6", "#29b8db", "#ffffff"};
    QStringList palette;
    for (int i = 0; i < 16; ++i)
        palette << m_colors.value(QString::fromLatin1(names[i]), m_colors.value(QStringLiteral("color%1").arg(i), fallback.at(i)));
    return palette;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto accent = readAccent(m_directory + "/theme/colors.toml");
    const auto colors = readColors(m_directory + "/theme/colors.toml");
    if (accent != m_accent || colors != m_colors) {
        m_accent = accent;
        m_colors = colors;
        emit changed();
    }
}
