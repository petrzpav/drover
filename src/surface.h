#pragma once

#include <QObject>

class QWindow;

// Turns the window into a wlr-layer-shell surface in the bottom-right corner,
// above windows on every workspace. It takes no keyboard focus while it is
// only the indicator, and takes it on demand while the panel is open.
class Surface : public QObject {
    Q_OBJECT

public:
    explicit Surface(QObject *parent = nullptr) : QObject(parent) {}

    // Must run before the window is first shown. Returns false off Wayland
    // (tests, offscreen), where the window stays an ordinary window.
    bool attach(QWindow *window);

    Q_INVOKABLE void setExpanded(bool expanded);

private:
    QWindow *m_window = nullptr;
    bool m_layer = false;
};
