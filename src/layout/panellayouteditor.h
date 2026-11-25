#ifndef PANELLAYOUTEDITOR_H
#define PANELLAYOUTEDITOR_H

#include "panellayout.h"
#include <QObject>
#include <QPointer>
#include <map>
#include <optional>
#include <vector>

class PanelLayoutView;
class QMenu;
class QWidget;

// Edit mode for a PanelLayoutView: while enabled, a right-click (any context
// menu request) on the layout opens the layout menu for the node under it
// instead of the widget's own menu. Frontend class. See
// docs/layout-editing.md, "Edit mode".
class PanelLayoutEditor : public QObject {
  Q_OBJECT

public:
  // A child of view, so it goes with it. containerKinds are the containers
  // (splitters, boxes, Tabs) that Insert and Replace with offer, in order.
  PanelLayoutEditor(PanelLayoutView &view,
                    std::vector<PanelLayout::Kind> containerKinds);

  // Names a container kind in this editor's menus, in place of its default
  // name ("Box (Left to Right)", ...).
  void setContainerTitle(PanelLayout::Kind kind, const QString &title);
  // The root can't be replaced, deleted, cut or pasted over: its menu has
  // only its own items (a toolbar's row, Add Slot).
  void fixRoot();
  void setEnabled(bool enabled);

signals:
  // An edit changed the view's layout.
  void layoutChanged();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // The menu for the node at path; its actions edit the layout.
  QMenu *createMenu(const PanelLayout::Path &path, QWidget *parent);
  // Adds Insert or Replace with, Delete, Cut and Paste for the node at path.
  void addNodeItems(QMenu *menu, const PanelLayout::Path &path,
                    bool placeholder);
  // Applies an edit to a copy of the view's layout and shows the result.
  template <typename Edit> void edit(Edit &&change);
  // Asks for a name for the tab at path, prefilled with its label.
  void renameTab(const PanelLayout::Path &path);
  void showOverlay(QWidget *target);
  // A container kind's name, as a menu title and an Insert item.
  QString containerTitle(PanelLayout::Kind kind) const;

  PanelLayoutView &view_;
  const std::vector<PanelLayout::Kind> containerKinds_;
  std::map<PanelLayout::Kind, QString> containerTitles_;
  bool rootFixed_ = false;
  // The subtree of the last Cut, until it is pasted.
  std::optional<PanelLayout::Node> cut_;
  QPointer<QWidget> overlay_;
};

#endif // PANELLAYOUTEDITOR_H
