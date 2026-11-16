QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = sipsearch
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/ranker.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/ranker.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
