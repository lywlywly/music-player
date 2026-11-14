#include "pluginspage.h"

#include "pluginmanager.h"
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace {
enum Column { NameColumn, FileColumn, StateColumn };
constexpr int kIdRole = Qt::UserRole;
constexpr int kLoadedRole = Qt::UserRole + 1;
} // namespace

PluginsPage::PluginsPage(const QList<PluginEntry> &entries, QWidget *parent)
    : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  auto *note = new QLabel(
      tr("Enabling or disabling a plugin takes effect after a restart."), this);
  note->setWordWrap(true);
  layout->addWidget(note);

  list_ = new QTreeWidget(this);
  list_->setObjectName("plugins_list");
  list_->setRootIsDecorated(false);
  list_->setHeaderLabels({tr("Name"), tr("File"), tr("State")});
  list_->header()->setSectionResizeMode(NameColumn,
                                        QHeaderView::ResizeToContents);
  list_->header()->setSectionResizeMode(FileColumn,
                                        QHeaderView::ResizeToContents);
  layout->addWidget(list_);

  for (const PluginEntry &entry : entries) {
    auto *row = new QTreeWidgetItem(list_);
    row->setText(NameColumn,
                 entry.name.isEmpty() ? entry.fileName : entry.name);
    row->setText(FileColumn, entry.fileName);
    if (entry.state == PluginEntry::State::Skipped) {
      row->setText(StateColumn, tr("Not loaded: %1").arg(entry.reason));
      // Qt's errorString() names the file's path: too long for the column.
      for (int column = NameColumn; column <= StateColumn; ++column) {
        row->setToolTip(column, entry.details);
      }
      continue;
    }
    const bool loaded = entry.state == PluginEntry::State::Loaded;
    row->setData(NameColumn, kIdRole, entry.id);
    row->setData(NameColumn, kLoadedRole, loaded);
    row->setCheckState(NameColumn, loaded ? Qt::Checked : Qt::Unchecked);
    checkableRows_.append(row);
    updateState(row);
  }
  // Only a row with a check box changes in the name column after this.
  connect(list_, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem *row, int column) {
            if (column == NameColumn) {
              updateState(row);
            }
          });
}

void PluginsPage::updateState(QTreeWidgetItem *row) {
  const bool loaded = row->data(NameColumn, kLoadedRole).toBool();
  const bool checked = row->checkState(NameColumn) == Qt::Checked;
  if (loaded) {
    row->setText(StateColumn,
                 checked ? tr("Enabled") : tr("Disabled after restart"));
  } else {
    row->setText(StateColumn,
                 checked ? tr("Enabled after restart") : tr("Disabled"));
  }
}

void PluginsPage::apply() {
  QStringList disabled;
  QStringList listed;
  for (const QTreeWidgetItem *row : std::as_const(checkableRows_)) {
    const QString id = row->data(NameColumn, kIdRole).toString();
    listed << id;
    if (row->checkState(NameColumn) == Qt::Unchecked) {
      disabled << id;
    }
  }
  // A plugin whose file is gone, or that is skipped for now, stays disabled
  // when it comes back.
  for (const QString &id : loadDisabledPluginIds()) {
    if (!listed.contains(id)) {
      disabled << id;
    }
  }
  saveDisabledPluginIds(disabled);
}
