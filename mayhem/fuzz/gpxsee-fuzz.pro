# Static library of GPXSee's GUI-free parsing/rendering code (src/common, src/data, src/map and the
# two GUI helpers they use) for the fuzz harnesses. Built by mayhem/build.sh with clang and the
# sanitizer flags passed in QMAKE_CFLAGS/QMAKE_CXXFLAGS from the command line.
TEMPLATE = lib
CONFIG += staticlib c++17
CONFIG -= debug_and_release
TARGET = gpxsee-fuzz
QT += core gui network sql concurrent widgets svg

ROOT = $$PWD/../..
INCLUDEPATH += $$ROOT/src
HEADERS += $$files($$ROOT/src/common/*.h, true) $$files($$ROOT/src/data/*.h, true) \
    $$files($$ROOT/src/map/*.h, true) $$ROOT/src/GUI/format.h $$ROOT/src/GUI/units.h
SOURCES += $$files($$ROOT/src/common/*.cpp, true) $$files($$ROOT/src/data/*.cpp, true) \
    $$files($$ROOT/src/map/*.cpp, true) $$ROOT/src/GUI/format.cpp
DEFINES += APP_VERSION=\\\"16.20\\\"
RESOURCES += $$ROOT/common.qrc
