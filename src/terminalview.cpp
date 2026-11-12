#include "terminalview.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QSocketNotifier>
#include <QTimer>
#include <QWheelEvent>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int ScrollbackLines = 10000;

VTermScreenCell blankCell() {
    VTermScreenCell cell;
    std::memset(&cell, 0, sizeof cell);
    cell.width = 1;
    cell.fg.type = VTERM_COLOR_DEFAULT_FG;
    cell.bg.type = VTERM_COLOR_DEFAULT_BG;
    return cell;
}

VTermModifier modifiersOf(Qt::KeyboardModifiers modifiers) {
    int mod = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        mod |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier)
        mod |= VTERM_MOD_ALT;
    if (modifiers & Qt::ControlModifier)
        mod |= VTERM_MOD_CTRL;
    return VTermModifier(mod);
}

// Keys that are not text, as libvterm names them.
VTermKey specialKey(int key) {
    switch (key) {
    case Qt::Key_Return: return VTERM_KEY_ENTER;
    case Qt::Key_Enter: return VTERM_KEY_ENTER;
    case Qt::Key_Tab: return VTERM_KEY_TAB;
    case Qt::Key_Backtab: return VTERM_KEY_TAB;
    case Qt::Key_Backspace: return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape: return VTERM_KEY_ESCAPE;
    case Qt::Key_Up: return VTERM_KEY_UP;
    case Qt::Key_Down: return VTERM_KEY_DOWN;
    case Qt::Key_Left: return VTERM_KEY_LEFT;
    case Qt::Key_Right: return VTERM_KEY_RIGHT;
    case Qt::Key_Insert: return VTERM_KEY_INS;
    case Qt::Key_Delete: return VTERM_KEY_DEL;
    case Qt::Key_Home: return VTERM_KEY_HOME;
    case Qt::Key_End: return VTERM_KEY_END;
    case Qt::Key_PageUp: return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown: return VTERM_KEY_PAGEDOWN;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return VTermKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    return VTERM_KEY_NONE;
}

} // namespace

TerminalView::TerminalView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setOpaquePainting(true);

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &TerminalView::onOutput, this);
    m_screen = vterm_obtain_screen(m_vt);
    static const VTermScreenCallbacks callbacks = {
        &TerminalView::onDamage,     nullptr, &TerminalView::onMoveCursor, &TerminalView::onSetTermProp, nullptr,
        nullptr,                     &TerminalView::onPushLine, &TerminalView::onPopLine, &TerminalView::onClearScrollback,
    };
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);
    updateMetrics();
    applyColors();
}

TerminalView::~TerminalView() {
    stop();
    vterm_free(m_vt);
}

void TerminalView::setProgram(const QString &program) {
    if (program == m_program)
        return;
    m_program = program;
    emit programChanged();
}

void TerminalView::setArguments(const QStringList &arguments) {
    if (arguments == m_arguments)
        return;
    m_arguments = arguments;
    emit programChanged();
}

void TerminalView::setFont(const QFont &font) {
    m_font = font;
    updateMetrics();
    updateGrid();
    emit fontChanged();
    update();
}

void TerminalView::setBackground(const QColor &color) {
    m_background = color;
    setFillColor(color);
    applyColors();
}

void TerminalView::setForeground(const QColor &color) {
    m_foreground = color;
    applyColors();
}

void TerminalView::setSelectionColor(const QColor &color) {
    m_selectionColor = color;
    emit colorsChanged();
    update();
}

void TerminalView::setColors(const QStringList &colors) {
    m_colors = colors;
    applyColors();
}

void TerminalView::applyColors() {
    VTermState *state = vterm_obtain_state(m_vt);
    auto toVterm = [](const QColor &color) {
        VTermColor c;
        vterm_color_rgb(&c, color.red(), color.green(), color.blue());
        return c;
    };
    const VTermColor fg = toVterm(m_foreground), bg = toVterm(m_background);
    vterm_state_set_default_colors(state, &fg, &bg);
    for (int i = 0; i < m_colors.size() && i < 16; ++i) {
        const VTermColor c = toVterm(QColor(m_colors.at(i)));
        vterm_state_set_palette_color(state, i, &c);
    }
    emit colorsChanged();
    update();
}

