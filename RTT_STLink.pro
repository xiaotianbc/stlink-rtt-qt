QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 添加-lssp 修复报错：undefined reference to `__memcpy_chk'
win32: LIBS += -L$$PWD/./ -lstlink -lssp

INCLUDEPATH += $$PWD/.
INCLUDEPATH += $$PWD/include/stlink
INCLUDEPATH += $$PWD/include/libusb-1.0
DEPENDPATH += $$PWD/.

win32:!win32-g++: PRE_TARGETDEPS += $$PWD/./stlink.lib
else:win32-g++: PRE_TARGETDEPS += $$PWD/./libstlink.a

win32: LIBS += -L$$PWD/./ -llibusb-1.0

INCLUDEPATH += $$PWD/.
DEPENDPATH += $$PWD/.

win32:!win32-g++: PRE_TARGETDEPS += $$PWD/./libusb-1.0.lib
else:win32-g++: PRE_TARGETDEPS += $$PWD/./libusb-1.0.a
