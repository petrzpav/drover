#include "surface.h"

#include <LayerShellQt/window.h>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

Surface::Surface(const QString &stateFile, QObject *parent)
    : QObject(parent),
      m_stateFile(stateFile.isEmpty() ? QDir::homePath() + "/.local/state/drover/position" : stateFile) {
    QFile file(m_stateFile);
    if (file.open(QIODevice::ReadOnly)) {
        const auto parts = QString::fromUtf8(file.readAll()).split(' ');
        bool okX = false, okY = false;
        const double x = parts.value(0).toDouble(&okX), y = parts.value(1).toDouble(&okY);
        if (okX && okY && x >= 0 && y >= 0)
            m_offset = {x, y};
    }
}

bool Surface::attach(QWindow *window) {
    m_window = window;
    m_layer = QGuiApplication::platformName().startsWith("wayland");
    if (m_layer)
        m_openFlag = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/drover.open";
    if (!m_layer) {
        if (auto *screen = QGuiApplication::primaryScreen())
            window->resize(screen->availableSize());
        return false;
    }

    auto *layer = LayerShellQt::Window::get(window);
    layer->setScope("drover");
    // The top layer floats over windows but under fullscreen ones, so a
    // fullscreen video isn't covered by the indicator.
    layer->setLayer(LayerShellQt::Window::LayerTop);
    // Anchored to every edge, the compositor sizes the surface to the area
    // the bar leaves free.
    layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorBottom,
                       LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight});
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    layer->setWantsToBeOnActiveScreen(true);
    // When the compositor drops the surface (its output went away: a monitor
    // unplugged, phone mode), keep the window and show it again on a screen
    // that is still there, rather than letting the app quit.
    layer->setCloseOnDismissed(false);
    auto reshow = [this] {
        QTimer::singleShot(500, m_window, [this] {
            m_window->hide();
            m_window->show();
        });
    };
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, reshow);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        if (!m_window->isVisible())
            m_window->show();
    });
    return true;
}

void Surface::setOffset(const QPointF &offset) {
    const QPointF clamped(qMax(0.0, offset.x()), qMax(0.0, offset.y()));
    if (clamped == m_offset)
        return;
    m_offset = clamped;
    emit offsetChanged();
}

Surface::~Surface() {
    if (!m_openFlag.isEmpty())
        QFile::remove(m_openFlag);
}

void Surface::setExpanded(bool expanded) {
    // While the panel is open this file exists, so a compositor key binding
    // (Ctrl+P in Hyprland) can hand the key to drover instead of a window.
    if (!m_openFlag.isEmpty()) {
        if (expanded) {
            QFile flag(m_openFlag);
            if (!flag.open(QIODevice::WriteOnly))
                qWarning("drover: can't write %s", qPrintable(m_openFlag));
        } else {
            QFile::remove(m_openFlag);
        }
    }
    if (!m_layer || !m_window)
        return;
    LayerShellQt::Window::get(m_window)->setKeyboardInteractivity(
        expanded ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                 : LayerShellQt::Window::KeyboardInteractivityNone);
    if (expanded)
        m_window->requestActivate();
}

void Surface::setInputRegion(const QRectF &indicator, const QRectF &panel) {
    if (!m_window)
        return;
    QRegion region(indicator.toAlignedRect());
    if (!panel.isEmpty())
        region += panel.toAlignedRect();
    m_window->setMask(region);
}

void Surface::save() {
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    // Written beside the target and renamed, so a crash never leaves half a file.
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QStringLiteral("%1 %2\n").arg(m_offset.x()).arg(m_offset.y()).toUtf8());
    file.commit();
}
