#include "surface.h"

#include <LayerShellQt/window.h>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegion>
#include <QSaveFile>
#include <QScreen>
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
    return true;
}

void Surface::setOffset(const QPointF &offset) {
    const QPointF clamped(qMax(0.0, offset.x()), qMax(0.0, offset.y()));
    if (clamped == m_offset)
        return;
    m_offset = clamped;
    emit offsetChanged();
}

void Surface::setExpanded(bool expanded) {
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
