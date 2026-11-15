#include "abrepeatplugin.h"

#include "abrepeat.h"
#include "iplayerhost.h"
#include "ipluginhost.h"
#include <QAction>
#include <QMenu>
#include <optional>

namespace {
// m:ss, minutes unbounded.
QString formatTime(qint64 ms) {
  return QString("%1:%2")
      .arg(ms / 60000)
      .arg((ms / 1000) % 60, 2, 10, QChar('0'));
}

// The item's text, with the marked time when there is one.
QString itemText(const QString &text, std::optional<qint64> ms) {
  return ms ? QString("%1 (%2)").arg(text, formatTime(*ms)) : text;
}
} // namespace

ABRepeatPlugin::ABRepeatPlugin() = default;

ABRepeatPlugin::~ABRepeatPlugin() = default;

void ABRepeatPlugin::initialize(IPluginHost &host) {
  host_ = &host;
  repeat_ = std::make_unique<ABRepeat>(host.control());
  host.player().addListener(repeat_.get());
}

void ABRepeatPlugin::shutdown() {
  host_->player().removeListener(repeat_.get());
  repeat_.reset();
  host_ = nullptr;
}

QMenu *ABRepeatPlugin::createMenu(QWidget *parent) {
  auto *menu = new QMenu(tr("A-B Repeat"), parent);
  menu->setObjectName("abRepeatMenu");
  QAction *setA = menu->addAction(tr("Set A"));
  setA->setObjectName("abRepeatSetA");
  QAction *setB = menu->addAction(tr("Set B"));
  setB->setObjectName("abRepeatSetB");
  QAction *clear = menu->addAction(tr("Clear"));
  clear->setObjectName("abRepeatClear");
  connect(setA, &QAction::triggered, menu, [this]() { repeat_->setA(); });
  connect(setB, &QAction::triggered, menu, [this]() { repeat_->setB(); });
  connect(clear, &QAction::triggered, menu, [this]() { repeat_->clear(); });
  // The items are refreshed each time the menu opens, so the loop needs no
  // signal for its state.
  connect(menu, &QMenu::aboutToShow, menu, [this, setA, setB, clear]() {
    setA->setText(itemText(tr("Set A"), repeat_->a()));
    setB->setText(itemText(tr("Set B"), repeat_->b()));
    setA->setEnabled(repeat_->songLoaded());
    setB->setEnabled(repeat_->a().has_value());
    clear->setEnabled(repeat_->a().has_value());
  });
  return menu;
}
