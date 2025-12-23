#include <string>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "storage/index/b_plus_tree.h"
#include "storage/page/header_page.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                          int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      root_page_id_(INVALID_PAGE_ID),
      buffer_pool_manager_(buffer_pool_manager),
      comparator_(comparator),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return root_page_id_ == INVALID_PAGE_ID; }
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction) -> bool {
  bool dummy_used = false;
  if (transaction == nullptr) {
    transaction = new Transaction(0);
    dummy_used = true;
  }
  LatchRootPageId(transaction, LatchMode::READ);
  if (IsEmpty()) {
    ReleaseAllLatches(transaction, LatchMode::READ);
    if (dummy_used) {
      delete transaction;
    }
    return false;
  }
  auto [raw_leaf_page, leaf_page] = FindLeafPage(key, transaction, LatchMode::READ);
  // binary search
  auto found = false;
  int low = 0, high = leaf_page->GetSize() - 1;
  while(low < high) {
    int mid = (low + high) / 2;
    auto com_res = comparator_(leaf_page->KeyAt(mid), key);
    if(com_res >= 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  if(comparator_(leaf_page->KeyAt(low), key) == 0) {
    found = true;
    result->push_back(leaf_page->ValueAt(low));
  }
  // clear up all the latches held in this transaction
  ReleaseAllLatches(transaction, LatchMode::READ);
  if (dummy_used) {
    delete transaction;
  }
  return found;
}

INDEX_TEMPLATE_ARGUMENTS auto BPLUSTREE_TYPE::FetchBPlusTreePage(page_id_t page_id)
    -> std::pair<Page *, BPlusTreePage *> {
  Page *page = buffer_pool_manager_->FetchPage(page_id);
  BUSTUB_ASSERT(page != nullptr, "FetchBPlusTreePage(): page != nullptr");
  return {page, reinterpret_cast<BPlusTreePage *>(page->GetData())};
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key, Transaction *transaction, LatchMode mode)
    -> std::pair<Page *, BPlusTreeLeafPage<KeyType, RID, KeyComparator> *> {
  BUSTUB_ASSERT(root_page_id_ != INVALID_PAGE_ID, "root_page_id_ != INVALID_PAGE_ID");
  if (transaction != nullptr) {
    // root id latch should already be held in this transaction
    BUSTUB_ASSERT(transaction->GetPageSet()->front() == nullptr, "transaction->GetPageSet()->front() == nullptr");
  }
  auto [raw_curr_page, curr_page] = FetchBPlusTreePage(root_page_id_);
  decltype(raw_curr_page) raw_next_page = nullptr;
  decltype(curr_page) next_page = nullptr;

  // concurrency support
  if (transaction != nullptr) {
    if (mode == LatchMode::READ) {
      raw_curr_page->RLatch();
    } else if (mode == LatchMode::OPTIMIZE) {
      if (curr_page->IsLeafPage()) {
        raw_curr_page->WLatch();
      } else {
        raw_curr_page->RLatch();
      }
    } else {
      raw_curr_page->WLatch();
    }
    if (IsSafePage(curr_page, mode)) {
      ReleaseAllLatches(transaction, mode);
    }
    transaction->AddIntoPageSet(raw_curr_page);
  }
  while (!curr_page->IsLeafPage()) {
    auto curr_internal_page = ReinterpretAsInternalPage(curr_page);
    page_id_t jump_pid = curr_internal_page->SearchPage(key, comparator_);
    BUSTUB_ASSERT(jump_pid != INVALID_PAGE_ID, "jump_pid != INVALID_PAGE_ID");
    auto next_pair = FetchBPlusTreePage(jump_pid);
    raw_next_page = next_pair.first;
    next_page = next_pair.second;
    // 从上到下依次上锁
    if (transaction != nullptr) {
      if (mode == LatchMode::READ) {
        raw_next_page->RLatch();
      } else if (mode == LatchMode::OPTIMIZE) {
        if (next_page->IsLeafPage()) {
          raw_next_page->WLatch();
        } else {
          raw_next_page->RLatch();
        }
      } else {
        raw_next_page->WLatch();
      }
      if (IsSafePage(next_page, mode)) {
        ReleaseAllLatches(transaction, mode);
      }
      transaction->AddIntoPageSet(raw_next_page);
    } else {
      buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), false);
    }
    raw_curr_page = raw_next_page;
    curr_page = next_page;
  }
  BUSTUB_ASSERT(curr_page->IsLeafPage(), "curr_page->IsLeafPage()");
  return {raw_curr_page, ReinterpretAsLeafPage(curr_page)};
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ReinterpretAsInternalPage(BPlusTreePage *page) -> InternalPage * {
  return reinterpret_cast<InternalPage *>(page);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ReinterpretAsLeafPage(BPlusTreePage *page) -> BPlusTreeLeafPage<KeyType, RID, KeyComparator> * {
  return reinterpret_cast<BPlusTreeLeafPage<KeyType, RID, KeyComparator> *>(page);
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 * @param:transaction,如何记录哪些 page 当前持有锁？这里就要用到在 Checkpoint1 里一直没有提到的一个参数，transaction。
transaction 就是 Bustub 里的事务。在 Project2 中，可以暂时不用理解事务是什么，而是将其看作当前在对 B+ 树进行操作的线程。
 */

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  bool dummy_used = false;
  if (transaction == nullptr) {
    transaction = new Transaction(0);
    dummy_used = true;
  }
  bool success = InsertHelper(key, value, transaction, LatchMode::OPTIMIZE);
  if (dummy_used) {
    delete transaction;
  }
  return success;
}

/*
 * Helper for insert
 * transaction must be not null
 * when OPTIMIZE mode fails, re-call with INSERT mode
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertHelper(const KeyType &key, const ValueType &value, Transaction *transaction, LatchMode mode)
    -> bool {
  int dirty_height = 0;
  // optimize mode first lock rootpage
  LatchRootPageId(transaction, mode);
  if (IsEmpty()) {
    if (mode == LatchMode::OPTIMIZE) {
      // optimize mode fail,because the tree is empty
      // release all latch before
      ReleaseAllLatches(transaction, mode, dirty_height);  // no page is dirty
      return InsertHelper(key, value, transaction, LatchMode::INSERT);
    }
    // b+ tree is empty & mode is not optimize
    InitBPlusTree(key, value);
    ReleaseAllLatches(transaction, mode, dirty_height);  // no page is dirty
    return true;
  }
  auto [raw_leaf_page, leaf_page] = FindLeafPage(key, transaction, mode);
  if (leaf_page->GetSize() + 1 == leaf_page->GetMaxSize() && mode == LatchMode::OPTIMIZE) {
    // optimize mode fail
    ReleaseAllLatches(transaction, mode, dirty_height);
    // 重新上write latch
    return InsertHelper(key, value, transaction, LatchMode::INSERT);
  }
  bool no_duplicate = leaf_page->Insert(key, value, comparator_);
  if (!no_duplicate) {
    // insert fail. release all latch before
    ReleaseAllLatches(transaction, mode, dirty_height);
    return false;
  }
  dirty_height += 1;
  // after insert a kv ,need split
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    auto leaf_page_prime = CreateLeafPage();
    leaf_page->MoveLatterHalfTo(leaf_page_prime);
    leaf_page_prime->SetParentPageId(leaf_page->GetParentPageId());
    const auto key_upward = leaf_page_prime->KeyAt(0);
    InsertInParent(leaf_page, leaf_page_prime, key_upward);
    buffer_pool_manager_->UnpinPage(leaf_page_prime->GetPageId(), true);
  }
  ReleaseAllLatches(transaction, mode, dirty_height);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InitBPlusTree(const KeyType &key, const ValueType &value) {
  auto root_leaf_page = CreateLeafPage();
  root_page_id_ = root_leaf_page->GetPageId();
  BUSTUB_ASSERT(root_leaf_page != nullptr, "root_leaf_page != nullptr");
  BUSTUB_ASSERT(root_page_id_ != INVALID_PAGE_ID, "root_page_id_ != INVALID_PAGE_ID");
  UpdateRootPageId(!header_record_created_);
  header_record_created_ = true;
  auto ret = root_leaf_page->Insert(key, value, comparator_);
  BUSTUB_ASSERT(ret, "BPlusTree Init Insert should be True");
  buffer_pool_manager_->UnpinPage(root_page_id_, true);  // modification made
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::CreateLeafPage() -> BPlusTreeLeafPage<KeyType, RID, KeyComparator> * {
  page_id_t pid = INVALID_PAGE_ID;
  auto new_page = buffer_pool_manager_->NewPage(&pid);
  BUSTUB_ASSERT(pid != INVALID_PAGE_ID, "pid != INVALID_PAGE_ID");
  auto leaf_page = reinterpret_cast<BPlusTreeLeafPage<KeyType, RID, KeyComparator> *>(new_page->GetData());
  leaf_page->Init(pid, INVALID_PAGE_ID, leaf_max_size_);
  return leaf_page;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::CreateInternalPage() -> BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> * {
  page_id_t pid = INVALID_PAGE_ID;
  auto new_page = buffer_pool_manager_->NewPage(&pid);
  BUSTUB_ASSERT(pid != INVALID_PAGE_ID, "pid != INVALID_PAGE_ID");
  auto internal_page =
      reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(new_page->GetData());
  internal_page->Init(pid, INVALID_PAGE_ID, internal_max_size_);
  return internal_page;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertInParent(BPlusTreePage *left_page, BPlusTreePage *right_page, const KeyType &upward_key) {
  // left是root,最初只有root节点的时候。分一半到right后，应该新建一个new root. [key0,left page] [upward_key,right page]
  if (left_page->IsRootPage()) {
    // std::cout << "root page split" << std::endl;
    auto new_root_page = CreateInternalPage();
    root_page_id_ = new_root_page->GetPageId();
    UpdateRootPageId(false);
    new_root_page->SetValueAt(0, left_page->GetPageId());
    new_root_page->SetKeyAt(1, upward_key);
    new_root_page->SetValueAt(1, right_page->GetPageId());
    new_root_page->IncreaseSize(1);
    BUSTUB_ASSERT(new_root_page->GetSize() == 2, "new_root_page->GetSize() == 2");
    left_page->SetParentPageId(root_page_id_);
    right_page->SetParentPageId(root_page_id_);
    buffer_pool_manager_->UnpinPage(root_page_id_, true);
    return;
  }
  // 不是rootpage的情况
  // std::cout << "not root page split" << std::endl;
  auto [raw_parent_page, base_parent_page] = FetchBPlusTreePage(left_page->GetParentPageId());
  auto parent_page = ReinterpretAsInternalPage(base_parent_page);
  if (parent_page->GetSize() == parent_page->GetMaxSize()) {
    // follow rule that split internal node when number of values reaches max_size before insertion
    // insert之前检查，split
    auto parent_page_prime = CreateInternalPage();
    // 设置为相同的父节点id
    parent_page_prime->SetParentPageId(parent_page->GetParentPageId());
    parent_page->MoveLatterHalfWithOneExtraTo(parent_page_prime, upward_key, right_page->GetPageId(), comparator_);
    // 更新新节点中子节点的父节点指针为这个新节点
    RefreshAllParentPointer(parent_page_prime);
    const auto futher_upward_key = parent_page_prime->KeyAt(
        0);  // actually invalid
             // 0-indexed，这个key是要存放在上一层中，所以在移动时是直接把后面一半key移动到新page从0开始的位置
    // 递归上一层，将parent_page_prime的key插入到其父亲节点中
    InsertInParent(parent_page, parent_page_prime, futher_upward_key);
    buffer_pool_manager_->UnpinPage(parent_page_prime->GetPageId(), true);
  } else {
    parent_page->Insert(upward_key, right_page->GetPageId(), comparator_);
  }
  buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RefreshParentPointer(BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *page, int index) {
  auto page_id = page->GetPageId();
  auto [raw_page, moved_page] = FetchBPlusTreePage(page->ValueAt(index));
  moved_page->SetParentPageId(page_id);
  buffer_pool_manager_->UnpinPage(moved_page->GetPageId(), true);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RefreshAllParentPointer(BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *page) {
  auto page_id = page->GetPageId();
  for (auto i = 0; i < page->GetSize(); i++) {
    auto [raw_page, moved_page] = FetchBPlusTreePage(page->ValueAt(i));
    moved_page->SetParentPageId(page_id);
    buffer_pool_manager_->UnpinPage(moved_page->GetPageId(), true);
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immdiately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  bool dummy_used = false;
  if (transaction == nullptr) {
    transaction = new Transaction(0);
    dummy_used = true;
  }
  RemoveHelper(key, transaction, LatchMode::OPTIMIZE);
  if (dummy_used) {
    delete transaction;
  }
}

/*
 * Helper for Remove
 * Start with OPTIMIZE mode
 * if fails, retry with DELETE mode
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveHelper(const KeyType &key, Transaction *transaction, LatchMode mode) {
  int dirty_height = 0;
  LatchRootPageId(transaction, mode);
  if (IsEmpty()) {
    ReleaseAllLatches(transaction, mode, dirty_height);
    return;
  }

  auto [raw_leaf_page, leaf_page] = FindLeafPage(key, transaction, mode);
  if ((leaf_page->GetSize() - 1) < leaf_page->GetMinSize() && mode == LatchMode::OPTIMIZE) {
    // OPTIMIZE mode fail, page shoule be held in wlatch before leaf_page
    auto is_root = leaf_page->IsRootPage();
    auto is_leaf = leaf_page->IsLeafPage();
    auto is_internal = leaf_page->IsInternalPage();
    // root page should be treatment specail
    auto fail_condition1 = !is_root;
    auto fail_condition2 = is_root && is_leaf && (leaf_page->GetSize() - 1) == 0;
    auto fail_condition3 = is_root && is_internal && (leaf_page->GetSize() - 1) == 1;
    if (fail_condition1 || fail_condition2 || fail_condition3) {
      ReleaseAllLatches(transaction, mode);
      // 释放所有锁后，从上到下依次重新上写锁
      return RemoveHelper(key, transaction, LatchMode::DELETE);
    }
  }
  RemoveEntry(leaf_page, key, dirty_height);
  ReleaseAllLatches(transaction, mode, dirty_height);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveEntry(BPlusTreePage *base_page, const KeyType &key, int &dirty_height) {
  auto delete_success = RemoveDependOnType(base_page, key);
  if (!delete_success) {
    // not modification made on this page
    return;
  }
  dirty_height++;
  // 删除一个key后检查是否小于最小key的个数，如果是，先尝试在左右sibling偷一个，再尝试与左右sibling合并
  if (base_page->GetSize() < base_page->GetMinSize()) {
    // root page get special treatment
    // root's page being leaf page can violate the "half-full" property
    if (base_page->IsRootPage()) {
      if (base_page->IsInternalPage()) {
        if (base_page->GetSize() == 1) {
          // 只剩下一个，将其转化为root page 是 leaf page的情况
          root_page_id_ = ReinterpretAsInternalPage(base_page)->ValueAt(0);
          UpdateRootPageId(false);
          auto [raw_new_root_page, new_root_page] = FetchBPlusTreePage(root_page_id_);
          new_root_page->SetParentPageId(INVALID_PAGE_ID);
          buffer_pool_manager_->UnpinPage(root_page_id_, true);
        }
      } else {
        // root's page is leaf page, only if everything is deleted b+tree become empty
        if (base_page->GetSize() == 0) {
          root_page_id_ = INVALID_PAGE_ID;
          UpdateRootPageId(false);
        }
      }
    } else {
      // not root page
      // follow the order of
      // 1. redistribute from right
      // 2. redistribute from left
      // 3. merge from right
      // 4. merge from left
      auto redistribute_success = TryRedistribute(base_page, key);  // try steal a kv form right and then left
      if (!redistribute_success) {
        auto merge_success = TryMerge(base_page, key, dirty_height);  // must success
        BUSTUB_ASSERT(redistribute_success || merge_success, "redistribute_success || merge_success");
      }
    }
  }
}

/**
 * Try to redistribute from right, and then left
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryRedistribute(BPlusTreePage *base_page, const KeyType &key) -> bool {
  BUSTUB_ASSERT(!base_page->IsRootPage(), "!base_page->IsRootPage()");
  auto parent_page_id = base_page->GetParentPageId();
  auto [raw_parent_page, base_parent_page] = FetchBPlusTreePage(parent_page_id);
  auto parent_page = ReinterpretAsInternalPage(base_parent_page);
  auto underfull_idx = parent_page->SearchJumpIdx(key, comparator_);
  auto is_redistribute = false;
  if (underfull_idx < parent_page->GetSize() - 1) {
    // have right sibling
    auto [raw_sibling_page, sibling_page] = FetchBPlusTreePage(parent_page->ValueAt(underfull_idx + 1));
    raw_sibling_page->WLatch();  // lock sibling
    if ((sibling_page->GetSize() - 1) >= sibling_page->GetMinSize()) {
      // steal not lead to sibling underfull
      Redistribute(base_page, sibling_page, parent_page, underfull_idx, false);
      is_redistribute = true;
    }
    raw_sibling_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(sibling_page->GetPageId(), is_redistribute);
  }
  if (!is_redistribute && underfull_idx > 0) {
    // have left sibling
    auto [raw_sibling_page, sibling_page] = FetchBPlusTreePage(parent_page->ValueAt(underfull_idx - 1));
    raw_sibling_page->WLatch();  // lock sibling
    if ((sibling_page->GetSize() - 1) >= sibling_page->GetMinSize()) {
      // steal not lead to sibling underfull
      Redistribute(base_page, sibling_page, parent_page, underfull_idx, true);
      is_redistribute = true;
    }
    raw_sibling_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(sibling_page->GetPageId(), is_redistribute);
  }
  buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), is_redistribute);
  return is_redistribute;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Redistribute(BPlusTreePage *base_page, BPlusTreePage *sibling_page,
                                  BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent_page, int base_index,
                                  bool sibling_on_left) {
  if (base_page->IsLeafPage()) {
    auto base_leaf = ReinterpretAsLeafPage(base_page);
    auto sibling_leaf = ReinterpretAsLeafPage(sibling_page);
    if (sibling_on_left) {
      // sibling_leaf -> base
      sibling_leaf->MoveLastToFrontOf(base_leaf);
      parent_page->SetKeyAt(base_index, base_leaf->KeyAt(0));
    } else {
      // base <- sibling_leaf
      sibling_leaf->MoveFirstToEndOf(base_leaf);
      parent_page->SetKeyAt(base_index + 1, sibling_leaf->KeyAt(0));
    }
    
  } else {
    auto base_internal = ReinterpretAsInternalPage(base_page);
    auto sibling_internal = ReinterpretAsInternalPage(sibling_page);
    if (sibling_on_left) {
      // sibling_internal -> base
      sibling_internal->MoveLastToFrontOf(base_internal);
      RefreshParentPointer(base_internal, 0);
      parent_page->SetKeyAt(base_index, base_internal->KeyAt(0));
    } else {
      // base <- sibling_internal
      sibling_internal->MoveFirstToEndOf(base_internal);
      RefreshParentPointer(base_internal, base_internal->GetSize() - 1);
      // 还需要修改偷取过来的key为父节点中的key
      base_internal->SetKeyAt(base_internal->GetSize() - 1, parent_page->KeyAt(base_index + 1));
      parent_page->SetKeyAt(base_index + 1, sibling_internal->KeyAt(0));
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryMerge(BPlusTreePage *base_page, const KeyType &key, int &dirty_height) -> bool {
  BUSTUB_ASSERT(!base_page->IsRootPage(), "!base_page->IsRootPage()");
  auto parent_page_id = base_page->GetParentPageId();
  auto [raw_parent_page, base_parent_page] = FetchBPlusTreePage(parent_page_id);
  auto parent_page = ReinterpretAsInternalPage(base_parent_page);
  // 找到不满足minsize节点所在parent节点中位置
  auto underfull_idx = parent_page->SearchJumpIdx(key, comparator_);
  auto is_merge = false;
  if (underfull_idx < parent_page->GetSize() - 1) {
    // have right sibling
    auto [raw_sibling_page, sibling_page] = FetchBPlusTreePage(parent_page->ValueAt(underfull_idx + 1));
    raw_sibling_page->WLatch();
    Merge(base_page, sibling_page, parent_page, underfull_idx, false, dirty_height);
    raw_sibling_page->WUnlatch();
    is_merge = true;
    buffer_pool_manager_->UnpinPage(sibling_page->GetPageId(), is_merge);
  }
  if (!is_merge && underfull_idx > 0) {
    // have left sibling
    auto [raw_sibling_page, sibling_page] = FetchBPlusTreePage(parent_page->ValueAt(underfull_idx - 1));
    raw_sibling_page->WLatch();
    Merge(base_page, sibling_page, parent_page, underfull_idx, true, dirty_height);
    raw_sibling_page->WUnlatch();
    is_merge = true;
    buffer_pool_manager_->UnpinPage(sibling_page->GetPageId(), is_merge);
  }
  buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), is_merge);
  return is_merge;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Merge(BPlusTreePage *base_page, BPlusTreePage *sibling_page,
                           BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent_page, int base_index,
                           bool sibling_on_left, int &dirty_height) {
  if (base_page->IsLeafPage()) {
    auto base_leaf = ReinterpretAsLeafPage(base_page);
    auto sibling_leaf = ReinterpretAsLeafPage(sibling_page);
    if (sibling_on_left) {
      // <----.merge base to left sibling_leaf
      auto key_in_between = parent_page->KeyAt(base_index);
      base_leaf->MoveAllTo(sibling_leaf);
      // set next_page_id
      sibling_leaf->SetNextPageId(base_leaf->GetNextPageId());
      // mark off the link
      base_leaf->SetParentPageId(INVALID_PAGE_ID);
      // 将不满的节点合并到了左边兄弟节点后，需要在父节点中删除不满节点的key，递归的一个过程
      RemoveEntry(parent_page, key_in_between, dirty_height);
    } else {
      // <----.merge right sibling_leaf to base
      auto key_in_between = parent_page->KeyAt(base_index + 1);
      sibling_leaf->MoveAllTo(base_leaf);
      // set next_page_id
      base_leaf->SetNextPageId(sibling_leaf->GetNextPageId());
      // mark off the link
      sibling_leaf->SetParentPageId(INVALID_PAGE_ID);
      //
      RemoveEntry(parent_page, key_in_between, dirty_height);
    }
  } else {
    auto base_internal = ReinterpretAsInternalPage(base_page);
    auto sibling_internal = ReinterpretAsInternalPage(sibling_page);
    if (sibling_on_left) {
      // <----. merge base to left sibling interal
      auto key_in_between = parent_page->KeyAt(base_index);
      auto sibling_old_size = sibling_internal->GetSize();
      base_internal->MoveAllTo(sibling_internal);
      // set parent id
      RefreshAllParentPointer(sibling_internal);
      sibling_internal->SetKeyAt(sibling_old_size, key_in_between);
      // mark off the link
      base_internal->SetParentPageId(INVALID_PAGE_ID);
      RemoveEntry(parent_page, key_in_between, dirty_height);
    } else {
      // <----. merge  right sibling interal to base
      auto key_in_between = parent_page->KeyAt(base_index + 1);
      auto base_old_size = base_internal->GetSize();
      sibling_internal->MoveAllTo(base_internal);
      // set parent id
      RefreshAllParentPointer(base_internal);
      base_internal->SetKeyAt(base_old_size, key_in_between);
      // mark off the link
      sibling_internal->SetParentPageId(INVALID_PAGE_ID);
      RemoveEntry(parent_page, key_in_between, dirty_height);
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RemoveDependOnType(BPlusTreePage *base_page, const KeyType &key) -> bool {
  auto is_leaf = base_page->IsLeafPage();
  if (is_leaf) {
    return ReinterpretAsLeafPage(base_page)->RemoveKey(key, comparator_);
  }
  return ReinterpretAsInternalPage(base_page)->RemoveKey(key, comparator_);
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leaftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  if (IsEmpty()) {
    return End();
  }
  auto [raw_leaf_page, leaf_page] = FindLeafPage(KeyType());
  BUSTUB_ASSERT(leaf_page != nullptr, "leaf_page != nullptr");
  return INDEXITERATOR_TYPE(leaf_page->GetPageId(), 0, leaf_page, buffer_pool_manager_);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  auto [raw_leaf_page, leaf_page] = FindLeafPage(key);
  auto bigger_or_equal_idx = leaf_page->FindGreaterEqualKeyPosition(key, comparator_);
  if (bigger_or_equal_idx == -1) {
    // no bigger in this leaf,need to move to next one
    auto leaf_id = leaf_page->GetPageId();
    auto next_page_id = leaf_page->GetNextPageId();
    buffer_pool_manager_->UnpinPage(leaf_id, false);
    if (next_page_id == INVALID_PAGE_ID) {
      return End();
    }
    auto base_leaf_page = FetchBPlusTreePage(next_page_id).second;
    leaf_page = ReinterpretAsLeafPage(base_leaf_page);
    bigger_or_equal_idx = 0;
  }
  return INDEXITERATOR_TYPE(leaf_page->GetPageId(), bigger_or_equal_idx, leaf_page, buffer_pool_manager_);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

// 安全指当前 page 在当前操作下一定不会发生 split/steal/merge,当前操作仅可能改变此 page 及其 child page
// 的值，因此可以提前释放掉其祖先的锁来提高并发性能
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsSafePage(BPlusTreePage *page, BPlusTree::LatchMode mode) -> bool {
  if (mode == LatchMode::READ || mode == LatchMode::OPTIMIZE) {
    return true;
  }
  if (mode == LatchMode::INSERT) {
    return static_cast<bool>(page->GetSize() + 1 < page->GetMaxSize());
  }
  if (mode == LatchMode::DELETE) {
    auto after_delete_size = page->GetSize() - 1;
    if (after_delete_size < page->GetMinSize()) {
      // 根节点需要特殊处理
      if (page->IsRootPage()) {
        // key must >= 2
        if (page->IsInternalPage() && after_delete_size > 1) {
          return true;
        }
        // key must >= 1
        if (page->IsLeafPage() && after_delete_size > 0) {
          return true;
        }
      }
      return false;
    }
    return true;
  }
  BUSTUB_ASSERT(false, "Not supposed to hit this default return branch in IsSafePage()");
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::LatchRootPageId(Transaction *transaction, BPlusTree::LatchMode mode) {
  if (mode == LatchMode::READ || mode == LatchMode::OPTIMIZE) {
    root_id_rwlatch_.RLock();
  } else {
    root_id_rwlatch_.WLock();
  }
  // nullptr as indicator for page id latch
  transaction->AddIntoPageSet(nullptr);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ReleaseAllLatches(Transaction *transaction, BPlusTree::LatchMode mode, int dirty_height) {
  auto page_set = transaction->GetPageSet();
  while (!page_set->empty()) {
    // 从上到下释放锁
    auto front_page = page_set->front();
    // front_page == nullptr 为rootpage
    auto is_leaf =
        (front_page != nullptr) ? reinterpret_cast<BPlusTreePage *>(front_page->GetData())->IsLeafPage() : false;
    if (mode == LatchMode::READ) {
      if (front_page == nullptr) {
        root_id_rwlatch_.RUnlock();
      } else {
        front_page->RUnlatch();
      }
    } else if (mode == LatchMode::OPTIMIZE) {
      if (front_page == nullptr) {
        root_id_rwlatch_.RUnlock();
      } else {
        // in optimize mode, the last leaf page is held a Wlatch,else held RLatch
        if (is_leaf) {
          front_page->WUnlatch();
        } else {
          front_page->RUnlatch();
        }
      }
    } else {
      // insert or delete mode , normal write latch
      if (front_page == nullptr) {
        root_id_rwlatch_.WUnlock();
      } else {
        front_page->WUnlatch();
      }
    }
    if (front_page != nullptr) {
      // the last 'dirty_height' pages should be marked as dirty
      buffer_pool_manager_->UnpinPage(front_page->GetPageId(), page_set->size() <= static_cast<size_t>(dirty_height));
    }
    page_set->pop_front();
  }
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t { return root_page_id_; }

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Update/Insert root page id in header page(where page_id = 0, header_page is
 * defined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      defualt value is false. When set to true,
 * insert a record <index_name, root_page_id> into header page instead of
 * updating it.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  auto *header_page = static_cast<HeaderPage *>(buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record != 0) {
    // create a new record<index_name + root_page_id> in header_page
    header_page->InsertRecord(index_name_, root_page_id_);
  } else {
    // update root_page_id in header_page
    header_page->UpdateRecord(index_name_, root_page_id_);
  }
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;

    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, transaction);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, transaction);
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager *bpm, const std::string &outf) {
  if (IsEmpty()) {
    LOG_WARN("Draw an empty tree");
    return;
  }
  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  ToGraph(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm, out);
  out << "}" << std::endl;
  out.flush();
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager *bpm) {
  if (IsEmpty()) {
    LOG_WARN("Print an empty tree");
    return;
  }
  ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm);
}

/**
 * This method is used for debug only, You don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 * @param out
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    // Print node name
    out << leaf_prefix << leaf->GetPageId();
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << leaf->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      out << "<TD>" << leaf->KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << leaf->GetPageId() << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << leaf->GetPageId() << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }

    // Print parent links if there is a parent
    if (leaf->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << leaf->GetParentPageId() << ":p" << leaf->GetPageId() << " -> " << leaf_prefix
          << leaf->GetPageId() << ";\n";
    }
  } else {
    auto *inner = reinterpret_cast<InternalPage *>(page);
    // Print node name
    out << internal_prefix << inner->GetPageId();
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">P=" << inner->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        out << inner->KeyAt(i);
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Parent link
    if (inner->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << inner->GetParentPageId() << ":p" << inner->GetPageId() << " -> " << internal_prefix
          << inner->GetPageId() << ";\n";
    }
    // Print leaves
    for (int i = 0; i < inner->GetSize(); i++) {
      auto child_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i))->GetData());
      ToGraph(child_page, bpm, out);
      if (i > 0) {
        auto sibling_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i - 1))->GetData());
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_page->GetPageId() << " " << internal_prefix
              << child_page->GetPageId() << "};\n";
        }
        bpm->UnpinPage(sibling_page->GetPageId(), false);
      }
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

/**
 * This function is for debug only, you don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToString(BPlusTreePage *page, BufferPoolManager *bpm) const {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    std::cout << "Leaf Page: " << leaf->GetPageId() << " parent: " << leaf->GetParentPageId()
              << " next: " << leaf->GetNextPageId() << std::endl;
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  } else {
    auto *internal = reinterpret_cast<InternalPage *>(page);
    std::cout << "Internal Page: " << internal->GetPageId() << " parent: " << internal->GetParentPageId() << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(internal->ValueAt(i))->GetData()), bpm);
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub