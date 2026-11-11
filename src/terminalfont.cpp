#include "terminalfont.h"

#include <QFile>
#include <QRegularExpression>

namespace {

QString readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString first(const QString &text, const QString &pattern) {
    const QRegularExpression expression(pattern, QRegularExpression::MultilineOption);
    return expression.match(text).captured(1).trimmed();
}

QString terminal(const QString &home) {
    const auto lines = readFile(home + "/.config/xdg-terminals.list").split('\n');
    for (const auto &line : lines) {
        const QString entry = line.trimmed();
        if (entry.isEmpty() || entry.startsWith('#'))
            continue;
        for (const auto &name : {"ghostty", "alacritty", "kitty", "foot"})
            if (entry.contains(QLatin1String(name), Qt::CaseInsensitive))
                return name;
        return {};
    }
    return {};
}

} // namespace

QFont TerminalFont::read(const QString &home) {
    QString family, size;
    const QString which = terminal(home);
    if (which == "ghostty") {
        const QString config = readFile(home + "/.config/ghostty/config");
        family = first(config, R"re(^\s*font-family\s*=\s*"?([^"\n]+)"?\s*$)re");
        size = first(config, R"re(^\s*font-size\s*=\s*([0-9.]+))re");
    } else if (which == "alacritty") {
        const QString config = readFile(home + "/.config/alacritty/alacritty.toml");
        family = first(config, R"re(^\s*normal\s*=\s*\{[^}]*family\s*=\s*"([^"]+)")re");
        size = first(config, R"re(^\s*size\s*=\s*([0-9.]+))re");
    } else if (which == "kitty") {
        const QString config = readFile(home + "/.config/kitty/kitty.conf");
        family = first(config, R"re(^\s*font_family\s+(.+)$)re");
        size = first(config, R"re(^\s*font_size\s+([0-9.]+))re");
    } else if (which == "foot") {
        const QString config = readFile(home + "/.config/foot/foot.ini");
        family = first(config, R"re(^\s*font\s*=\s*([^:\n]+))re");
        size = first(config, R"re(^\s*font\s*=.*:size=([0-9.]+))re");
    }

    // Without a terminal config, the system monospace font at Omarchy's
    // terminal default size.
    QFont font(family.isEmpty() ? QStringLiteral("monospace") : family);
    const double points = size.toDouble();
    font.setPointSizeF(points > 0 ? points : 10);
    return font;
}
