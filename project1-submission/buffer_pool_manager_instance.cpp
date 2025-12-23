//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }

  // TODO(students): remove this line after you have implemented the buffer pool
  // manager
  /*
  throw NotImplementedException(
      "BufferPoolManager is not implemented yet. If you have finished "
      "implementing BPM, please remove the throw "
      "exception line in `buffer_pool_manager_instance.cpp`.");
  */
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  // first check if there is any available frame
  frame_id_t available_frame_id = -1;
  if (!FindVictim(&available_frame_id)) {
    return nullptr;
  }
  // at this point , we know there is indeed available frame
  page_id_t new_page_id = AllocatePage();
  // reset memory and mata for new page
  pages_[available_frame_id].ResetMemory();
  pages_[available_frame_id].page_id_ = new_page_id;
  pages_[available_frame_id].pin_count_ = 1;
  pages_[available_frame_id].is_dirty_ = false;
  // let replacer track this new page
  replacer_->RecordAccess(available_frame_id);
  replacer_->SetEvictable(available_frame_id, false);
  // maping pageid
  page_table_->Insert(new_page_id, available_frame_id);
  *page_id = new_page_id;
  return &pages_[available_frame_id];
}

// fetch 和 newpage的区别是：new是在buffer pool中加入一个新的页面，而fetch是获得在buffer
// pool中已有的页面,没有再尝试evict a frame and read page_id page from disk
auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  // first search the page_id in buffer pool
  frame_id_t existing_frame_id = -1;
  if (page_table_->Find(page_id, existing_frame_id)) {
    // this page in buffer pool
    replacer_->RecordAccess(existing_frame_id);
    replacer_->SetEvictable(existing_frame_id, false);
    pages_[existing_frame_id].pin_count_++;
    return &pages_[existing_frame_id];
  }
  // not currently in buffer pool, need first try to evict a frame out,then read this page from disk to memory
  frame_id_t available_frame_id = -1;
  if (!FindVictim(&available_frame_id)) {
    // no evictable frame
    return nullptr;
  }
  pages_[available_frame_id].ResetMemory();
  pages_[available_frame_id].page_id_ = page_id;
  pages_[available_frame_id].pin_count_ = 1;
  pages_[available_frame_id].is_dirty_ = false;
  // fetch the required page's actual data from disk
  disk_manager_->ReadPage(page_id, pages_[available_frame_id].GetData());

  replacer_->RecordAccess(available_frame_id);
  replacer_->SetEvictable(available_frame_id, false);
  page_table_->Insert(page_id, available_frame_id);
  return &pages_[available_frame_id];
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    // page id page is not in buffer pool
    return false;
  }
  if (pages_[frame_id].GetPinCount() == 0) {
    return false;
  }
  if (--pages_[frame_id].pin_count_ == 0) {
    // pin count reaches 0, become evictable
    replacer_->SetEvictable(frame_id, true);
  }
  if (is_dirty) {
    pages_[frame_id].is_dirty_ = is_dirty;
  }
  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    // page id page is not in buffer pool
    return false;
  }
  // flush
  disk_manager_->WritePage(page_id, pages_[frame_id].GetData());
  pages_[frame_id].is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::scoped_lock<std::mutex> lock(latch_);
  for (size_t i = 0; i < pool_size_; i++) {
    if (pages_[i].GetPageId() != INVALID_PAGE_ID) {
      disk_manager_->WritePage(pages_[i].GetPageId(), pages_[i].GetData());
      pages_[i].is_dirty_ = false;
    }
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  frame_id_t frame_id = -1;
  if (!page_table_->Find(page_id, frame_id)) {
    // page id page is not in buffer pool
    return true;
  }
  if (pages_[frame_id].pin_count_ != 0) {
    // currently pined cannot be deleted
    return false;
  }
  // if dirty , write back to disk
  if (pages_[frame_id].IsDirty()) {
    disk_manager_->WritePage(pages_[frame_id].GetPageId(), pages_[frame_id].GetData());
    pages_[frame_id].is_dirty_ = false;
  }
  page_table_->Remove(page_id);
  replacer_->Remove(frame_id);
  free_list_.push_back(frame_id);
  // reset page metadata
  pages_[frame_id].ResetMemory();
  pages_[frame_id].ResetMemory();
  pages_[frame_id].ResetMemory();
  pages_[frame_id].is_dirty_ = false;
  // imitate free the page on disk
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManagerInstance::FindVictim(frame_id_t *available_frame_id) -> bool {
  if (!free_list_.empty()) {
    *available_frame_id = free_list_.back();
    free_list_.pop_back();
    return true;
  }
  // try evict a page
  if (replacer_->Evict(available_frame_id)) {
    // evict a frame, check it if dirty
    if (pages_[*available_frame_id].IsDirty()) {
      disk_manager_->WritePage(pages_[*available_frame_id].GetPageId(), pages_[*available_frame_id].GetData());
      pages_[*available_frame_id].is_dirty_ = false;
    }
    // remove maping of page_id to frame_id
    page_table_->Remove(pages_[*available_frame_id].GetPageId());
    return true;
  }
  // no free and vitim found
  return false;
}

}  // namespace bustub