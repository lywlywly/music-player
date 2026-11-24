#ifndef PANELLAYOUTVIEW_H
#define PANELLAYOUTVIEW_H

#include "panellayout.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <optional>
#include <vector>

class QWidget;

// Builds the widgets of a PanelLayout in a host widget and places the
// elements in them. Frontend class. See docs/layout-editing.md.
//
// Each element is an existing widget, registered once; the view only moves
// it. An element not in the tree waits, hidden, in the store widget, a hidden
// widget the caller owns. Every setLayout rebuilds the containers. The view
// writes splitter moves and tab switches into its layout and reports them.
// The host must have no layout: the view gives it one for the root widget.
//
// A child of the host, created before the containers it builds in it, so it
// is destroyed before them and their signals on destruction reach nothing.
class PanelLayoutView : public QObject {
  Q_OBJECT

public:
  // placeholderText is shown in each empty slot, e.g. how to fill it.
  PanelLayoutView(QWidget *host, QWidget *store,
                  const QString &placeholderText);

  // Registers an element; the widget moves to the store until placed.
  void addElement(const QString &id, const QString &title, QWidget *widget);
  // The name of an element that is in a saved tree but not registered (its
  // widget does not exist), for its "not available" label.
  void setUnavailableTitle(const QString &id, const QString &title);
  // The registered elements' ids, in registration order.
  QStringList elementIds() const { return elementOrder_; }
  // An element's title, an unavailable element's name, or else its id.
  QString title(const QString &id) const;
  // The label of a tab holding node that has no name: the titles of the
  // elements in it, or "Empty".
  QString automaticTabLabel(const PanelLayout::Node &node) const;

  const PanelLayout &layout() const { return *layout_; }
  void setLayout(PanelLayout layout);

  // The node a widget in the layout belongs to: the nearest widget at or
  // above it that the view built or placed. nullopt outside the layout.
  std::optional<PanelLayout::Path> pathOf(QWidget *widget) const;
  QWidget *widgetAt(const PanelLayout::Path &path) const;

signals:
  // A splitter was moved or a tab switched; layout() has the change.
  void layoutChanged();

private:
  QWidget *build(const PanelLayout::Node &node, const PanelLayout::Path &path);
  void connectContainers();

  QWidget *host_;
  QWidget *store_;
  QString placeholderText_;
  QHash<QString, QWidget *> elements_;
  QStringList elementOrder_;
  QHash<QString, QString> titles_;
  // Set by the first setLayout.
  std::optional<PanelLayout> layout_;
  QWidget *root_ = nullptr;
  // Every widget the last build created or placed, with its node's path.
  QHash<QWidget *, PanelLayout::Path> paths_;
  std::vector<QMetaObject::Connection> connections_;
};

#endif // PANELLAYOUTVIEW_H
