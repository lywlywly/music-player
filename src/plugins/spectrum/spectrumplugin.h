#ifndef SPECTRUMPLUGIN_H
#define SPECTRUMPLUGIN_H

#include "ipanelprovider.h"
#include "iplugin.h"
#include <QObject>

// The spectrum plugin's root object: the Spectrum panel (a layout element).
class SpectrumPlugin : public QObject, public IPlugin, public IPanelProvider {
  Q_OBJECT
  // The id ("spectrum") and name, read by the host before loading.
  Q_PLUGIN_METADATA(IID IPlugin_iid FILE "spectrumplugin.json")
  Q_INTERFACES(IPlugin IPanelProvider)

public:
  void initialize(IPluginHost &host) override;
  void shutdown() override;

  QString panelTitle() const override;
  // A Spectrum panel, with its presenter as a child, following the host's
  // playback and reading its audio.
  QWidget *createPanel(QWidget *parent) override;

private:
  IPluginHost *host_ = nullptr;
};

#endif // SPECTRUMPLUGIN_H
