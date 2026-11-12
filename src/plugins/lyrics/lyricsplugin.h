#ifndef LYRICSPLUGIN_H
#define LYRICSPLUGIN_H

#include "ipanelprovider.h"
#include "iplugin.h"
#include "isettingspageprovider.h"
#include <QList>
#include <QObject>
#include <QPointer>

class LyricsPresenter;

// The lyrics plugin's root object: the Lyrics info panel tab and its
// Settings page.
class LyricsPlugin : public QObject,
                     public IPlugin,
                     public IPanelProvider,
                     public ISettingsPageProvider {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID IPlugin_iid)
  Q_INTERFACES(IPlugin IPanelProvider ISettingsPageProvider)

public:
  QString id() const override;
  void initialize(IPluginHost &host) override;
  void shutdown() override;

  QString panelTitle() const override;
  // A lyrics panel, following the host's playback.
  QWidget *createPanel(QWidget *parent) override;

  QString settingsPageTitle() const override;
  // Applying the page restyles the open panels.
  IPluginSettingsPage *createSettingsPage(QWidget *parent) override;

private:
  IPluginHost *host_ = nullptr;
  // The open panels' presenters; each is deleted with its panel.
  QList<QPointer<LyricsPresenter>> presenters_;
};

#endif // LYRICSPLUGIN_H
