#include "mainwindowlayout.h"

#include <utility>

namespace {
using Kind = PanelLayout::Kind;
using Node = PanelLayout::Node;

Node element(const char *id) {
  return {.kind = Kind::Element, .elementId = id};
}

Node splitter(Kind kind, std::vector<int> sizes, Node first, Node second) {
  Node node{.kind = kind, .sizes = std::move(sizes)};
  node.children.push_back(std::move(first));
  node.children.push_back(std::move(second));
  return node;
}
} // namespace

PanelLayout MainWindowLayout::defaultLayout() {
  // The info side starts at a quarter of the width, not at its panels' size
  // hints (docs/architecture.md, "App composition"); Properties and the cover
  // share it 3 to 5.
  Node info = splitter(Kind::VSplitter, {300, 500}, element(kProperties),
                       element(kCover));
  return PanelLayout(splitter(Kind::HSplitter, {320, 960}, std::move(info),
                              element(kPlaylist)));
}
