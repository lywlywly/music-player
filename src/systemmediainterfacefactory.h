#ifndef SYSTEMMEDIAINTERFACEFACTORY_H
#define SYSTEMMEDIAINTERFACEFACTORY_H

#include "isystemmediainterface.h"

class QWidget;

// Creates the media interface for this platform, parented to window. Windows
// also uses window's native handle. Test builds return
// DummySystemMediaInterface when MYPLAYER_USE_DUMMY_MEDIA_INTERFACE is set.
ISystemMediaInterface *createSystemMediaInterface(QWidget *window);

#endif // SYSTEMMEDIAINTERFACEFACTORY_H
