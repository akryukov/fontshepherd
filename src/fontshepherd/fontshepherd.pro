QT += xml svg widgets
CONFIG += qt debug
CONFIG += object_parallel_to_source
TEMPLATE = app
QMAKE_CXXFLAGS += -DBOOST_SYSTEM_NO_DEPRECATED
QMAKE_CXXFLAGS += -std=c++11

RESOURCES = ../../fontshepherd.qrc
include (tables/tables.pri)
include (editors/editors.pri)

SOURCES += fontshepherd.cpp tableview.cpp sfnt.cpp charbuffer.cpp
SOURCES += tables.cpp splineglyph.cpp splineglyphsvg.cpp splineutil.cpp
SOURCES += fs_notify.cpp fs_math.cpp fs_undo.cpp commonlists.cpp
SOURCES += ftwrapper.cpp icuwrapper.cpp
SOURCES += stemdb.cpp

HEADERS += fontshepherd.h tableview.h sfnt.h cffstuff.h
HEADERS += tables.h splineglyph.h charbuffer.h commonlists.h
HEADERS += exceptions.h fs_notify.h fs_math.h fs_undo.h
HEADERS += ftwrapper.h icuwrapper.h
HEADERS += stemdb.h

DEPENDPATH += ../qhexedit2
INCLUDEPATH += ../qhexedit2 /usr/include/freetype2

unix: {
  DESTDIR = release
  isEmpty (PREFIX) {
    PREFIX = /usr/local
  }
  BINDIR = $$PREFIX/bin
  DATADIR = $$PREFIX/share
  SHAREDIR = $$DATADIR/$${TARGET}/

  TARGET = fontshepherd
  target.path = $$BINDIR
  message("unix")
}
LIBS += -L../qhexedit2/$$DESTDIR -lqhexedit -lfreetype -licuuc -lpugixml -lboost_iostreams
INSTALLS += target
QMAKE_RPATHDIR += $${PREFIX}/lib/fontshepherd
DEFINES += SHAREDIR=\\\"$$SHAREDIR\\\"
