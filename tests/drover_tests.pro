QT += core gui quick quickcontrols2 testlib network
CONFIG += c++20 testcase
TARGET = drover_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/agentmodel.h ../src/ansi.h ../src/backend.h ../src/theme.h ../src/translator.h ../src/terminalfont.h
SOURCES += drover_tests.cpp ../src/agentmodel.cpp ../src/ansi.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp ../src/terminalfont.cpp
