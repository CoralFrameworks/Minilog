/*
 * Copyright (C) 2026 The Coral Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINILOG_MESSAGE_QUEUE_H
#define MINILOG_MESSAGE_QUEUE_H

#include <cassert>
#include <mutex>
#include <vector>

namespace minilog {

template<typename T>
class MessageQueue {
  size_t maxItems = 0;
  std::vector<T>::size_type mHead = 0;
  std::vector<T>::size_type mTail = 0;
  std::vector<T> mValue;

public:
  using value_type = T;

  MessageQueue() = default;

  /**
   * A ring buffer that keeps at most `capacity` items.
   *
   * One slot stays unused so that `mHead == mTail` can mean empty; the vector itself must therefore hold
   * `capacity + 1` slots. Using `capacity` here made the fourth push of a three-item queue write past the end
   * of the vector.
   */
  explicit MessageQueue(size_t capacity)
    : maxItems(capacity + 1),
      mValue(capacity + 1) {
  }

  MessageQueue(const MessageQueue&) = default;
  MessageQueue& operator=(const MessageQueue&) = default;

  MessageQueue(MessageQueue&& other) noexcept {
    maxItems = other.maxItems;
    mHead = other.mHead;
    mTail = other.mTail;
    mValue = std::move(other.mValue);

    other.maxItems = 0;
    other.mHead = other.mTail = 0;
  }

  MessageQueue& operator=(MessageQueue&& other) noexcept {
    maxItems = other.maxItems;
    mHead = other.mHead;
    mTail = other.mTail;
    mValue = std::move(other.mValue);

    other.maxItems = 0;
    other.mHead = other.mTail = 0;
    return *this;
  }

  void push_back(T&& item) {
    if (maxItems > 0) {
      mValue[mTail] = std::move(item);
      mTail = (mTail + 1) % maxItems;

      // overrun last item if full
      if (mTail == mHead) {
        mHead = (mHead + 1) % maxItems;
      }
    }
  }

  /**
   * Return reference to the front item.
   * If there are no elements in the container, the behavior is undefined.
   */
  const T& front() const { return mValue[mHead]; }

  T& front() { return mValue[mHead]; }

  /**
   * Return number of elements actually stored
   */
  [[nodiscard]] size_t size() const {
    if (mTail >= mHead) {
      return mTail - mHead;
    }
    return maxItems - (mHead - mTail);
  }

  /**
   * Return const reference to item by index.
   *
   * If index is out of range 0…size()-1, the behavior is undefined.
   */
  const T& at(size_t i) const {
    assert(i < size());
    return mValue[(mHead + i) % maxItems];
  }

  /**
   * Pop item from front.
   * If there are no elements in the container, the behavior is undefined.
   */
  void pop_front() { mHead = (mHead + 1) % maxItems; }

  [[nodiscard]] bool empty() const { return mHead == mTail; }

  [[nodiscard]] bool full() const {
    // head is ahead of the tail by 1
    if (maxItems > 0) {
      return ((mTail + 1) % maxItems) == mHead;
    }
    return false;
  }
};

} // namespace minilog

#endif //MINILOG_MESSAGE_QUEUE_H