// The cell is as wide as the font's advance rounded to a whole pixel, and the
// font is spaced to match, so every glyph sits on the pixel grid and the
// text stays as sharp as in a standalone terminal.
void TerminalView::updateMetrics() {
    QFont base = m_font;
    base.setKerning(false);
    base.setStyleHint(QFont::TypeWriter);
    const QFontMetricsF raw(base);
    const qreal advance = raw.horizontalAdvance(QLatin1Char('M'));
    m_cellWidth = qMax<qreal>(1, qRound(advance));
    base.setLetterSpacing(QFont::AbsoluteSpacing, m_cellWidth - advance);
    m_font = base;
    m_bold = base;
    m_bold.setBold(true);
    m_italic = base;
    m_italic.setItalic(true);
    m_boldItalic = m_bold;
    m_boldItalic.setItalic(true);
    const QFontMetricsF metrics(m_font);
    m_cellHeight = qCeil(metrics.height());
    m_ascent = qRound(metrics.ascent());
}

void TerminalView::updateGrid() {
    const int cols = qMax(2, int(width() / m_cellWidth));
    const int rows = qMax(1, int(height() / m_cellHeight));
    if (cols == m_cols && rows == m_rows)
        return;
    m_cols = cols;
    m_rows = rows;
    vterm_set_size(m_vt, rows, cols);
    vterm_screen_flush_damage(m_screen);
    if (m_fd >= 0) {
        struct winsize size = {ushort(rows), ushort(cols), ushort(width()), ushort(height())};
        ioctl(m_fd, TIOCSWINSZ, &size);
    }
    m_scrollOffset = qMin<int>(m_scrollOffset, m_scrollback.size());
    emit gridChanged();
    update();
}

void TerminalView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateGrid();
}

void TerminalView::start() {
    if (m_pid > 0 || m_program.isEmpty())
        return;
    updateGrid();

    // Everything the child needs is prepared before fork, so the child only
    // calls exec.
    QList<QByteArray> storage{m_program.toLocal8Bit()};
    for (const auto &argument : m_arguments)
        storage << argument.toLocal8Bit();
    QVector<char *> argv;
    for (auto &argument : storage)
        argv << argument.data();
    argv << nullptr;

    struct winsize size = {ushort(m_rows), ushort(m_cols), ushort(width()), ushort(height())};
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &size);
    if (pid < 0) {
        qWarning("drover: can't open a terminal: %s", std::strerror(errno));
        emit finished();
        return;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    m_pid = pid;
    m_fd = fd;
    fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &TerminalView::readPty);
    emit runningChanged();
}

void TerminalView::stop() {
    if (m_pid <= 0)
        return;
    delete m_notifier;
    m_notifier = nullptr;
    // A hangup is what closing a terminal sends, to the program's whole
    // process group (forkpty made it a session of its own); whatever doesn't
    // go after a moment is killed, so no process is left behind.
    ::kill(-m_pid, SIGHUP);
    ::close(m_fd);
    m_fd = -1;
    int status = 0;
    bool reaped = false;
    for (int i = 0; i < 20 && !reaped; ++i) {
        reaped = waitpid(m_pid, &status, WNOHANG) == m_pid;
        if (!reaped)
            usleep(5000);
    }
    if (!reaped) {
        ::kill(-m_pid, SIGKILL);
        waitpid(m_pid, &status, 0);
    }
    m_pid = 0;
}

// Reads all the program has written and draws it on the next frame.
void TerminalView::readPty() {
    char buffer[65536];
    bool ended = false;
    for (;;) {
        const ssize_t length = ::read(m_fd, buffer, sizeof buffer);
        if (length > 0) {
            vterm_input_write(m_vt, buffer, size_t(length));
            continue;
        }
        if (length < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        ended = true;  // EOF, or EIO once the program has exited
        break;
    }
    vterm_screen_flush_damage(m_screen);
    if (ended) {
        stop();
        emit runningChanged();
        emit finished();
    }
}

void TerminalView::writePty(const char *bytes, size_t length) {
    while (m_fd >= 0 && length > 0) {
        const ssize_t written = ::write(m_fd, bytes, length);
        if (written > 0) {
            bytes += written;
            length -= size_t(written);
        } else if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd poll_fd = {m_fd, POLLOUT, 0};
            ::poll(&poll_fd, 1, 100);
        } else {
            return;
        }
    }
}

