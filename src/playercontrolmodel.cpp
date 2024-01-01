#include "playercontrolmodel.h"

#include <QRandomGenerator>
#include <algorithm>

#include "qurl.h"
#include "songtablemodel.h"

PlayerControlModel::PlayerControlModel(IPlayList* parent) {
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
  return model->getUrl(playList[position]);
}

void PlayerControlModel::onPlayListChange() {
  indices = model->getSourceIndices();
}

int PlayerControlModel::currentRawIndex() const {
  return indices[playList[position]];
}

void PlayerControlModel::addToQueue(int i) {
  playList.append(i);
  qDebug() << playList;
}

// playback follows cursor
void PlayerControlModel::setNext(int idx) {
  qDebug() << "setNext" << idx;
  toBeInsertedIndex = idx;
}

QList<int> PlayerControlModel::getQueue() { return playList; }

void PlayerControlModel::shuffle() {
  int randomIndex = QRandomGenerator::global()->bounded(indices.size());
  // TODO
  emit indexChange(playList[position]);
}

void PlayerControlModel::handleInitialPlay() {
  if (playList.isEmpty()) {
    if (toBeInsertedIndex > 0) {
      playList.append(toBeInsertedIndex);
      toBeInsertedIndex = -1;
    } else {
      playList.append(0);
    }
  }
  position = 0;
}

void PlayerControlModel::moveIndex(int& index, bool isDirectionNext) {
  if (isDirectionNext) {
    if (index + 1 < indices.size()) {
      index++;
    } else {
      index = 0;
    }
  } else {
    if (index >= 1) {
      index--;
    } else {
      index = indices.size() - 1;
    }
  }
}

// current position + queue
void PlayerControlModel::next() {
  // Initial state, no history
  if (position == -1) {
    handleInitialPlay();
  } else {
    // default playback mode position is always 0 except for initial state
    // To be inserted position
    if (toBeInsertedIndex != playList[position] && toBeInsertedIndex >= 0) {
      qDebug() << "toBeInsertedIndex != playList[position] && "
                  "toBeInsertedIndex >= 0";
      playList.insert(1, toBeInsertedIndex);
      toBeInsertedIndex = -1;
    }

    // Queue
    if (position + 1 < playList.length()) {
      qDebug() << "queue";
      playList.removeFirst();
    } else {
      // No queue
      qDebug() << "no queue";
      moveIndex(playList[position]);
    }
  }
  qDebug() << "emit indexChange(playList[position]);" << playList[position];
  qDebug() << indices;
  emit indexChange(playList[position]);
}

void PlayerControlModel::previous() {
  moveIndex(playList[position], false);
  emit indexChange(playList[position]);
}

void PlayerControlModel::setCurrentIndex(int index) {
  if (position == -1) {
    playList.insert(0, index);
    position = 0;
    this->playList[position] = playList[position];
    emit indexChange(this->playList[position]);
    return;
  }
  playList.insert(1, index);
  qDebug() << "setCurrentIndex" << playList;
  next();
}
