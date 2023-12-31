#include "playercontrolmodel.h"

#include <QRandomGenerator>
#include <algorithm>

#include "qurl.h"
#include "songtablemodel.h"

PlayerControlModel::PlayerControlModel(PlayList* parent) {
  model = parent;
  std::vector<std::pair<Field, QString>> myVector(fieldToStringMap.begin(),
                                                  fieldToStringMap.end());
  Field keyToFind = Field::FILE_PATH;
  auto it = std::find_if(
      myVector.begin(), myVector.end(),
      [keyToFind](const auto& pair) { return pair.first == keyToFind; });
  filePathColumnIndex = std::distance(myVector.begin(), it);
}

QUrl PlayerControlModel::getCurrentUrl() { return model->getUrl(index); }

void PlayerControlModel::onPlayListChange() {
  indices = model->getSourceIndices();
}

int PlayerControlModel::currentRawIndex() const { return indices[index]; }

void PlayerControlModel::addToQueue(int i) {
  playList.append(i);
  qDebug() << playList;
}

// playback follows cursor
void PlayerControlModel::setNext(int idx) {
  qDebug() << "setNext" << idx;
  toBeInsertedIndex = idx;
}

void PlayerControlModel::shuffle() {
  int randomIndex = QRandomGenerator::global()->bounded(indices.size());
  index = indices[randomIndex];
  emit indexChange(index);
}

// current position + queue
void PlayerControlModel::next() {
  qDebug() << "next()" << playList;
  // initPlayList();
  // Initial state, no history
  if (position == -1) {
    if (playList.isEmpty()) {
      if (toBeInsertedIndex > 0) {
        playList.append(toBeInsertedIndex);
        toBeInsertedIndex = -1;
      } else {
        playList.append(0);
      }
    }
    position = 0;
  } else {
    // default playback mode position is always 0 except for initial state
    position = 0;
    // To be inserted position
    if (toBeInsertedIndex != index && toBeInsertedIndex >= 0) {
      playList.insert(1, toBeInsertedIndex);
      toBeInsertedIndex = -1;
    }

    // Remove previous
    playList.removeAt(0);

    // Queue
    if (position < playList.length()) {
      qDebug() << "queue";
    } else {
      // No queue
      if (index + 1 < indices.size()) {
        index++;
      } else {
        index = 0;
      }

      playList.append(index);
    }
  }

  index = playList[position];
  emit indexChange(index);
}

void PlayerControlModel::previous() {
  if (index >= 1) {
    index--;
  } else {
    index = indices.size() - 1;
  }
  playList.append(index);
  position++;
  qDebug() << index;
  qDebug() << position << "in" << playList;
  emit indexChange(index);
}

void PlayerControlModel::setCurrentIndex(int index) {
  if (position == -1) {
    playList.insert(0, index);
    position = 0;
    this->index = playList[position];
    emit indexChange(this->index);
    return;
  }
  playList.insert(1, index);
  qDebug() << "setCurrentIndex" << playList;
  next();
}
