QT += core gui qml quick quickcontrols2 testlib network dbus
CONFIG += c++20 testcase
TARGET = drover_ui_tests
TEMPLATE = app

INCLUDEPATH += ../src
LIBS += -lvterm -lutil
LIBS += -lLayerShellQtInterface

HEADERS += ../src/asker.h ../src/hook.h ../src/terminalfont.h ../src/agentmodel.h ../src/backend.h ../src/surface.h ../src/terminalview.h ../src/theme.h ../src/translator.h
SOURCES += drover_ui_tests.cpp ../src/asker.cpp ../src/hook.cpp ../src/terminalfont.cpp ../src/agentmodel.cpp ../src/backend.cpp ../src/surface.cpp \
           ../src/terminalview.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
