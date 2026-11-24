#ifndef IPANELPROVIDER_H
#define IPANELPROVIDER_H

#include <QObject>
#include <QString>

class QWidget;

// Capability: a panel that the user can place in the main window's layout.
class IPanelProvider {
public:
  virtual ~IPanelProvider() = default;

  // The panel's title in the layout: its tab text and its menu entry.
  virtual QString panelTitle() const = 0;
  // Returns a new panel parented to parent, which owns and deletes it. Called
  // once, at startup.
  virtual QWidget *createPanel(QWidget *parent) = 0;
};

#define IPanelProvider_iid "myplayer.IPanelProvider/1"
Q_DECLARE_INTERFACE(IPanelProvider, IPanelProvider_iid)

#endif // IPANELPROVIDER_H