void TerminalView::onOutput(const char *bytes, size_t length, void *user) {
    static_cast<TerminalView *>(user)->writePty(bytes, length);
}

int TerminalView::onDamage(VTermRect, void *user) {
    static_cast<TerminalView *>(user)->update();
    return 1;
}

int TerminalView::onMoveCursor(VTermPos pos, VTermPos, int visible, void *user) {
    auto *self = static_cast<TerminalView *>(user);
    self->m_cursor = pos;
    self->m_cursorVisible = visible;
    self->update();
    return 1;
}

int TerminalView::onSetTermProp(VTermProp prop, VTermValue *value, void *user) {
    auto *self = static_cast<TerminalView *>(user);
    if (prop == VTERM_PROP_CURSORVISIBLE)
        self->m_cursorVisible = value->boolean;
    else if (prop == VTERM_PROP_CURSORSHAPE)
        self->m_cursorShape = value->number;
    else if (prop == VTERM_PROP_MOUSE) {
        self->m_mouseMode = value->number;
        self->setAcceptHoverEvents(value->number == VTERM_PROP_MOUSE_MOVE);
    }
    self->update();
    return 1;
}

int TerminalView::onPushLine(int cols, const VTermScreenCell *cells, void *user) {
    auto *self = static_cast<TerminalView *>(user);
    self->m_scrollback.emplace_back(cells, cells + cols);
    if (self->m_scrollback.size() > ScrollbackLines) {
        self->m_scrollback.pop_front();
        // Selection lines count from the oldest line, which just went.
        self->m_selectionStart.ry()--;
        self->m_selectionEnd.ry()--;
    }
    // Someone reading the scrollback keeps their place as new lines come.
    if (self->m_scrollOffset > 0)
        self->m_scrollOffset = qMin<int>(self->m_scrollOffset + 1, self->m_scrollback.size());
    return 1;
}

int TerminalView::onPopLine(int cols, VTermScreenCell *cells, void *user) {
    auto *self = static_cast<TerminalView *>(user);
    if (self->m_scrollback.empty())
        return 0;
    const QVector<VTermScreenCell> line = self->m_scrollback.back();
    self->m_scrollback.pop_back();
    for (int i = 0; i < cols; ++i)
        cells[i] = i < line.size() ? line.at(i) : blankCell();
    self->m_scrollOffset = qMin<int>(self->m_scrollOffset, self->m_scrollback.size());
    return 1;
}

int TerminalView::onClearScrollback(void *user) {
    auto *self = static_cast<TerminalView *>(user);
    self->m_scrollback.clear();
    self->m_scrollOffset = 0;
    self->m_hasSelection = false;
    self->update();
    return 1;
}

int TerminalView::firstVisibleLine() const {
    return int(m_scrollback.size()) - m_scrollOffset;
}

bool TerminalView::cellAt(int line, int column, VTermScreenCell *cell) const {
    const int history = int(m_scrollback.size());
    if (line < history) {
        const auto &row = m_scrollback.at(size_t(line));
        *cell = column < row.size() ? row.at(column) : blankCell();
        return true;
    }
    const VTermPos pos{line - history, column};
    if (pos.row >= m_rows || column >= m_cols)
        return false;
    return vterm_screen_get_cell(m_screen, pos, cell) != 0;
}

QColor TerminalView::resolve(VTermColor color, bool foreground) const {
    if (foreground && VTERM_COLOR_IS_DEFAULT_FG(&color))
        return m_foreground;
    if (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))
        return m_background;
    vterm_screen_convert_color_to_rgb(m_screen, &color);
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}

bool TerminalView::selected(int line, int column) const {
    if (!m_hasSelection)
        return false;
    QPoint start = m_selectionStart, end = m_selectionEnd;
    if (end.y() < start.y() || (end.y() == start.y() && end.x() < start.x()))
        std::swap(start, end);
    if (line < start.y() || line > end.y())
        return false;
    if (line == start.y() && column < start.x())
        return false;
    if (line == end.y() && column > end.x())
        return false;
    return true;
}

