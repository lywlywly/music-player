#ifndef IPANELPROVIDER_H
#define IPANELPROVIDER_H

#include <QObject>
#include <QString>

class QWidget;

// Capability: a tab in the info panel.
class IPanelProvider {
public:
  virtual ~IPanelProvider() = default;

  virtual QString panelTitle() const = 0;
  // Returns a new panel parented to parent, which owns and deletes it.
  virtual QWidget *createPanel(QWidget *parent) = 0;
};

#define IPanelProvider_iid "myplayer.IPanelProvider/1"
Q_DECLARE_INTERFACE(IPanelProvider, IPanelProvider_iid)

#endif // IPANELPROVIDER_H
