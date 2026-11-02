#include "systemmediainterfacefactory.h"

#ifdef MYPLAYER_TESTING
#include "dummysystemmediainterface.h"
#endif
#ifdef Q_OS_MACOS
#include "macosmediacenter.h"
#elif defined(Q_OS_LINUX)
#include "mprismediainterface.h"
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windowsmediacenter.h"
#endif
#include <QWidget>

ISystemMediaInterface *createSystemMediaInterface(QWidget *window) {
#ifdef MYPLAYER_TESTING
  if (qEnvironmentVariableIntValue("MYPLAYER_USE_DUMMY_MEDIA_INTERFACE") != 0) {
    return new DummySystemMediaInterface(window);
  }
#endif
#ifdef Q_OS_MACOS
  return new MacOSMediaCenter(window);
#elif defined(Q_OS_LINUX)
  return new MprisMediaInterface(window);
#elif defined(Q_OS_WIN)
  return new WindowsMediaCenter(static_cast<quintptr>(window->winId()), window);
#else
  qFatal("createSystemMediaInterface: unsupported platform");
#endif
}
