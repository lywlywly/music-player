#ifndef PANELLAYOUTEDITOR_H
#define PANELLAYOUTEDITOR_H

#include "panellayout.h"
#include <QObject>
#include <QPointer>
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

  void setEnabled(bool enabled);

signals:
  // An edit changed the view's layout.
  void layoutChanged();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // The menu for the node at path; its actions edit the layout.
  QMenu *createMenu(const PanelLayout::Path &path, QWidget *parent);
  // Applies an edit to a copy of the view's layout and shows the result.
  template <typename Edit> void edit(Edit &&change);
  // Asks for a name for the tab at path, prefilled with its label.
  void renameTab(const PanelLayout::Path &path);
  void showOverlay(QWidget *target);

  PanelLayoutView &view_;
  const std::vector<PanelLayout::Kind> containerKinds_;
  // The subtree of the last Cut, until it is pasted.
  std::optional<PanelLayout::Node> cut_;
  QPointer<QWidget> overlay_;
};

#endif // PANELLAYOUTEDITOR_H
