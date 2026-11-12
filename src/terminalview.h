#pragma once

#include <QColor>
#include <QFont>
#include <QPoint>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QVector>
#include <deque>
#include <sys/types.h>

extern "C" {
#include <vterm.h>
}

class QSocketNotifier;

// A terminal for QML: a program on a pseudo-terminal, its output parsed by
// libvterm (the engine of Neovim's terminal) and painted here. Output is
// drawn on the next frame after it arrives, with no buffering in between, so
// typing feels as quick as in a standalone terminal. Keys go to the program
// as a terminal would send them (Shift+Tab, Esc, Ctrl+C, Alt+letters), except
// the shortcuts listed in `reservedShortcuts`, which stay with the window.
class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged)
    Q_PROPERTY(QStringList arguments READ arguments WRITE setArguments NOTIFY programChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY colorsChanged)
    // The 16 basic colors, as "#rrggbb".
    Q_PROPERTY(QStringList colors READ colors WRITE setColors NOTIFY colorsChanged)
    Q_PROPERTY(QStringList reservedShortcuts READ reservedShortcuts WRITE setReservedShortcuts)
    // Sends Ctrl+Shift+letter as such (CSI u, the kitty keyboard protocol's
    // form), for programs that tell them from Ctrl+letter, like editors.
    // Off, they go as Ctrl+letter, which is all most programs know.
    Q_PROPERTY(bool extendedKeys READ extendedKeys WRITE setExtendedKeys)
    // Sends Ctrl+B twice: `herdr terminal attach` (0.9.1) takes Ctrl+B as its
    // prefix whatever the config says, and passes it on only when doubled.
    Q_PROPERTY(bool doubleCtrlB READ doubleCtrlB WRITE setDoubleCtrlB)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int columns READ columns NOTIFY gridChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridChanged)

public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    ~TerminalView() override;

    QString program() const { return m_program; }
    void setProgram(const QString &program);
    QStringList arguments() const { return m_arguments; }
    void setArguments(const QStringList &arguments);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    QColor background() const { return m_background; }
    void setBackground(const QColor &color);
    QColor foreground() const { return m_foreground; }
    void setForeground(const QColor &color);
    QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor &color);
    QStringList colors() const { return m_colors; }
    void setColors(const QStringList &colors);
    QStringList reservedShortcuts() const { return m_reserved; }
    void setReservedShortcuts(const QStringList &shortcuts) { m_reserved = shortcuts; }
    bool extendedKeys() const { return m_extendedKeys; }
    bool doubleCtrlB() const { return m_doubleCtrlB; }
    void setDoubleCtrlB(bool on) { m_doubleCtrlB = on; }
    void setExtendedKeys(bool on) { m_extendedKeys = on; }
    bool running() const { return m_pid > 0; }
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }

    // Starts the program; the terminal is sized from the item already.
    Q_INVOKABLE void start();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    // Sends Ctrl and a letter, as if typed (for a shortcut the desktop took).
    Q_INVOKABLE void sendControl(const QString &letter);
    // What the screen shows, one line per row, trailing spaces trimmed.
    Q_INVOKABLE QString screenText() const;
    // The color a cell's text is drawn in.
    Q_INVOKABLE QColor textColorAt(int row, int column) const;

    void paint(QPainter *painter) override;

signals:
    void programChanged();
    void fontChanged();
    void colorsChanged();
    void runningChanged();
    void gridChanged();
    // The program ended (for `herdr terminal attach`: the terminal detached).
    void finished();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldPos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *value, void *user);
    static int onPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onPopLine(int cols, VTermScreenCell *cells, void *user);
    static int onClearScrollback(void *user);
    static void onOutput(const char *bytes, size_t length, void *user);

    void readPty();
    void writePty(const char *bytes, size_t length);
    void stop();
    void updateMetrics();
    void updateGrid();
    void applyColors();
    void sendText(const QString &text, VTermModifier modifiers);
    void scrollBy(int lines);
    bool cellAt(int line, int column, VTermScreenCell *cell) const;
    QColor resolve(VTermColor color, bool foreground) const;
    QPoint cellUnder(const QPointF &position) const;
    bool forwardsMouse(Qt::KeyboardModifiers modifiers) const;
    void sendMousePosition(const QPointF &position, Qt::KeyboardModifiers modifiers);
    int firstVisibleLine() const;
    bool selected(int line, int column) const;
    QString lineText(int line, int from, int to) const;

    QString m_program;
    QStringList m_arguments, m_colors, m_reserved;
    bool m_extendedKeys = false;
    bool m_doubleCtrlB = false;
    QFont m_font, m_bold, m_italic, m_boldItalic;
    QColor m_background{"#101315"}, m_foreground{"#cacccc"}, m_selectionColor{"#505a5f"};
    qreal m_cellWidth = 8, m_cellHeight = 16, m_ascent = 12;
    int m_rows = 24, m_cols = 80;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    int m_fd = -1;
    pid_t m_pid = 0;
    QSocketNotifier *m_notifier = nullptr;

    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    // What mouse events the program asked for (VTERM_PROP_MOUSE_*).
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
    bool m_forwardingPress = false;

    std::deque<QVector<VTermScreenCell>> m_scrollback;
    int m_scrollOffset = 0;
    int m_wheel = 0;  // wheel movement not yet a whole notch

    // Selection, in lines counted from the oldest line of scrollback.
    bool m_selecting = false, m_hasSelection = false;
    QPoint m_selectionStart, m_selectionEnd;
};
