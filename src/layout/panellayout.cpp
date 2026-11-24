#include "panellayout.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
constexpr const char *kTypeKey = "type";
constexpr const char *kIdKey = "id";
constexpr const char *kSizesKey = "sizes";
constexpr const char *kCurrentKey = "current";
// Tabs only, one per child; left out when no tab is named.
constexpr const char *kTitlesKey = "titles";
constexpr const char *kChildrenKey = "children";

struct TypeName {
  PanelLayout::Kind kind;
  const char *name;
};
constexpr TypeName kTypeNames[] = {
    {PanelLayout::Kind::HSplitter, "hsplitter"},
    {PanelLayout::Kind::VSplitter, "vsplitter"},
    {PanelLayout::Kind::HBox, "hbox"},
    {PanelLayout::Kind::VBox, "vbox"},
    {PanelLayout::Kind::Tabs, "tabs"},
    {PanelLayout::Kind::Element, "element"},
    {PanelLayout::Kind::Placeholder, "placeholder"},
};

bool isSplitter(PanelLayout::Kind kind) {
  return kind == PanelLayout::Kind::HSplitter ||
         kind == PanelLayout::Kind::VSplitter;
}

PanelLayout::Node elementNode(const QString &id) {
  return {.kind = PanelLayout::Kind::Element, .elementId = id};
}

PanelLayout::Node containerNode(PanelLayout::Kind kind, std::vector<int> sizes,
                                PanelLayout::Node first,
                                PanelLayout::Node second) {
  PanelLayout::Node node{.kind = kind, .sizes = std::move(sizes)};
  node.children.push_back(std::move(first));
  node.children.push_back(std::move(second));
  return node;
}

std::optional<PanelLayout::Node> nodeFromJson(const QJsonValue &value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const QJsonObject object = value.toObject();
  const QString type = object.value(kTypeKey).toString();
  PanelLayout::Node node;
  bool known = false;
  for (const TypeName &typeName : kTypeNames) {
    if (type == QLatin1String(typeName.name)) {
      node.kind = typeName.kind;
      known = true;
    }
  }
  if (!known) {
    return std::nullopt;
  }

  for (const QJsonValue &child : object.value(kChildrenKey).toArray()) {
    std::optional<PanelLayout::Node> childNode = nodeFromJson(child);
    if (!childNode) {
      return std::nullopt;
    }
    node.children.push_back(std::move(*childNode));
  }
  const size_t childCount = node.children.size();

  switch (node.kind) {
  case PanelLayout::Kind::HSplitter:
  case PanelLayout::Kind::VSplitter: {
    const QJsonArray sizes = object.value(kSizesKey).toArray();
    if (childCount != 2 || sizes.size() != 2) {
      return std::nullopt;
    }
    for (const QJsonValue &size : sizes) {
      if (!size.isDouble() || size.toInt(-1) < 0) {
        return std::nullopt;
      }
      node.sizes.push_back(size.toInt());
    }
    break;
  }
  case PanelLayout::Kind::HBox:
  case PanelLayout::Kind::VBox:
    if (childCount == 0) {
      return std::nullopt;
    }
    break;
  case PanelLayout::Kind::Tabs: {
    node.currentTab = object.value(kCurrentKey).toInt(-1);
    if (childCount == 0 || node.currentTab < 0 ||
        node.currentTab >= static_cast<int>(childCount)) {
      return std::nullopt;
    }
    node.tabTitles.resize(childCount);
    if (object.contains(kTitlesKey)) {
      const QJsonArray titles = object.value(kTitlesKey).toArray();
      if (titles.size() != static_cast<qsizetype>(childCount)) {
        return std::nullopt;
      }
      for (qsizetype i = 0; i < titles.size(); ++i) {
        if (!titles[i].isString()) {
          return std::nullopt;
        }
        node.tabTitles[i] = titles[i].toString();
      }
    }
    break;
  }
  case PanelLayout::Kind::Element:
    node.elementId = object.value(kIdKey).toString();
    if (node.elementId.isEmpty() || childCount != 0) {
      return std::nullopt;
    }
    break;
  case PanelLayout::Kind::Placeholder:
    if (childCount != 0) {
      return std::nullopt;
    }
    break;
  }
  return node;
}

QJsonObject nodeToJson(const PanelLayout::Node &node) {
  QJsonObject object;
  for (const TypeName &typeName : kTypeNames) {
    if (typeName.kind == node.kind) {
      object.insert(kTypeKey, QLatin1String(typeName.name));
    }
  }
  if (node.kind == PanelLayout::Kind::Element) {
    object.insert(kIdKey, node.elementId);
  }
  if (isSplitter(node.kind)) {
    object.insert(kSizesKey, QJsonArray{node.sizes[0], node.sizes[1]});
  }
  if (node.kind == PanelLayout::Kind::Tabs) {
    object.insert(kCurrentKey, node.currentTab);
    if (std::any_of(node.tabTitles.begin(), node.tabTitles.end(),
                    [](const QString &title) { return !title.isEmpty(); })) {
      object.insert(kTitlesKey,
                    QJsonArray::fromStringList(QStringList(
                        node.tabTitles.begin(), node.tabTitles.end())));
    }
  }
  if (!node.children.empty()) {
    QJsonArray children;
    for (const PanelLayout::Node &child : node.children) {
      children.append(nodeToJson(child));
    }
    object.insert(kChildrenKey, children);
  }
  return object;
}

