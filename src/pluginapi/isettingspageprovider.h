#ifndef ISETTINGSPAGEPROVIDER_H
#define ISETTINGSPAGEPROVIDER_H

#include <QObject>
#include <QString>

class QWidget;

// A page of the Settings dialog. It lives as long as its widget().
class IPluginSettingsPage {
public:
  virtual ~IPluginSettingsPage() = default;

  virtual QWidget *widget() = 0;
  // Saves the page's settings and applies them; called on the dialog's OK.
  virtual void apply() = 0;
};

// Capability: a page of the Settings dialog, under Plugins in its tree.
class ISettingsPageProvider {
public:
  virtual ~ISettingsPageProvider() = default;

  virtual QString settingsPageTitle() const = 0;
  // Returns a new page whose widget() is parented to parent, which owns and
  // deletes it.
  virtual IPluginSettingsPage *createSettingsPage(QWidget *parent) = 0;
};

#define ISettingsPageProvider_iid "myplayer.ISettingsPageProvider/1"
Q_DECLARE_INTERFACE(ISettingsPageProvider, ISettingsPageProvider_iid)

#endif // ISETTINGSPAGEPROVIDER_H
