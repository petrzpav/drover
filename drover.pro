QT += core gui qml quick quickcontrols2 network dbus

CONFIG += c++20 release
TARGET = drover
TEMPLATE = app

# The indicator is a wlr-layer-shell surface, so it sits above every Hyprland
# workspace without window rules.
LIBS += -lLayerShellQtInterface

# The panel's terminal: libvterm parses, forkpty runs the program.
LIBS += -lvterm -lutil

HEADERS += \
    src/agentmodel.h \
    src/asker.h \
    src/backend.h \
    src/freshtheme.h \
    src/hook.h \
    src/surface.h \
    src/terminalview.h \
    src/terminalfont.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/agentmodel.cpp \
    src/asker.cpp \
    src/backend.cpp \
    src/freshtheme.cpp \
    src/hook.cpp \
    src/surface.cpp \
    src/terminalview.cpp \
    src/terminalfont.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
