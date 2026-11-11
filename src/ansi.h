#pragma once

#include <QString>
#include <QStringList>

// Terminal text with ANSI escape codes, as `herdr agent read --format ansi`
// gives it: stripped to plain text, or turned into rich text that keeps the
// agent's colors, bold, italic, and underline.
namespace Ansi {

// Plain text, every escape sequence removed.
QString strip(const QString &text);

// HTML for a Qt rich-text view, one <pre> block in `family`. The 16 basic
// colors come from `palette` (index 0 to 15, from the Omarchy theme); 256-color
// and 24-bit colors are used as they are. Text without a color takes the
// view's own color.
QString toHtml(const QString &text, const QStringList &palette, const QString &family);

// xterm's 16 basic colors, for when the theme has none.
QStringList defaultPalette();

} // namespace Ansi
