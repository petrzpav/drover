#pragma once

#include <QFont>
#include <QString>

// The font of the user's terminal: the first terminal in
// ~/.config/xdg-terminals.list (Omarchy's default-terminal setting) and the
// family and size from its config. Drover uses it for all of its text, so
// the panel reads like the terminal the agents run in.
namespace TerminalFont {
QFont read(const QString &home);
}
