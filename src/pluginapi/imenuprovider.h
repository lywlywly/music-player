#ifndef IMENUPROVIDER_H
#define IMENUPROVIDER_H

#include <QObject>

class QMenu;
class QWidget;

// Capability: a submenu at the end of the Playback menu.
class IMenuProvider {
public:
  virtual ~IMenuProvider() = default;

  // Returns a new menu, with its title set, parented to parent, which owns
  // and deletes it.
  virtual QMenu *createMenu(QWidget *parent) = 0;
};

#define IMenuProvider_iid "myplayer.IMenuProvider/1"
Q_DECLARE_INTERFACE(IMenuProvider, IMenuProvider_iid)

#endif // IMENUPROVIDER_H
