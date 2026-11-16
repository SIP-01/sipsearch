QT += core gui quick quickcontrols2 testlib

CONFIG += c++17 testcase
TARGET = sipsearch_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/engine.h ../src/instant.h ../src/query.h ../src/ranker.h ../src/theme.h ../src/translator.h
SOURCES += sipsearch_tests.cpp ../src/backend.cpp ../src/engine.cpp ../src/instant.cpp ../src/query.cpp ../src/ranker.cpp ../src/theme.cpp ../src/translator.cpp