void TerminalView::paint(QPainter *painter) {
    painter->fillRect(QRectF(0, 0, width(), height()), m_background);
    const int first = firstVisibleLine();

    struct Run {
        qreal x = 0;
        QString text;
        QColor color;
        const QFont *font = nullptr;
        bool underline = false, strike = false;
    };
    auto flush = [&](Run &run, qreal y) {
        if (run.text.isEmpty())
            return;
        painter->setFont(*run.font);
        painter->setPen(run.color);
        painter->drawText(QPointF(run.x, y + m_ascent), run.text);
        const qreal width = run.text.size() * m_cellWidth;
        if (run.underline)
            painter->fillRect(QRectF(run.x, y + m_ascent + 2, width, 1), run.color);
        if (run.strike)
            painter->fillRect(QRectF(run.x, y + m_ascent * 0.6, width, 1), run.color);
        run.text.clear();
    };

    for (int row = 0; row < m_rows; ++row) {
        const int line = first + row;
        const qreal y = row * m_cellHeight;
        Run run;
        for (int column = 0; column < m_cols;) {
            VTermScreenCell cell;
            if (!cellAt(line, column, &cell))
                break;
            const int span = qMax(1, int(cell.width));
            QColor fg = resolve(cell.fg, true), bg = resolve(cell.bg, false);
            if (cell.attrs.reverse)
                std::swap(fg, bg);
            if (selected(line, column))
                bg = m_selectionColor;
            const qreal x = column * m_cellWidth;
            if (bg != m_background)
                painter->fillRect(QRectF(x, y, span * m_cellWidth, m_cellHeight), bg);

            QString text;
            if (!cell.attrs.conceal)
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                    text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
            const QFont *font = cell.attrs.bold ? (cell.attrs.italic ? &m_boldItalic : &m_bold)
                                                : (cell.attrs.italic ? &m_italic : &m_font);
            // Plain ASCII cells in one style are drawn as a run; anything
            // else (wide, accented, symbols from fallback fonts) goes on its
            // own, at its cell, so a different advance never shifts the grid.
            const bool ascii = text.size() == 1 && text.at(0).unicode() >= 0x20 && text.at(0).unicode() < 0x7f && span == 1;
            const bool joins = ascii && run.font == font && run.color == fg && run.underline == bool(cell.attrs.underline)
                               && run.strike == bool(cell.attrs.strike)
                               && qFuzzyCompare(run.x + run.text.size() * m_cellWidth + 1, x + 1);
            if (!joins)
                flush(run, y);
            if (!text.isEmpty() && text != QLatin1String(" ")) {
                if (run.text.isEmpty()) {
                    run.x = x;
                    run.color = fg;
                    run.font = font;
                    run.underline = cell.attrs.underline;
                    run.strike = cell.attrs.strike;
                }
                run.text += text;
                if (!ascii)
                    flush(run, y);
            } else {
                flush(run, y);
            }
            column += span;
        }
        flush(run, y);
    }

    // The cursor, while the screen (not the scrollback) is shown.
    if (m_scrollOffset == 0 && m_cursorVisible && m_cursor.row < m_rows && m_cursor.col < m_cols) {
        const QRectF cell(m_cursor.col * m_cellWidth, m_cursor.row * m_cellHeight, m_cellWidth, m_cellHeight);
        if (!hasActiveFocus()) {
            painter->setPen(QPen(m_foreground, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
            painter->fillRect(QRectF(cell.left(), cell.bottom() - 2, cell.width(), 2), m_foreground);
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
            painter->fillRect(QRectF(cell.left(), cell.top(), 2, cell.height()), m_foreground);
        } else {
            painter->fillRect(cell, m_foreground);
            VTermScreenCell under;
            if (cellAt(firstVisibleLine() + m_cursor.row, m_cursor.col, &under) && under.chars[0]) {
                painter->setFont(under.attrs.bold ? m_bold : m_font);
                painter->setPen(m_background);
                painter->drawText(QPointF(cell.left(), cell.top() + m_ascent),
                                  QString::fromUcs4(reinterpret_cast<const char32_t *>(&under.chars[0]), 1));
            }
        }
    }
}

// Every key goes to the program, before the window's shortcuts see it,
// except the reserved ones.
bool TerminalView::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QString sequence = QKeySequence(key->keyCombination()).toString(QKeySequence::PortableText);
        if (!m_reserved.contains(sequence)) {
            event->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(event);
}

void TerminalView::sendControl(const QString &letter) {
    if (letter.size() == 1 && letter.at(0).isLetter())
        vterm_keyboard_unichar(m_vt, letter.at(0).toLower().unicode(), VTERM_MOD_CTRL);
}

void TerminalView::sendText(const QString &text, VTermModifier modifiers) {
    for (const char32_t c : text.toUcs4())
        vterm_keyboard_unichar(m_vt, c, modifiers);
}

void TerminalView::keyPressEvent(QKeyEvent *event) {
    const auto mods = event->modifiers();
    const bool ctrlShift = (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier);
    if (ctrlShift && event->key() == Qt::Key_C) {
        copy();
        return;
    }
    if (ctrlShift && event->key() == Qt::Key_V) {
        paste();
        return;
    }
    if ((mods & Qt::ShiftModifier) && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
        scrollBy(event->key() == Qt::Key_PageUp ? m_rows - 1 : -(m_rows - 1));
        return;
    }

    VTermModifier modifiers = modifiersOf(mods);
    const VTermKey special = specialKey(event->key());
    if (special != VTERM_KEY_NONE) {
        if (event->key() == Qt::Key_Backtab)
            modifiers = VTermModifier(modifiers | VTERM_MOD_SHIFT);
        vterm_keyboard_key(m_vt, special, modifiers);
    } else if ((mods & (Qt::ControlModifier | Qt::AltModifier)) && event->key() < 0x7f && event->key() >= 0x20) {
        // Ctrl and Alt combinations name the key itself, not the control
        // character Qt puts in the text.
        char32_t c = char32_t(event->key());
        if (m_extendedKeys && c >= 'A' && c <= 'Z' && (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
            // CSI codepoint ; 1 + shift(1) + alt(2) + ctrl(4) u
            const QByteArray csi = QStringLiteral("\x1b[%1;%2u").arg(int(c - 'A' + 'a')).arg(1 + modifiers).toLatin1();
            writePty(csi.constData(), size_t(csi.size()));
            c = 0;
        } else if (c >= 'A' && c <= 'Z') {
            c = (mods & Qt::ShiftModifier) && !(mods & Qt::ControlModifier) ? c : c - 'A' + 'a';
        }
        if (c && m_doubleCtrlB && c == 'b' && modifiers == VTERM_MOD_CTRL)
            vterm_keyboard_unichar(m_vt, c, VTERM_MOD_CTRL);
        if (c)
            vterm_keyboard_unichar(m_vt, c, VTermModifier(modifiers & ~VTERM_MOD_SHIFT));
    } else if (!event->text().isEmpty()) {
        // The layout already applied Shift (and AltGr) to the text.
        sendText(event->text(), VTermModifier(modifiers & ~VTERM_MOD_SHIFT & ~VTERM_MOD_ALT));
    } else {
        event->ignore();
        return;
    }
    if (m_scrollOffset > 0) {
        m_scrollOffset = 0;
        update();
    }
    m_hasSelection = false;
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
    if (!event->commitString().isEmpty())
        sendText(event->commitString(), VTERM_MOD_NONE);
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled)
        return true;
    if (query == Qt::ImCursorRectangle)
        return QRectF(m_cursor.col * m_cellWidth, m_cursor.row * m_cellHeight, m_cellWidth, m_cellHeight);
    return QQuickPaintedItem::inputMethodQuery(query);
}

void TerminalView::copy() {
    if (!m_hasSelection)
        return;
    QPoint start = m_selectionStart, end = m_selectionEnd;
    if (end.y() < start.y() || (end.y() == start.y() && end.x() < start.x()))
        std::swap(start, end);
    QStringList lines;
    for (int line = start.y(); line <= end.y(); ++line)
        lines << lineText(line, line == start.y() ? start.x() : 0, line == end.y() ? end.x() : m_cols - 1);
    QGuiApplication::clipboard()->setText(lines.join('\n'));
}

void TerminalView::paste() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    text.replace("\r\n", "\r").replace('\n', '\r');
    vterm_keyboard_start_paste(m_vt);
    sendText(text, VTERM_MOD_NONE);
    vterm_keyboard_end_paste(m_vt);
}

QString TerminalView::lineText(int line, int from, int to) const {
    QString text;
    for (int column = from; column <= to && column < m_cols;) {
        VTermScreenCell cell;
        if (!cellAt(line, column, &cell))
            break;
        if (!cell.chars[0])
            text += ' ';
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
            text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
        column += qMax(1, int(cell.width));
    }
    while (text.endsWith(' '))
        text.chop(1);
    return text;
}

QString TerminalView::screenText() const {
    QStringList lines;
    for (int row = 0; row < m_rows; ++row)
        lines << lineText(int(m_scrollback.size()) + row, 0, m_cols - 1);
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines.join('\n');
}

QColor TerminalView::textColorAt(int row, int column) const {
    VTermScreenCell cell;
    if (!cellAt(int(m_scrollback.size()) + row, column, &cell))
        return {};
    return resolve(cell.attrs.reverse ? cell.bg : cell.fg, !cell.attrs.reverse);
}

QPoint TerminalView::cellUnder(const QPointF &position) const {
    const int column = qBound(0, int(position.x() / m_cellWidth), m_cols - 1);
    const int row = qBound(0, int(position.y() / m_cellHeight), m_rows - 1);
    return {column, firstVisibleLine() + row};
}

// A program that asked for mouse events (herdr's attach does) gets them, as
// in any terminal: it scrolls its own history on the wheel and handles
// clicks. Shift+drag still selects text here, as in Ghostty.
bool TerminalView::forwardsMouse(Qt::KeyboardModifiers modifiers) const {
    return m_mouseMode != VTERM_PROP_MOUSE_NONE && !(modifiers & Qt::ShiftModifier);
}

void TerminalView::sendMousePosition(const QPointF &position, Qt::KeyboardModifiers modifiers) {
    const int column = qBound(0, int(position.x() / m_cellWidth), m_cols - 1);
    const int row = qBound(0, int(position.y() / m_cellHeight), m_rows - 1);
    vterm_mouse_move(m_vt, row, column, modifiersOf(modifiers));
}

static int mouseButton(Qt::MouseButton button) {
    return button == Qt::LeftButton ? 1 : button == Qt::MiddleButton ? 2 : button == Qt::RightButton ? 3 : 0;
}

void TerminalView::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus(Qt::MouseFocusReason);
    m_hasSelection = false;
    m_forwardingPress = forwardsMouse(event->modifiers()) && mouseButton(event->button());
    if (m_forwardingPress) {
        sendMousePosition(event->position(), event->modifiers());
        vterm_mouse_button(m_vt, mouseButton(event->button()), true, modifiersOf(event->modifiers()));
        update();
        return;
    }
    m_selecting = true;
    m_selectionStart = m_selectionEnd = cellUnder(event->position());
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *event) {
    if (m_forwardingPress) {
        sendMousePosition(event->position(), event->modifiers());
        return;
    }
    if (!m_selecting)
        return;
    m_selectionEnd = cellUnder(event->position());
    m_hasSelection = m_selectionEnd != m_selectionStart;
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_forwardingPress) {
        sendMousePosition(event->position(), event->modifiers());
        vterm_mouse_button(m_vt, mouseButton(event->button()), false, modifiersOf(event->modifiers()));
        m_forwardingPress = false;
        return;
    }
    mouseMoveEvent(event);
    m_selecting = false;
}

