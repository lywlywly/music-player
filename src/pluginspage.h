#ifndef PLUGINSPAGE_H
#define PLUGINSPAGE_H

#include <QList>
#include <QWidget>

struct PluginEntry;
class QTreeWidget;
class QTreeWidgetItem;

// The Settings > Plugins page (docs/plugins.md, "Plugins page"): a row per
// library in the plugins folder with its name, file and state. A loaded or
// disabled plugin has a check box, which enables or disables it from the next
// startup.
class PluginsPage : public QWidget {
  Q_OBJECT

public:
  explicit PluginsPage(const QList<PluginEntry> &entries,
                       QWidget *parent = nullptr);

  // On the dialog's OK: stores the ids of the unchecked rows as the disabled
  // plugins, keeping the stored ids that no row with a check box has.
  void apply();

private:
  // The state column of a row with a check box, which follows the box.
  void updateState(QTreeWidgetItem *row);

  QTreeWidget *list_ = nullptr;
  // The rows with a check box; each holds its plugin's id and whether it is
  // loaded (kIdRole, kLoadedRole in the .cpp).
  QList<QTreeWidgetItem *> checkableRows_;
};

#endif // PLUGINSPAGE_H
