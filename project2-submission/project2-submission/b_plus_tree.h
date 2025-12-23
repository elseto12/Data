//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/b_plus_tree.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "concurrency/transaction.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator>

/**
 * Main class providing the API for the Interactive B+ Tree.
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTree {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  // control flag for latch crabbing
  // OPTIMIZE 对于 latch crabbing，存在一种比较简单的优化。在普通的 latch crabbing 中，Insert/Delete
  // 均需对节点上写锁，而越上层的节点被访问的可能性越大，锁竞争也越激烈，频繁对上层节点上互斥的写锁对性能影响较大。因此可以做出如下优化：
  /*
    Search 操作不变，在 Insert / Delete 操作中，我们可以先乐观地认为不会发生 split / steal /
      merge，对沿途的节点上读锁，并及时释放，对 leaf page 上写锁。当发现操作对 leaf page 确实不会造成 split / steal /
      merge 时，可以直接完成操作。当发现操作会使 leaf page split / steal /
      merge 时，则放弃所有持有的锁，从 root page 开始重新悲观地进行这次操作，即沿途上写锁
  */
  enum class LatchMode { READ, INSERT, DELETE, OPTIMIZE };
  explicit BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                     int leaf_max_size = LEAF_PAGE_SIZE, int internal_max_size = INTERNAL_PAGE_SIZE);

  // Returns true if this B+ tree has no keys and values.
  auto IsEmpty() const -> bool;

  // Insert a key-value pair into this B+ tree.
  auto Insert(const KeyType &key, const ValueType &value, Transaction *transaction = nullptr) -> bool;

  auto InsertHelper(const KeyType &key, const ValueType &value, Transaction *transaction, LatchMode mode) -> bool;

  // Remove a key and its value from this B+ tree.
  void Remove(const KeyType &key, Transaction *transaction = nullptr);
  void RemoveHelper(const KeyType &key, Transaction *transaction, LatchMode mode);

  // return the value associated with a given key
  auto GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction = nullptr) -> bool;

  // return the page id of the root node
  auto GetRootPageId() -> page_id_t;

  // index iterator
  auto Begin() -> INDEXITERATOR_TYPE;
  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;
  auto End() -> INDEXITERATOR_TYPE;

  // print the B+ tree
  void Print(BufferPoolManager *bpm);

  // draw the B+ tree
  void Draw(BufferPoolManager *bpm, const std::string &outf);

  // read data from file and insert one by one
  void InsertFromFile(const std::string &file_name, Transaction *transaction = nullptr);

  // read data from file and remove one by one
  void RemoveFromFile(const std::string &file_name, Transaction *transaction = nullptr);

 private:
  auto FindLeafPage(const KeyType &key, Transaction *transaction = nullptr, LatchMode mode = LatchMode::READ)
      -> std::pair<Page *, BPlusTreeLeafPage<KeyType, RID, KeyComparator> *>;

  auto FetchBPlusTreePage(page_id_t page_id) -> std::pair<Page *, BPlusTreePage *>;

  void UpdateRootPageId(int insert_record = 0);

  auto ReinterpretAsInternalPage(BPlusTreePage *page) -> InternalPage *;
  auto ReinterpretAsLeafPage(BPlusTreePage *page) -> BPlusTreeLeafPage<KeyType, RID, KeyComparator> *;

  auto CreateLeafPage() -> BPlusTreeLeafPage<KeyType, RID, KeyComparator> *;
  auto CreateInternalPage() -> BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *;

  void InsertInParent(BPlusTreePage *left_page, BPlusTreePage *right_page, const KeyType &upward_key);

  void RefreshAllParentPointer(BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *page);
  void RefreshParentPointer(BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *page, int index);

  void InitBPlusTree(const KeyType &key, const ValueType &value);

  void RemoveEntry(BPlusTreePage *base_page, const KeyType &key, int &dirty_height);
  auto RemoveDependOnType(BPlusTreePage *base_page, const KeyType &key) -> bool;

  auto TryRedistribute(BPlusTreePage *base_page, const KeyType &key) -> bool;
  void Redistribute(BPlusTreePage *base_page, BPlusTreePage *sibling_page,
                    BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent_page, int base_index,
                    bool sibling_on_left);
  auto TryMerge(BPlusTreePage *base_page, const KeyType &key, int &dirty_height) -> bool;
  void Merge(BPlusTreePage *base_page, BPlusTreePage *sibling_page,
             BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent_page, int base_index,
             bool sibling_on_left, int &dirty_height);

  auto IsSafePage(BPlusTreePage *page, BPlusTree::LatchMode mode) -> bool;
  void LatchRootPageId(Transaction *transaction, BPlusTree::LatchMode mode);
  void ReleaseAllLatches(Transaction *transaction, BPlusTree::LatchMode mode, int dirty_height = 0);
  /* Debug Routines for FREE!! */
  void ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const;

  void ToString(BPlusTreePage *page, BufferPoolManager *bpm) const;

  // member variable
  std::string index_name_;
  page_id_t root_page_id_;
  BufferPoolManager *buffer_pool_manager_;
  KeyComparator comparator_;
  int leaf_max_size_;
  int internal_max_size_;
  bool header_record_created_{false};
  ReaderWriterLatch root_id_rwlatch_;
};

}  // namespace bustub