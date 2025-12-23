//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, set page id, set parent id and set
 * max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetSize(1);  // 第一个pair的key为空表示[-∞,key1)
  SetMaxSize(max_size);
}
/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { array_[index].first = key; }

/*
 * Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return array_[index].second; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { array_[index].second = value; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::SearchJumpIdx(const KeyType &key, KeyComparator &comparator) -> int {
  // find smallest i s.t. key <= array[i].key
  if(GetSize() == 1) {
    return 0;
  }
  int low = 1, high = GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    if(comparator(KeyAt(mid), key) > 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  if(comparator(KeyAt(low), key) > 0) {
    return low - 1;
  }
  else {
    return GetSize() - 1;
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::SearchPage(const KeyType &key, KeyComparator &comparator) -> ValueType {
  return ValueAt(SearchJumpIdx(key, comparator));
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value, KeyComparator &comparator)
    -> bool {
  int low = 1, high = GetSize() - 1;
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
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindKeyPosition(const KeyType &key, KeyComparator &comparator) -> int {
  int low = 1, high = GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    if(comparator(KeyAt(mid), key) >= 0) {
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
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveKey(const KeyType &key, KeyComparator &comparator) -> bool {
  auto to_delete_idx = FindKeyPosition(key, comparator);
  if (to_delete_idx == -1) {
    return false;
  }
  FillIndex(to_delete_idx + 1);
  DecreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient) {
  auto recipient_size = recipient->GetSize();
  auto size = GetSize();
  std::copy(&array_[0], &array_[size], &recipient->array_[recipient_size]);
  SetSize(0);
  recipient->IncreaseSize(size);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLatterHalfWithOneExtraTo(BPlusTreeInternalPage *recipient, const KeyType &key,
                                                                  const ValueType &value, KeyComparator &comparator) {
  BUSTUB_ASSERT(GetSize() == GetMaxSize(), "MoveLatterHalfWithOneExtraTo(): Assert GetSize() == GetMaxSize()");
  auto total_size = GetSize() + 1;
  Insert(key, value, comparator);
  auto remain_size = total_size / 2 + (total_size % 2 != 0);
  auto move_size = total_size - remain_size;
  std::copy(&array_[remain_size], &array_[total_size], recipient->array_);
  SetSize(remain_size);
  recipient->SetSize(move_size);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient) {
  auto recipient_size = recipient->GetSize();
  recipient->SetKeyAt(recipient_size, KeyAt(0));
  recipient->SetValueAt(recipient_size, ValueAt(0));
  recipient->IncreaseSize(1);
  FillIndex(1);
  DecreaseSize(1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient) {
  auto size = GetSize();
  recipient->ExcavateIndex(0);
  recipient->SetKeyAt(0, KeyAt(size - 1));
  recipient->SetValueAt(0, ValueAt(size - 1));
  recipient->IncreaseSize(1);
  DecreaseSize(1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetMappingSize() -> size_t { return sizeof(MappingType); }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetArray() -> char * { return reinterpret_cast<char *>(&array_[0]); }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::ExcavateIndex(int index) {
  std::copy_backward(array_ + index, array_ + GetSize(), array_ + GetSize() + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::FillIndex(int index) {
  std::copy(array_ + index, array_ + GetSize(), array_ + index - 1);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
