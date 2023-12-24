#include "playercontrolmodel.h"

#include <QRandomGenerator>
#include <algorithm>

#include "myproxymodel.h"
#include "qurl.h"
#include "songtablemodel.h"

PlayerControlModel::PlayerControlModel(MyProxyModel* parent) : QObject(parent) {
  model = parent;
  std::vector<std::pair<Field, QString>> myVector(fieldToStringMap.begin(),
                                                  fieldToStringMap.end());
  Field keyToFind = Field::FILE_PATH;
  auto it = std::find_if(
      myVector.begin(), myVector.end(),
      [keyToFind](const auto& pair) { return pair.first == keyToFind; });
  filePathColumnIndex = std::distance(myVector.begin(), it);
}

QUrl PlayerControlModel::getCurrentUrl() {
  return model->data(model->index(index, filePathColumnIndex)).toUrl();
}

void PlayerControlModel::onPlayListChange() {
  indices = model->getSourceIndices();
}

int PlayerControlModel::currentRawIndex() const { return indices[index]; }

void PlayerControlModel::shuffle() {
  int randomIndex = QRandomGenerator::global()->bounded(indices.size());
  index = indices[randomIndex];
  emit indexChange();
}

void PlayerControlModel::next() {
  if (index + 1 < indices.size()) {
    index++;
  } else {
    index = 0;
  }
  qDebug() << index;
  emit indexChange();
}

void PlayerControlModel::previous() {
  if (index >= 1) {
    index--;
  } else {
    index = indices.size() - 1;
  }
  qDebug() << index;
  emit indexChange();
}

void PlayerControlModel::setCurrentIndex(int index) { this->index = index; }
