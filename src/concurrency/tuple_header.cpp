#include "concurrency/tuple_header.h"
#include "common/types.h"
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <cstring>
#include <sys/types.h>

namespace minidb {

// ============================================================
// TODO: 你来实现 — 插入元组
// ============================================================
//
// 实现提示：
//   1. 获取用户数据区指针和 SlotDirectoryHeader
//   2. 计算新元组总大小：tuple_size = TUPLE_HEADER_SIZE + data_len
//   3. 计算 slot 目录占用空间：
//      slot_dir_end = SLOT_DIR_HEADER_SIZE + (slot_count + 1) * SLOT_ENTRY_SIZE
//   4. 检查空闲空间：free_space_offset - slot_dir_end >= tuple_size
//   5. 分配空间（从尾部向前）：
//      new_offset = free_space_offset - tuple_size
//   6. 构造 TupleHeader 并写入：
//      TupleHeader th;
//      th.xmin = xmin;
//      th.xmax = INVALID_TXN_ID;
//      th.data_len = data_len;
//      th.page_id = page->GetPageId();
//      th.slot_id = slot_count;
//      memcpy(user_data + new_offset, &th, TUPLE_HEADER_SIZE);
//   7. 写入用户数据：
//      memcpy(user_data + new_offset + TUPLE_HEADER_SIZE, data, data_len);
//   8. 添加 SlotEntry：
//      SlotEntry* slot = GetSlotEntry(user_data, slot_count);
//      slot->offset = new_offset;
//      slot->length = tuple_size;
//      slot->in_use = true;
//   9. 更新 header：
//      header->slot_count++;
//      header->free_space_offset = new_offset;
//  10. 返回 slot_id（即旧的 slot_count）

    // ============================================================
    // TODO: 你来实现 — 插入元组
    // ============================================================
    //
    // 实现步骤：
    //   1. 读取 SlotDirectoryHeader，获取 slot_count 和 free_space_offset
    //   2. 计算新元组的总大小：TUPLE_HEADER_SIZE + data_len
    //   3. 检查空闲空间是否足够：
    //      - 需要空间 = 新元组大小 + 一个 SlotEntry 的大小
    //      - 可用空间 = free_space_offset - (SLOT_DIR_HEADER_SIZE + slot_count * SLOT_ENTRY_SIZE)
    //   4. 在页面尾部分配空间：free_space_offset -= tuple_total_size
    //   5. 写入 TupleHeader（设置 xmin, flags 等）
    //   6. 写入用户数据
    //   7. 添加新的 SlotEntry
    //   8. 更新 SlotDirectoryHeader
    //   9. 返回 slot_id
    //
    // 参数：
    //   page     - 目标页面（调用者需持有写锁）
    //   xmin     - 创建此元组的事务 ID
    //   data     - 用户数据
    //   data_len - 用户数据长度
    //
    // 返回：
    //   成功返回 slot_id（>=0），空间不足返回 -1
    //
//
int TuplePage::InsertTuple(Page* page, txn_id_t xmin,
                           const char* data, uint16_t data_len) {
    if (page == nullptr || xmin == INVALID_TXN_ID || data == nullptr || data_len == 0) {
        throw std::invalid_argument("TuplePage::InsertTuple: page is null");
    }
    // 先加锁，避免 TOCTOU 竞态（空间检查和写入必须原子执行）
    std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());

    char* user_data = page->GetUserData();
    auto* header = reinterpret_cast<SlotDirectoryHeader*>(user_data);

    uint32_t tuple_size = TUPLE_HEADER_SIZE + data_len;
    uint32_t need_space = tuple_size + SLOT_ENTRY_SIZE;
    uint32_t slot_dir_end =
        SLOT_DIR_HEADER_SIZE + (header->slot_count + 1) * SLOT_ENTRY_SIZE;
    ssize_t available_space = header->free_space_offset > slot_dir_end ?
        header->free_space_offset - slot_dir_end : 0;
    if (need_space > available_space) {
        return -1;
    }

    header->free_space_offset -= tuple_size;
    TupleHeader th;
    th.xmin = xmin;
    th.xmax = INVALID_TXN_ID;
    th.data_len = data_len;
    th.page_id = page->GetPageId();
    th.slot_id = header->slot_count;
    memcpy(user_data + header->free_space_offset, &th, TUPLE_HEADER_SIZE);
    memcpy(user_data + header->free_space_offset + TUPLE_HEADER_SIZE, data, data_len);
    SlotEntry* slot = GetSlotEntry(user_data, header->slot_count);
    slot->offset = header->free_space_offset;
    slot->length = tuple_size;
    slot->in_use = true;
    uint32_t old_slot = header->slot_count++;
    return old_slot;
}