void collectElements(const PanelLayout::Node &node, QStringList &ids) {
  if (node.kind == PanelLayout::Kind::Element) {
    ids.append(node.elementId);
  }
  for (const PanelLayout::Node &child : node.children) {
    collectElements(child, ids);
  }
}

// Turns each element of node whose id is in placed into a placeholder, and
// adds the others to placed.
void replaceRepeatedElements(PanelLayout::Node &node, QSet<QString> &placed) {
  if (node.kind == PanelLayout::Kind::Element) {
    if (placed.contains(node.elementId)) {
      node = PanelLayout::Node{};
    } else {
      placed.insert(node.elementId);
    }
  }
  for (PanelLayout::Node &child : node.children) {
    replaceRepeatedElements(child, placed);
  }
}
} // namespace

std::optional<PanelLayout> PanelLayout::fromJson(const QByteArray &json) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  std::optional<Node> root = nodeFromJson(document.object());
  if (!root) {
    return std::nullopt;
  }
  QSet<QString> placed;
  replaceRepeatedElements(*root, placed);
  return PanelLayout(std::move(*root));
}

PanelLayout::PanelLayout(Node root) : root_(std::move(root)) {}

QByteArray PanelLayout::toJson() const {
  return QJsonDocument(nodeToJson(root_)).toJson(QJsonDocument::Compact);
}

const PanelLayout::Node &PanelLayout::node(const Path &path) const {
  const Node *node = &root_;
  for (int index : path) {
    node = &node->children.at(index);
  }
  return *node;
}

PanelLayout::Node &PanelLayout::at(const Path &path) {
  return const_cast<Node &>(std::as_const(*this).node(path));
}

QStringList PanelLayout::placedElements() const {
  QStringList ids;
  collectElements(root_, ids);
  return ids;
}

void PanelLayout::replaceWithElement(const Path &path,
                                     const QString &elementId) {
  at(path) = elementNode(elementId);
}

void PanelLayout::split(const Path &path, Kind kind) {
  Node &node = at(path);
  // A box has no sizes: its children take their widgets' size hints.
  node = containerNode(
      kind, isSplitter(kind) ? std::vector<int>{1, 1} : std::vector<int>{},
      std::move(node), Node{});
}

void PanelLayout::wrapInTabs(const Path &path) {
  Node &node = at(path);
  Node tabs{.kind = Kind::Tabs, .tabTitles = {QString()}};
  tabs.children.push_back(std::move(node));
  node = std::move(tabs);
}

PanelLayout::Node PanelLayout::take(const Path &path) {
  return std::exchange(at(path), Node{});
}

void PanelLayout::paste(const Path &path, Node subtree) {
  at(path) = Node{};
  QSet<QString> placed;
  for (const QString &id : placedElements()) {
    placed.insert(id);
  }
  replaceRepeatedElements(subtree, placed);
  at(path) = std::move(subtree);
}

// The tabs-only fields (titles, current) are kept in step with the children;
// a box has none.
void PanelLayout::addChild(const Path &path) {
  Node &parent = at(path);
  parent.children.push_back({});
  if (parent.kind == Kind::Tabs) {
    parent.tabTitles.push_back({});
  }
}

void PanelLayout::moveChild(const Path &path, int delta) {
  Node &parent = at(Path(path.begin(), path.end() - 1));
  const int from = path.back();
  const int to = from + delta;
  std::swap(parent.children[from], parent.children[to]);
  if (parent.kind != Kind::Tabs) {
    return;
  }
  std::swap(parent.tabTitles[from], parent.tabTitles[to]);
  if (parent.currentTab == from) {
    parent.currentTab = to;
  } else if (parent.currentTab == to) {
    parent.currentTab = from;
  }
}

void PanelLayout::removeChild(const Path &path) {
  Node &parent = at(Path(path.begin(), path.end() - 1));
  const int index = path.back();
  parent.children.erase(parent.children.begin() + index);
  if (parent.kind != Kind::Tabs) {
    return;
  }
  parent.tabTitles.erase(parent.tabTitles.begin() + index);
  // The tab after the removed one becomes current, as in QTabWidget.
  if (parent.currentTab > index ||
      parent.currentTab == static_cast<int>(parent.children.size())) {
    --parent.currentTab;
  }
}

void PanelLayout::setTabTitle(const Path &path, const QString &title) {
  at(Path(path.begin(), path.end() - 1)).tabTitles[path.back()] = title;
}

void PanelLayout::setSizes(const Path &path, std::vector<int> sizes) {
  at(path).sizes = std::move(sizes);
}

void PanelLayout::setCurrentTab(const Path &path, int index) {
  at(path).currentTab = index;
}
