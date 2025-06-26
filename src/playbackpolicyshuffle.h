#ifndef PLAYBACKPOLICYSHUFFLE_H
#define PLAYBACKPOLICYSHUFFLE_H

#include "playbackpolicy.h"

template <typename T> class BoundedSetWithHistory {
public:
  explicit BoundedSetWithHistory(size_t capacity)
      : capacity_(capacity), cursor_index_(-1) {}

  bool insert_back(const T &item) {
    if (capacity_ == 0)
      return false;

    if (set_.count(item))
      return true;

    if (queue_.size() == capacity_) {
      const T old = queue_.front();
      queue_.pop_front();
      set_.erase(old);
    }

    queue_.push_back(item);
    set_.insert(item);
    cursor_index_ = static_cast<int>(queue_.size()) - 1;

    return false;
  }

  bool insert_front(const T &item) {
    if (capacity_ == 0)
      return false;

    if (set_.count(item))
      return true;

    if (queue_.size() == capacity_) {
      const T old = queue_.back();
      queue_.pop_back();
      set_.erase(old);
    }

    queue_.push_front(item);
    set_.insert(item);
    cursor_index_ = 0;

    return false;
  }

  bool insert_cursor(const T &item) {
    if (capacity_ == 0)
      return false;

    if (queue_.size() == capacity_) {
      const T old = queue_.front();
      queue_.pop_front();
      set_.erase(old);
      cursor_index_--;
    }

    cursor_index_++;

    queue_.insert(queue_.begin() + cursor_index_, item);
    set_.insert(item);

    return false;
  }

  bool contains(const T &item) const { return set_.count(item) > 0; }

  size_t size() const { return queue_.size(); }

  T *prev() {
    if (has_prev()) {
      --cursor_index_;
      return &queue_[cursor_index_];
    }
    return nullptr;
  }

  T *next() {
    if (has_next()) {
      ++cursor_index_;
      return &queue_[cursor_index_];
    }
    return nullptr;
  }

  T *current() {
    if (cursor_index_ >= 0 && cursor_index_ < static_cast<int>(queue_.size()))
      return &queue_[cursor_index_];
    return nullptr;
  }

  void resize(int new_size) {
    if (new_size < capacity_) {
      throw std::runtime_error(
          "new_size should be greater than or equal to original capacity. ");
    }
    capacity_ = new_size;
  }

  bool has_prev() const { return cursor_index_ > 0; }

  bool has_next() const {
    return cursor_index_ >= 0 &&
           cursor_index_ + 1 < static_cast<int>(queue_.size());
  }

private:
  size_t capacity_;
  std::deque<T> queue_;
  std::unordered_set<T> set_;
  int cursor_index_; // -1 if invalid
};

class PlaybackPolicyShuffle : public PlaybackPolicy {
public:
  PlaybackPolicyShuffle();
  ~PlaybackPolicyShuffle() override;

  // PlaybackPolicy interface
public:
  void setPlaylist(const Playlist *) override;
  int nextPk() override;
  int prevPk() override;
  void setCurrentPk(int pk) override;

private:
  void reset() override;
  int getRandom(int, int);
  std::unique_ptr<BoundedSetWithHistory<int>> recentPks;
};

#endif // PLAYBACKPOLICYSEQUENTIAL_H