void TerminalView::hoverMoveEvent(QHoverEvent *event) {
    if (m_mouseMode == VTERM_PROP_MOUSE_MOVE)
        sendMousePosition(event->position(), event->modifiers());
}

void TerminalView::scrollBy(int lines) {
    const int offset = qBound(0, m_scrollOffset + lines, int(m_scrollback.size()));
    if (offset == m_scrollOffset)
        return;
    m_scrollOffset = offset;
    update();
}

void TerminalView::wheelEvent(QWheelEvent *event) {
    event->accept();
    m_wheel += event->angleDelta().y();
    const int steps = m_wheel / 120;
    if (steps == 0)
        return;
    m_wheel -= steps * 120;
    if (forwardsMouse(event->modifiers())) {
        // Buttons 4 and 5 are the wheel; one notch, three lines, as usual.
        sendMousePosition(event->position(), event->modifiers());
        for (int i = 0; i < qAbs(steps); ++i) {
            vterm_mouse_button(m_vt, steps > 0 ? 4 : 5, true, modifiersOf(event->modifiers()));
        }
        return;
    }
    scrollBy(steps * 3);
}

void TerminalView::focusInEvent(QFocusEvent *event) {
    QQuickPaintedItem::focusInEvent(event);
    update();
}

void TerminalView::focusOutEvent(QFocusEvent *event) {
    QQuickPaintedItem::focusOutEvent(event);
    update();
}
