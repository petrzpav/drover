QT += core gui quick quickcontrols2 testlib network dbus
CONFIG += c++20 testcase
TARGET = drover_tests
TEMPLATE = app

INCLUDEPATH += ../src
LIBS += -lvterm -lutil

HEADERS += ../src/agentmodel.h ../src/backend.h ../src/terminalview.h ../src/theme.h ../src/freshtheme.h ../src/translator.h ../src/terminalfont.h
SOURCES += drover_tests.cpp ../src/agentmodel.cpp ../src/backend.cpp ../src/terminalview.cpp ../src/theme.cpp ../src/freshtheme.cpp ../src/translator.cpp ../src/terminalfont.cpp
