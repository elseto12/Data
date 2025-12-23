//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetNextPageId(INVALID_PAGE_ID);
  SetSize(0);
  SetMaxSize(max_size);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { array_[index].first = key; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return array_[index].second; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { array_[index].second = value; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::PairAt(int index) const -> const MappingType & { return array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value, KeyComparator &comparator) -> bool {
  int low = 0, high = GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    auto comp_res = comparator(KeyAt(mid), key);
    if(comp_res >= 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  if(comparator(KeyAt(low), key) == 0)  {
    return false;
  }
  int insert_idx = GetSize();
  if(comparator(KeyAt(low), key) > 0) {
    insert_idx = low;
  }
  ExcavateIndex(insert_idx);
  SetKeyAt(insert_idx, key);
  SetValueAt(insert_idx, value);
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::FindGreaterEqualKeyPosition(const KeyType &key, KeyComparator &comparator) -> int {
  int low = 0, high = GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    auto comp_res = comparator(KeyAt(mid), key);
    if(comp_res >= 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  auto bigger_or_equal_key_idx = -1;
  if(comparator(KeyAt(low), key) >= 0) {
    bigger_or_equal_key_idx = low;
  }
  return bigger_or_equal_key_idx;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::FindKeyPosition(const KeyType &key, KeyComparator &comparator) -> int {
  int low = 0, high = GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    auto comp_res = comparator(KeyAt(mid), key);
    if(comp_res >= 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  if(comparator(KeyAt(low), key) == 0) {
    return low;
  }
  return -1;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveKey(const KeyType &key, KeyComparator &comparator) -> bool {
  auto to_delete_idx = FindKeyPosition(key, comparator);
  if (to_delete_idx == -1) {
    return false;
  }
  FillIndex(to_delete_idx + 1);  // from to_delete_idx + 1 pos shift left by 1
  DecreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  auto recipient_size = recipient->GetSize();
  auto size = GetSize();
  std::copy(&array_[0], &array_[size], &recipient->array_[recipient_size]);
  SetSize(0);
  recipient->IncreaseSize(size);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLatterHalfTo(BPlusTreeLeafPage *recipient) {
  BUSTUB_ASSERT(GetSize() == GetMaxSize(), "MoveLatterHalfTo(): Assert GetSize() == GetMaxSize()");
  auto remain_size = GetMaxSize() / 2 + (GetMaxSize() % 2 != 0);
  auto move_size = GetMaxSize() - remain_size;
  std::copy(&array_[remain_size], &array_[GetSize()], recipient->array_);
  SetSize(remain_size);
  recipient->SetSize(move_size);
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(recipient->GetPageId());
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  auto recipient_size = recipient->GetSize();
  recipient->SetKeyAt(recipient_size, KeyAt(0));
  recipient->SetValueAt(recipient_size, ValueAt(0));
  recipient->IncreaseSize(1);
  FillIndex(1);
  DecreaseSize(1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  auto size = GetSize();
  recipient->ExcavateIndex(0);
  recipient->SetKeyAt(0, KeyAt(size - 1));
  recipient->SetValueAt(0, ValueAt(size - 1));
  recipient->IncreaseSize(1);
  DecreaseSize(1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetMappingSize() -> size_t { return sizeof(MappingType); }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetArray() -> char * { return reinterpret_cast<char *>(&array_[0]); }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ExcavateIndex(int index) {
  std::copy_backward(array_ + index, array_ + GetSize(), array_ + GetSize() + 1);  // *(--d_last) = *(--last);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::FillIndex(int index) {
  std::copy(array_ + index, array_ + GetSize(), array_ + index - 1);
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
