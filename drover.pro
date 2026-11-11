QT += core gui qml quick quickcontrols2 network

CONFIG += c++20 release
TARGET = drover
TEMPLATE = app

# The indicator is a wlr-layer-shell surface, so it sits above every Hyprland
# workspace without window rules.
LIBS += -lLayerShellQtInterface

HEADERS += \
    src/agentmodel.h \
    src/backend.h \
    src/surface.h \
    src/terminalfont.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/agentmodel.cpp \
    src/backend.cpp \
    src/surface.cpp \
    src/terminalfont.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
