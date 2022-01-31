QT += core widgets
TEMPLATE = lib

VERSION = 4.2.0

DEFINES += QHEXEDIT_EXPORTS

HEADERS = \
    qhexedit.h \
    chunks.h \
    commands.h


SOURCES = \
    qhexedit.cpp \
    chunks.cpp \
    commands.cpp

APPNAME=fontshepherd
LIBNAME=qhexedit

unix: {
  isEmpty (PREFIX) {
    PREFIX = /usr/local
  }
  TARGET = $$LIBNAME
  LIBDIR = $$PREFIX/lib/
  target.path = $$LIBDIR/$$APPNAME
  message("unix")
}
INSTALLS += target
