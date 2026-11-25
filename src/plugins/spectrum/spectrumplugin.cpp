#include "spectrumplugin.h"

#include "ipluginhost.h"
#include "spectrumpresenter.h"
#include "spectrumwidget.h"

void SpectrumPlugin::initialize(IPluginHost &host) { host_ = &host; }

// The panel, with its presenter, was destroyed with the UI.
void SpectrumPlugin::shutdown() { host_ = nullptr; }

QString SpectrumPlugin::panelTitle() const { return tr("Spectrum"); }

QWidget *SpectrumPlugin::createPanel(QWidget *parent) {
  auto *widget = new SpectrumWidget(parent);
  // The tests find it by name: across the DLL, their SpectrumWidget is not
  // this one, so findChild by type finds nothing.
  widget->setObjectName("spectrumWidget");
  widget->setMinimumSize(120, 240);
  new SpectrumPresenter(widget, host_->player(), host_->audio(), widget);
  return widget;
}
