#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

class QWindow;

// Turns the window into a transparent wlr-layer-shell surface over the whole
// usable area of the screen, above windows on every workspace. Only the
// indicator and the open panel take input (the window mask), so everything
// else is clicked through. Drawing the indicator inside one big surface lets
// it be dragged anywhere and the panel open beside it, without moving the
// surface itself.
//
// It takes no keyboard focus while it is only the indicator, and takes it on
// demand while the panel is open.
class Surface : public QObject {
    Q_OBJECT
    // Where the indicator sits, as its distance from the right and bottom
    // edges, so it stays in its corner when the screen size changes.
    Q_PROPERTY(QPointF offset READ offset WRITE setOffset NOTIFY offsetChanged)
    // How the panel was left: over the whole screen, and with the agent list
    // hidden. Both are kept for the next start, beside the indicator's place.
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY panelStateChanged)
    Q_PROPERTY(bool listHidden READ listHidden WRITE setListHidden NOTIFY panelStateChanged)
    // The panel shows the agent's conversation in points rather than its
    // terminal (Ctrl+Alt+B).
    Q_PROPERTY(bool overviewShown READ overviewShown WRITE setOverviewShown NOTIFY panelStateChanged)

public:
    explicit Surface(const QString &stateFile = {}, QObject *parent = nullptr);
    ~Surface() override;

    // Must run before the window is first shown. Off Wayland (tests,
    // offscreen) the window stays an ordinary window.
    bool attach(QWindow *window);

    QPointF offset() const { return m_offset; }
    void setOffset(const QPointF &offset);
    bool fullscreen() const { return m_fullscreen; }
    void setFullscreen(bool fullscreen);
    bool listHidden() const { return m_listHidden; }
    void setListHidden(bool hidden);
    bool overviewShown() const { return m_overviewShown; }
    void setOverviewShown(bool shown);

    Q_INVOKABLE void setExpanded(bool expanded);
    // The stage wants the keyboard (the user clicked it, or pressed the
    // toggle key while an agent asks), or gives it back.
    Q_INVOKABLE void setStageFocus(bool focus);
    // The open panel takes the keyboard again, as when it opened.
    Q_INVOKABLE void takeKeyboard();
    // The parts of the window that take the pointer.
    Q_INVOKABLE void setInputRegion(const QRectF &indicator, const QRectF &panel, const QRectF &stage = QRectF());
    // Remembers the indicator's place for the next start.
    Q_INVOKABLE void save();

signals:
    void offsetChanged();
    void panelStateChanged();

private slots:
    void updateFocusFlag();

private:
    void updateKeyboard(bool grab);
    void savePanelState();

private:
    QString m_stateFile, m_panelFile, m_focusFlag;
    bool m_fullscreen = false;
    bool m_listHidden = false;
    bool m_overviewShown = false;
    bool m_expanded = false;
    bool m_stageFocus = false;
    QWindow *m_window = nullptr;
    bool m_layer = false;
    QPointF m_offset{12, 12};
};
