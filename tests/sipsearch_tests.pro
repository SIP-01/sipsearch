QT += core gui quick quickcontrols2 testlib

CONFIG += c++17 testcase
TARGET = sipsearch_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/ranker.h ../src/theme.h ../src/translator.h
SOURCES += sipsearch_tests.cpp ../src/backend.cpp ../src/ranker.cpp ../src/theme.cpp ../src/translator.cpp
