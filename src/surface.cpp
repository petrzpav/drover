#include "surface.h"

#include <LayerShellQt/window.h>
#include <QGuiApplication>
#include <QWindow>

bool Surface::attach(QWindow *window) {
    m_window = window;
    m_layer = QGuiApplication::platformName().startsWith("wayland");
    if (!m_layer)
        return false;

    auto *layer = LayerShellQt::Window::get(window);
    layer->setScope("drover");
    // The top layer floats over windows but under fullscreen ones, so a
    // fullscreen video isn't covered by the indicator.
    layer->setLayer(LayerShellQt::Window::LayerTop);
    // Anchored to the corner, the surface grows away from it when the panel
    // opens, so the indicator itself never moves.
    layer->setAnchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorRight});
    layer->setMargins({0, 0, 12, 12});
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    layer->setWantsToBeOnActiveScreen(true);
    return true;
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
