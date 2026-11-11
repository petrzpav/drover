QT += core gui qml quick quickcontrols2 testlib network
CONFIG += c++20 testcase
TARGET = drover_ui_tests
TEMPLATE = app

INCLUDEPATH += ../src
LIBS += -lLayerShellQtInterface

HEADERS += ../src/agentmodel.h ../src/backend.h ../src/surface.h ../src/theme.h ../src/translator.h
SOURCES += drover_ui_tests.cpp ../src/agentmodel.cpp ../src/backend.cpp ../src/surface.cpp \
           ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
