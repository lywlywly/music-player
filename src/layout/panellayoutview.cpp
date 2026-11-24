#include "panellayoutview.h"

#include <QLabel>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QLabel *messageLabel(const QString &text) {
  auto *label = new QLabel(text);
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  label->setMargin(8);
  return label;
}

void collectTitles(const PanelLayout::Node &node, const PanelLayoutView &view,
                   QStringList &titles) {
  if (node.kind == PanelLayout::Kind::Element) {
    titles.append(view.title(node.elementId));
  }
  for (const PanelLayout::Node &child : node.children) {
    collectTitles(child, view, titles);
  }
}
} // namespace

PanelLayoutView::PanelLayoutView(QWidget *host, QWidget *store,
                                 const QString &placeholderText)
    : QObject(host), host_(host), store_(store),
      placeholderText_(placeholderText) {
  auto *hostLayout = new QVBoxLayout(host_);
  hostLayout->setContentsMargins(0, 0, 0, 0);
}

void PanelLayoutView::addElement(const QString &id, const QString &title,
                                 QWidget *widget) {
  widget->setParent(store_);
  elements_.insert(id, widget);
  elementOrder_.append(id);
  titles_.insert(id, title);
}

void PanelLayoutView::setUnavailableTitle(const QString &id,
                                          const QString &title) {
  titles_.insert(id, title);
}

QString PanelLayoutView::title(const QString &id) const {
  return titles_.value(id, id);
}

void PanelLayoutView::setLayout(PanelLayout layout) {
  // Disconnected first: taking the elements out of a QTabWidget, and adding
  // its first page, emit currentChanged, which would write a stale or zero
  // index into the new tree.
  for (const QMetaObject::Connection &connection : connections_) {
    disconnect(connection);
  }
  connections_.clear();
  // The elements go back to the store before the containers are deleted,
  // since a QSplitter or QTabWidget deletes its children. Hidden explicitly,
  // so a widget that follows its visibility sees it hide.
  for (QWidget *element : std::as_const(elements_)) {
    if (element->parentWidget() != store_) {
      element->hide();
      element->setParent(store_);
    }
  }
  // An element as the root is in the store now; anything else the view built.
  if (root_ != nullptr && root_->parentWidget() != store_) {
    delete root_;
  }
  paths_.clear();

  layout_ = std::move(layout);
  root_ = build(layout_->root(), {});
  host_->layout()->addWidget(root_);
  root_->show();
  connectContainers();
}

QWidget *PanelLayoutView::build(const PanelLayout::Node &node,
                                const PanelLayout::Path &path) {
  QWidget *widget = nullptr;
  switch (node.kind) {
  case PanelLayout::Kind::HSplitter:
  case PanelLayout::Kind::VSplitter: {
    auto *splitter =
        new QSplitter(node.kind == PanelLayout::Kind::HSplitter ? Qt::Horizontal
                                                                : Qt::Vertical);
    splitter->setHandleWidth(2);
    for (int i = 0; i < 2; ++i) {
      PanelLayout::Path childPath = path;
      childPath.push_back(i);
      QWidget *child = build(node.children[i], childPath);
      splitter->addWidget(child);
      // An element parked in the store was hidden explicitly.
      child->show();
    }
    // Scaled to the splitter's size by their ratio.
    splitter->setSizes({node.sizes[0], node.sizes[1]});
    widget = splitter;
    break;
  }
  case PanelLayout::Kind::Tabs: {
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    for (int i = 0; i < static_cast<int>(node.children.size()); ++i) {
      PanelLayout::Path childPath = path;
      childPath.push_back(i);
      // The tab widget shows its current page and hides the others.
      const QString &title = node.tabTitles[i];
      tabs->addTab(build(node.children[i], childPath),
                   title.isEmpty() ? automaticTabLabel(node.children[i])
                                   : title);
    }
    tabs->setCurrentIndex(node.currentTab);
    widget = tabs;
    break;
  }
  case PanelLayout::Kind::Element:
    widget = elements_.value(node.elementId);
    if (widget == nullptr) {
      widget =
          messageLabel(tr("%1 is not available").arg(title(node.elementId)));
    }
    break;
  case PanelLayout::Kind::Placeholder:
    widget = messageLabel(placeholderText_);
    break;
  }
  paths_.insert(widget, path);
  return widget;
}

QString
PanelLayoutView::automaticTabLabel(const PanelLayout::Node &node) const {
  QStringList titles;
  collectTitles(node, *this, titles);
  return titles.isEmpty() ? tr("Empty") : titles.join(QStringLiteral(" / "));
}

void PanelLayoutView::connectContainers() {
  for (auto it = paths_.cbegin(); it != paths_.cend(); ++it) {
    const PanelLayout::Path path = it.value();
    if (auto *splitter = qobject_cast<QSplitter *>(it.key())) {
      connections_.push_back(connect(
          splitter, &QSplitter::splitterMoved, this, [this, splitter, path]() {
            const QList<int> sizes = splitter->sizes();
            layout_->setSizes(path, {sizes[0], sizes[1]});
            emit layoutChanged();
          }));
    } else if (auto *tabs = qobject_cast<QTabWidget *>(it.key())) {
      connections_.push_back(connect(tabs, &QTabWidget::currentChanged, this,
                                     [this, path](int index) {
                                       layout_->setCurrentTab(path, index);
                                       emit layoutChanged();
                                     }));
    }
  }
}

std::optional<PanelLayout::Path>
PanelLayoutView::pathOf(QWidget *widget) const {
  for (; widget != nullptr; widget = widget->parentWidget()) {
    const auto it = paths_.constFind(widget);
    if (it != paths_.cend()) {
      return it.value();
    }
  }
  return std::nullopt;
}

QWidget *PanelLayoutView::widgetAt(const PanelLayout::Path &path) const {
  for (auto it = paths_.cbegin(); it != paths_.cend(); ++it) {
    if (it.value() == path) {
      return it.key();
    }
  }
  return nullptr;
}
