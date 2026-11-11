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

public:
    explicit Surface(const QString &stateFile = {}, QObject *parent = nullptr);
    ~Surface() override;

    // Must run before the window is first shown. Off Wayland (tests,
    // offscreen) the window stays an ordinary window.
    bool attach(QWindow *window);

    QPointF offset() const { return m_offset; }
    void setOffset(const QPointF &offset);

    Q_INVOKABLE void setExpanded(bool expanded);
    // The parts of the window that take the pointer.
    Q_INVOKABLE void setInputRegion(const QRectF &indicator, const QRectF &panel);
    // Remembers the indicator's place for the next start.
    Q_INVOKABLE void save();

signals:
    void offsetChanged();

private:
    QString m_stateFile, m_openFlag;
    QWindow *m_window = nullptr;
    bool m_layer = false;
    QPointF m_offset{12, 12};
};
