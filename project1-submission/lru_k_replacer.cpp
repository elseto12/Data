//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

namespace bustub {

LRUKFrameRecord::LRUKFrameRecord(size_t frame_id, size_t k) : frame_id_(frame_id), k_(k) {}

auto LRUKFrameRecord::IsEvictable() const -> bool { return is_evictable_; }

auto LRUKFrameRecord::SetEvictable(bool is_evictable) -> void { is_evictable_ = is_evictable; }

auto LRUKFrameRecord::Access(uint64_t time) -> void {
  while (access_records_.size() >= k_) {
    access_records_.pop();
  }
  access_records_.push(time);
}

auto LRUKFrameRecord::LastKAccessTime() const -> uint64_t { return access_records_.front(); }

auto LRUKFrameRecord::EarliestAccessTime() const -> uint64_t { return access_records_.front(); }

auto LRUKFrameRecord::AccessSize() const -> size_t { return access_records_.size(); }

auto LRUKFrameRecord::GetFrameId() const -> size_t { return frame_id_; }

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : k_(k) { frames_.resize(num_frames, nullptr); }

// 驱逐一个frame，先驱逐不满k次的，再驱逐>=k次的
auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  if (lru_permature_.empty() && lru_mature_.empty()) {
    return false;
  }
  auto has_permature = !lru_permature_.empty();
  auto first_iter = has_permature ? lru_permature_.begin() : lru_mature_.begin();
  *frame_id = (*first_iter)->GetFrameId();
  DeallocateFrameRecord(first_iter, has_permature);
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (frames_[frame_id] == nullptr) {
    AllocateFrameRecord(frame_id);
  }
  auto is_permature = frames_[frame_id]->AccessSize() < k_;
  auto is_evictable = frames_[frame_id]->IsEvictable();
  // 已经是k-1次访问，这次访问后需要加入到mature中，所以先在permature中remove
  // 没达到k-1次访问的话，还是在permature中，记录新的访问时间
  if (is_evictable && is_permature && frames_[frame_id]->AccessSize() == (k_ - 1)) {
    // from premature to mature
    lru_permature_.erase(frames_[frame_id]);
  }
  // 已经在mature中，先删除在插入，lru
  if (is_evictable && (!is_permature)) {
    lru_mature_.erase(frames_[frame_id]);
  }
  frames_[frame_id]->Access(CurrTime());

  if (is_evictable && is_permature && frames_[frame_id]->AccessSize() == k_) {
    lru_mature_.insert(frames_[frame_id]);
  }

  if (is_evictable && (!is_permature)) {
    lru_mature_.insert(frames_[frame_id]);
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (frames_[frame_id] == nullptr) {
    return;
  }
  auto is_permature = frames_[frame_id]->AccessSize() < k_;
  if (set_evictable && !frames_[frame_id]->IsEvictable()) {
    // not evictable -> evictable
    replacer_size_++;
    if (is_permature) {
      lru_permature_.insert(frames_[frame_id]);
    } else {
      lru_mature_.insert(frames_[frame_id]);
    }
  }

  if (!set_evictable && frames_[frame_id]->IsEvictable()) {
    // evictable -> not evictable
    replacer_size_--;
    if (is_permature) {
      lru_permature_.erase(frames_[frame_id]);
    } else {
      lru_mature_.erase(frames_[frame_id]);
    }
  }

  frames_[frame_id]->SetEvictable(set_evictable);
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (frames_[frame_id] == nullptr) {
    return;
  }
  DeallocateFrameRecord(frame_id, frames_[frame_id]->AccessSize() < k_);
}

auto LRUKReplacer::Size() -> size_t { return replacer_size_; }

auto LRUKReplacer::AllocateFrameRecord(size_t frame_id) -> void {
  frames_[frame_id] = new LRUKFrameRecord(frame_id, k_);
  curr_size_++;
}

auto LRUKReplacer::DeallocateFrameRecord(size_t frame_id, bool is_permature) -> void {
  if (is_permature) {
    lru_permature_.erase(frames_[frame_id]);
  } else {
    lru_mature_.erase(frames_[frame_id]);
  }
  delete frames_[frame_id];
  frames_[frame_id] = nullptr;
  curr_size_--;
  replacer_size_--;
}

auto LRUKReplacer::DeallocateFrameRecord(LRUKReplacer::container_iterator it, bool is_permature) -> void {
  frame_id_t frame_to_delete = (*it)->GetFrameId();
  if (is_permature) {
    lru_permature_.erase(it);
  } else {
    lru_mature_.erase(it);
  }
  delete frames_[frame_to_delete];
  frames_[frame_to_delete] = nullptr;
  curr_size_--;
  replacer_size_--;
}

}  // namespace bustub