// ============================================================
// TODO: 你来实现 — 标记删除元组
// ============================================================
//
// 实现提示：
//   1. 获取用户数据区指针和 SlotDirectoryHeader
//   2. 检查 slot_id 是否有效（< slot_count）
//   3. 获取 SlotEntry，检查 in_use
//   4. 通过 SlotEntry.offset 定位 TupleHeader：
//      TupleHeader* th = reinterpret_cast<TupleHeader*>(user_data + slot->offset);
//   5. 设置 th->xmax = xmax
//   6. 设置 th->flags |= TUPLE_DELETED
//   7. 返回 true
//
// 注意：这里只是逻辑删除（设置 xmax），物理空间不回收。
//       物理删除由 VACUUM 完成（遍历页面，回收 xmax 已提交的元组空间）。
//
bool TuplePage::MarkDelete(Page* page, uint16_t slot_id, txn_id_t xmax) {
    // TODO: 你来实现
    if (page == nullptr) {
        throw std::invalid_argument("TuplePage::MarkDelete: page is null");
    }
    std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
    TupleHeader* th = const_cast<TupleHeader*>(GetTupleHeaderInternal(page, slot_id));
    if (th == nullptr) {
        return false;  // slot_id 无效或 slot 未使用
    }
    th->xmax = xmax;
    th->flags |= TUPLE_DELETED;
    return true;
}

// ============================================================
// TODO: 你来实现 — 读取元组头部
// ============================================================
//
// 实现提示：
//   1. 获取用户数据区指针和 SlotDirectoryHeader
//   2. 检查 slot_id 有效性
//   3. 获取 SlotEntry，检查 in_use
//   4. 从 SlotEntry.offset 处读取 TupleHeader
//   5. 返回 TupleHeader 的拷贝
//
std::optional<TupleHeader> TuplePage::GetTupleHeader(const Page* page, uint16_t slot_id) {
    if (page == nullptr) {
        throw std::invalid_argument("TuplePage::GetTupleHeader: page is null");
    }
    Page *mutable_page = const_cast<Page*>(page);
    std::lock_guard<std::shared_mutex> lock(mutable_page->GetPageLatch());
    return *GetTupleHeaderInternal(page, slot_id);
}

const TupleHeader* TuplePage::GetTupleHeaderInternal(const Page* page, uint16_t slot_id) {
    if (page == nullptr) {
        throw std::invalid_argument("TuplePage::GetTupleHeader: page is null");
    }
    const char* user_data = page->GetUserData();
    auto* header = reinterpret_cast<const SlotDirectoryHeader*>(user_data);
    if (slot_id >= header->slot_count) {
        return nullptr;
    }
    const SlotEntry* slot = GetSlotEntry(user_data, slot_id);
    if (!slot->in_use) {
        return nullptr;
    }
    const TupleHeader* th = reinterpret_cast<const TupleHeader*>(user_data + slot->offset);
    return th;
}

// ============================================================
// TODO: 你来实现 — 读取元组用户数据
// ============================================================
//
// 实现提示：
//   1. 获取用户数据区指针和 SlotDirectoryHeader
//   2. 检查 slot_id 有效性
//   3. 获取 SlotEntry
//   4. 计算用户数据起始位置：slot->offset + TUPLE_HEADER_SIZE
//   5. 从 TupleHeader 中读取 data_len
//   6. 拷贝 min(data_len, max_len) 字节到 out_data
//   7. 返回实际拷贝的字节数
//
uint16_t TuplePage::GetTupleData(const Page* page, uint16_t slot_id,
                                 char* out_data, uint16_t max_len) {
    
    if (page == nullptr) {
        throw std::invalid_argument("TuplePage::GetTupleData: page is null");
    }
    const char* user_data = page->GetUserData();
    Page *mutable_page = const_cast<Page*>(page);
    std::lock_guard<std::shared_mutex> lock(mutable_page->GetPageLatch());
    auto* header = reinterpret_cast<const SlotDirectoryHeader*>(user_data);
    if (slot_id >= header->slot_count) {
        return 0;
    }
    const SlotEntry* slot = GetSlotEntry(user_data, slot_id);
    if (!slot->in_use) {
        return 0;
    }
    const TupleHeader* th = reinterpret_cast<const TupleHeader*>(user_data + slot->offset);
    ssize_t start = slot->offset + TUPLE_HEADER_SIZE;
    uint16_t to_copy = std::min(th->data_len, max_len);
    memcpy(out_data, user_data + start, to_copy);
    return to_copy;
}

// ============================================================
// TODO: 你来实现 — 更新 Hint Bits
// ============================================================
//
// 实现提示：
//   1. 获取用户数据区指针和 SlotDirectoryHeader
//   2. 检查 slot_id 有效性
//   3. 获取 SlotEntry
//   4. 定位 TupleHeader：
//      TupleHeader* th = reinterpret_cast<TupleHeader*>(user_data + slot->offset);
//   5. 设置 hint bits：th->flags |= flags
//   6. 返回 true
//
// 【注意】
//   设置 hint bits 会弄脏页面，但这是"良性脏"。
//   调用者应该在设置后标记页面为脏（page->SetDirty(true)），
//   但即使丢失也不影响正确性（下次再查 CLOG 即可）。
//
bool TuplePage::UpdateHintBits(Page* page, uint16_t slot_id, uint16_t flags) {
    // TODO: 你来实现
    if (page == nullptr) {
        throw std::invalid_argument("TuplePage::UpdateHintBits: page is null");
    }
    std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
    const TupleHeader *th = GetTupleHeaderInternal(page, slot_id);
    if (th == nullptr) {
        return false;
    }
    TupleHeader* header = const_cast<TupleHeader*>(th);
    header->flags |= flags;
    return true;
}

} // namespace minidb
