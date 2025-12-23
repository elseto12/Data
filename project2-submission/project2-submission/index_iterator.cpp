/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(page_id_t page_id, int index, B_PLUS_TREE_LEAF_PAGE_TYPE *page,
                                  BufferPoolManager *bpm)
    : page_id_(page_id), idx_(index), leaf_page_(page), bpm_(bpm) {
  BUSTUB_ASSERT(page->GetPageId() == page_id, "page->GetPageId() == page_id");
  BUSTUB_ASSERT(index < page->GetSize(), "index < page->GetSize()");
  BUSTUB_ASSERT(bpm != nullptr, "bpm != nullptr");
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() {
  if (page_id_ != INVALID_PAGE_ID) {
    bpm_->UnpinPage(page_id_, false);
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return page_id_ == INVALID_PAGE_ID; }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> const MappingType & {
  BUSTUB_ASSERT(page_id_ != INVALID_PAGE_ID, "page_id_ != INVALID_PAGE_ID");
  BUSTUB_ASSERT(leaf_page_->GetPageId() == page_id_, "leaf_page_->GetPageId() == page_id_");
  BUSTUB_ASSERT(idx_ < leaf_page_->GetSize(), "idx_ < leaf_page_->GetSize()");
  return leaf_page_->PairAt(idx_);
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  if (IsEnd()) {
    return *this;
  }
  idx_++;
  if (idx_ == leaf_page_->GetSize()) {
    // try fetch next page
    page_id_ = leaf_page_->GetNextPageId();
    if (page_id_ == INVALID_PAGE_ID) {
      // reach end。unpin 已经遍历完的 page
      bpm_->UnpinPage(leaf_page_->GetPageId(), false);
      leaf_page_ = nullptr;
      idx_ = -1;
    } else {
      bpm_->UnpinPage(leaf_page_->GetPageId(), false);
      leaf_page_ = FetchLeafPage(page_id_);
      idx_ = 0;
    }
  }
  return *this;
}
INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::FetchLeafPage(page_id_t page_id) -> B_PLUS_TREE_LEAF_PAGE_TYPE * {
  if (page_id == INVALID_PAGE_ID) {
    return nullptr;
  }
  return reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE *>(bpm_->FetchPage(page_id)->GetData());
}
INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator==(const IndexIterator &itr) const -> bool {
  return itr.page_id_ == page_id_ && itr.idx_ == idx_;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator!=(const IndexIterator &itr) const -> bool { return !operator==(itr); }

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub