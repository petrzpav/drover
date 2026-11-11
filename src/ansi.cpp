#include "ansi.h"

#include <QColor>
#include <QRegularExpression>

namespace {

// SGR sequences (colors and styles) and every other CSI or OSC sequence.
const QRegularExpression &escapes() {
    static const QRegularExpression expression(QStringLiteral(
        "\\x1b\\[[0-9;:?]*[A-Za-z]|\\x1b\\][^\\x07\\x1b]*(?:\\x07|\\x1b\\\\)|\\x1b[()][A-Za-z0-9]|\\x1b[=>]"));
    return expression;
}

struct Style {
    QString foreground, background;
    bool bold = false, dim = false, italic = false, underline = false, inverse = false;
    bool operator==(const Style &other) const = default;
};

QString color256(int index, const QStringList &palette) {
    if (index < 16)
        return palette.value(index);
    if (index < 232) {
        const int i = index - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return QColor(level(i / 36), level(i / 6 % 6), level(i % 6)).name();
    }
    const int grey = 8 + (index - 232) * 10;
    return QColor(grey, grey, grey).name();
}

// Applies one SGR parameter list ("1;38;2;255;0;0") to the style.
void apply(Style &style, const QString &parameters, const QStringList &palette) {
    QList<int> codes;
    for (const auto &part : parameters.split(QRegularExpression("[;:]")))
        codes.append(part.isEmpty() ? 0 : part.toInt());
    if (codes.isEmpty())
        codes.append(0);
    for (int i = 0; i < codes.size(); ++i) {
        const int code = codes.at(i);
        auto extended = [&](QString &target) {
            if (i + 1 < codes.size() && codes.at(i + 1) == 5 && i + 2 < codes.size()) {
                target = color256(codes.at(i + 2), palette);
                i += 2;
            } else if (i + 1 < codes.size() && codes.at(i + 1) == 2 && i + 4 < codes.size()) {
                target = QColor(codes.at(i + 2), codes.at(i + 3), codes.at(i + 4)).name();
                i += 4;
            }
        };
        if (code == 0) style = Style();
        else if (code == 1) style.bold = true;
        else if (code == 2) style.dim = true;
        else if (code == 3) style.italic = true;
        else if (code == 4) style.underline = true;
        else if (code == 7) style.inverse = true;
        else if (code == 22) style.bold = style.dim = false;
        else if (code == 23) style.italic = false;
        else if (code == 24) style.underline = false;
        else if (code == 27) style.inverse = false;
        else if (code >= 30 && code <= 37) style.foreground = palette.value(code - 30);
        else if (code == 38) extended(style.foreground);
        else if (code == 39) style.foreground.clear();
        else if (code >= 40 && code <= 47) style.background = palette.value(code - 40);
        else if (code == 48) extended(style.background);
        else if (code == 49) style.background.clear();
        else if (code >= 90 && code <= 97) style.foreground = palette.value(code - 90 + 8);
        else if (code >= 100 && code <= 107) style.background = palette.value(code - 100 + 8);
    }
}

QString open(const Style &style, const QStringList &palette) {
    QString foreground = style.foreground, background = style.background;
    // Dim text without a color of its own takes the palette's grey.
    if (style.dim && foreground.isEmpty())
        foreground = palette.value(8);
    if (style.inverse) {
        std::swap(foreground, background);
        // Inverse with default colors still has to show as a block.
        if (foreground.isEmpty())
            foreground = QStringLiteral("palette(base)");
    }
    QStringList css;
    if (!foreground.isEmpty())
        css << "color:" + foreground;
    if (!background.isEmpty())
        css << "background-color:" + background;
    if (style.bold)
        css << "font-weight:bold";
    if (style.italic)
        css << "font-style:italic";
    if (style.underline)
        css << "text-decoration:underline";
    if (css.isEmpty())
        return {};
    return "<span style=\"" + css.join(';') + "\">";
}

} // namespace

QString Ansi::strip(const QString &text) {
    QString plain = text;
    plain.remove(escapes());
    plain.remove('\r');
    return plain;
}

QStringList Ansi::defaultPalette() {
    return {"#000000", "#cd3131", "#0dbc79", "#e5e510", "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5",
            "#666666", "#f14c4c", "#23d18b", "#f5f543", "#3b8eea", "#d670d6", "#29b8db", "#ffffff"};
}

QString Ansi::toHtml(const QString &text, const QStringList &palette, const QString &family) {
    const QStringList colors = palette.size() >= 16 ? palette : defaultPalette();
    QString html = "<pre style=\"margin:0; font-family:'" + family.toHtmlEscaped() + "'\">";
    const QStringList lines = text.split('\n');
    for (int l = 0; l < lines.size(); ++l) {
        // Each line starts plain: screen dumps restate their style per line,
        // and a line whose tail was trimmed must not leak its style onward.
        Style style;
        QString line = lines.at(l);
        line.remove('\r');
        int position = 0;
        bool spanOpen = false;
        auto flushText = [&](const QString &chunk) {
            if (chunk.isEmpty())
                return;
            html += chunk.toHtmlEscaped();
        };
        auto reopen = [&](const Style &next) {
            if (spanOpen)
                html += "</span>";
            const QString tag = open(next, colors);
            spanOpen = !tag.isEmpty();
            html += tag;
        };
        auto matches = escapes().globalMatch(line);
        while (matches.hasNext()) {
            const auto match = matches.next();
            flushText(line.mid(position, match.capturedStart() - position));
            position = match.capturedEnd();
            const QString sequence = match.captured(0);
            if (!sequence.startsWith("\x1b[") || !sequence.endsWith('m'))
                continue;
            Style next = style;
            apply(next, sequence.mid(2, sequence.size() - 3), colors);
            if (!(next == style)) {
                style = next;
                reopen(style);
            }
        }
        flushText(line.mid(position));
        if (spanOpen)
            html += "</span>";
        if (l + 1 < lines.size())
            html += '\n';
    }
    return html + "</pre>";
}
