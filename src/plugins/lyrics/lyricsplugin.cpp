#include "lyricsplugin.h"

#include "ipluginhost.h"
#include "lyricspanel.h"
#include "lyricspresenter.h"
#include "lyricssettingspage.h"
#include <utility>

void LyricsPlugin::initialize(IPluginHost &host) { host_ = &host; }

void LyricsPlugin::shutdown() {
  // The panels, with their presenters, were destroyed with the UI.
  presenters_.clear();
  host_ = nullptr;
}

QString LyricsPlugin::panelTitle() const { return tr("Lyrics"); }

QWidget *LyricsPlugin::createPanel(QWidget *parent) {
  auto *panel = new LyricsPanel(parent);
  panel->setObjectName("lyricsPanel");
  auto *presenter = new LyricsPresenter(panel, host_->player());
  presenter->applySettings();
  presenters_.append(presenter);
  return panel;
}

QString LyricsPlugin::settingsPageTitle() const { return tr("Lyrics"); }

IPluginSettingsPage *LyricsPlugin::createSettingsPage(QWidget *parent) {
  auto *page = new LyricsSettingsPage(parent);
  connect(page, &LyricsSettingsPage::applied, this, [this]() {
    for (LyricsPresenter *presenter : std::as_const(presenters_)) {
      if (presenter) {
        presenter->applySettings();
      }
    }
  });
  return page;
}
