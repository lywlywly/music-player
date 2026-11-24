#include "panellayouteditor.h"

#include "panellayoutview.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPalette>
#include <QWidget>
#include <utility>

namespace {
using Kind = PanelLayout::Kind;
using Path = PanelLayout::Path;

Path parentOf(const Path &path) { return Path(path.begin(), path.end() - 1); }
} // namespace

PanelLayoutEditor::PanelLayoutEditor(PanelLayoutView &view)
    : QObject(&view), view_(view) {}

void PanelLayoutEditor::setEnabled(bool enabled) {
  if (enabled) {
    qApp->installEventFilter(this);
  } else {
    qApp->removeEventFilter(this);
  }
}

bool PanelLayoutEditor::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() != QEvent::ContextMenu || !watched->isWidgetType()) {
    return false;
  }
  auto *widget = static_cast<QWidget *>(watched);
  const std::optional<Path> path = view_.pathOf(widget);
  if (!path) {
    return false;
  }
  QMenu *menu = createMenu(*path, widget->window());
  showOverlay(view_.widgetAt(*path));
  // Deleted from the event loop, after the chosen action ran: aboutToHide
  // comes before the action triggers.
  connect(menu, &QMenu::aboutToHide, this, [this, menu]() {
    delete overlay_;
    menu->deleteLater();
  });
  // popup, not exec: this filter returns at once, so an action runs from the
  // menu's own event, never while an event is being delivered to a layout
  // widget that the action's rebuild deletes.
  menu->popup(static_cast<QContextMenuEvent *>(event)->globalPos());
  return true;
}

template <typename Edit> void PanelLayoutEditor::edit(Edit &&change) {
  PanelLayout layout = view_.layout();
  change(layout);
  view_.setLayout(std::move(layout));
  emit layoutChanged();
}

QMenu *PanelLayoutEditor::createMenu(const Path &path, QWidget *parent) {
  const PanelLayout &layout = view_.layout();
  const PanelLayout::Node &node = layout.node(path);
  const bool placeholder = node.kind == Kind::Placeholder;
  auto *menu = new QMenu(parent);

  QString title;
  switch (node.kind) {
  case Kind::HSplitter:
    title = tr("Splitter (Left and Right)");
    break;
  case Kind::VSplitter:
    title = tr("Splitter (Top and Bottom)");
    break;
  case Kind::Tabs:
    title = tr("Tabs");
    break;
  case Kind::Element:
    title = view_.title(node.elementId);
    break;
  case Kind::Placeholder:
    title = tr("Empty");
    break;
  }
  menu->addAction(title)->setEnabled(false);
  menu->addSeparator();

  // The elements not in the tree, then the containers. A new container holds
  // the node (a placeholder stays one), so splitting keeps what was there.
  QMenu *insert =
      menu->addMenu(placeholder ? tr("Insert") : tr("Replace with"));
  const QStringList placed = layout.placedElements();
  for (const QString &id : view_.elementIds()) {
    if (!placed.contains(id)) {
      insert->addAction(view_.title(id), this, [this, path, id]() {
        edit([&](PanelLayout &l) { l.replaceWithElement(path, id); });
      });
    }
  }
  insert->addSeparator();
  insert->addAction(tr("Splitter (Left and Right)"), this, [this, path]() {
    edit([&](PanelLayout &l) { l.split(path, Kind::HSplitter); });
  });
  insert->addAction(tr("Splitter (Top and Bottom)"), this, [this, path]() {
    edit([&](PanelLayout &l) { l.split(path, Kind::VSplitter); });
  });
  insert->addAction(tr("Tabs"), this, [this, path]() {
    edit([&](PanelLayout &l) { l.wrapInTabs(path); });
  });

  if (!placeholder) {
    menu->addAction(tr("Delete"), this, [this, path]() {
      edit([&](PanelLayout &l) { l.take(path); });
    });
    menu->addAction(tr("Cut"), this, [this, path]() {
      edit([&](PanelLayout &l) { cut_ = l.take(path); });
    });
  }
  QAction *paste = menu->addAction(tr("Paste"), this, [this, path]() {
    edit([&](PanelLayout &l) { l.paste(path, *std::exchange(cut_, {})); });
  });
  paste->setEnabled(cut_.has_value());

  if (node.kind == Kind::Tabs) {
    menu->addSeparator();
    menu->addAction(tr("Add Tab"), this, [this, path]() {
      edit([&](PanelLayout &l) { l.addTab(path); });
    });
  }
  if (!path.empty() && layout.node(parentOf(path)).kind == Kind::Tabs) {
    const int index = path.back();
    const int count =
        static_cast<int>(layout.node(parentOf(path)).children.size());
    menu->addSeparator();
    menu->addAction(tr("Move Tab Left"), this,
                    [this, path]() {
                      edit([&](PanelLayout &l) { l.moveTab(path, -1); });
                    })
        ->setEnabled(index > 0);
    menu->addAction(tr("Move Tab Right"), this,
                    [this, path]() {
                      edit([&](PanelLayout &l) { l.moveTab(path, 1); });
                    })
        ->setEnabled(index < count - 1);
    // The dialog opens from the event loop, after the menu closed: its own
    // event loop must not run inside the action of a menu being deleted.
    menu->addAction(tr("Rename Tab..."), this, [this, path]() {
      QMetaObject::invokeMethod(
          this, [this, path]() { renameTab(path); }, Qt::QueuedConnection);
    });
    menu->addAction(tr("Remove Tab"), this,
                    [this, path]() {
                      edit([&](PanelLayout &l) { l.removeTab(path); });
                    })
        ->setEnabled(count > 1);
  }

  // Reaches a container whose children cover it.
  if (!path.empty()) {
    menu->addSeparator();
    QMenu *parentMenu = createMenu(parentOf(path), menu);
    parentMenu->setTitle(tr("Parent"));
    menu->addMenu(parentMenu);
  }
  return menu;
}

void PanelLayoutEditor::renameTab(const Path &path) {
  const PanelLayout &layout = view_.layout();
  const QString automatic = view_.automaticTabLabel(layout.node(path));
  const QString title = layout.node(parentOf(path)).tabTitles[path.back()];
  bool ok = false;
  const QString text =
      QInputDialog::getText(view_.widgetAt(path)->window(), tr("Rename Tab"),
                            tr("Name (empty: the names of its panels):"),
                            QLineEdit::Normal,
                            title.isEmpty() ? automatic : title, &ok)
          .trimmed();
  if (!ok) {
    return;
  }
  // A name equal to the panels' names is not kept, so the tab follows its
  // content again.
  edit([&](PanelLayout &l) {
    l.setTabTitle(path, text == automatic ? QString() : text);
  });
}

void PanelLayoutEditor::showOverlay(QWidget *target) {
  QWidget *window = target->window();
  auto *overlay = new QWidget(window);
  overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  QColor color = window->palette().color(QPalette::Highlight);
  color.setAlpha(80);
  QPalette palette = overlay->palette();
  palette.setColor(QPalette::Window, color);
  overlay->setPalette(palette);
  overlay->setAutoFillBackground(true);
  overlay->setGeometry(
      QRect(target->mapTo(window, QPoint(0, 0)), target->size()));
  overlay->show();
  overlay->raise();
  overlay_ = overlay;
}
