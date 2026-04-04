#pragma once

#include "common/types.h"
#include "common/config.h"
#include "storage/page.h"
#include <cstring>
#include <vector>
#include <optional>

namespace minidb {

// ============================================================
// TupleHeader — 嵌入页面数据的元组头部
// ============================================================
//
// 【面试核心知识点 — 元组头部持久化】
//
// PostgreSQL 的 HeapTupleHeaderData 包含：
//   - t_xmin (4B): 创建此元组的事务 ID
//   - t_xmax (4B): 删除/更新此元组的事务 ID
//   - t_cid  (4B): 命令 ID（同一事务内的操作序号）
//   - t_ctid (6B): 当前元组的物理位置 (page, offset)
//   - t_infomask (2B): hint bits（缓存事务状态，避免频繁查 CLOG）
//   - t_infomask2 (2B): 属性数量 + 标志
//   - t_hoff (1B): 用户数据起始偏移
//
// 【关键设计】
//   TupleHeader 直接嵌入在页面的 data_[] 中，
//   随 ReadPage/WritePage 自动持久化到磁盘，
//   与 PageHeader 中的 LSN 持久化方案一致。
//
// 磁盘上的元组布局：
//   [TupleHeader (28B)] [用户数据 (变长)]
//

// Hint Bits — 缓存事务状态，避免频繁查询 CLOG
// 参考 PostgreSQL 的 t_infomask
enum TupleFlags : uint16_t {
    TUPLE_FLAG_NONE         = 0x0000,
    TUPLE_XMIN_COMMITTED    = 0x0100,  // xmin 事务已提交（hint bit）
    TUPLE_XMIN_ABORTED      = 0x0200,  // xmin 事务已回滚（hint bit）
    TUPLE_XMAX_COMMITTED    = 0x0400,  // xmax 事务已提交
    TUPLE_XMAX_ABORTED      = 0x0800,  // xmax 事务已回滚
    TUPLE_DELETED           = 0x1000,  // 元组已被标记删除
    TUPLE_UPDATED           = 0x2000,  // 元组已被更新（xmax 指向新版本）
};

struct TupleHeader {
    txn_id_t xmin{INVALID_TXN_ID};    // 8B — 创建此元组的事务 ID
    txn_id_t xmax{INVALID_TXN_ID};    // 8B — 删除/更新此元组的事务 ID（0=有效）
    uint16_t flags{TUPLE_FLAG_NONE};   // 2B — hint bits + 状态标志
    uint16_t data_len{0};              // 2B — 用户数据长度
    page_id_t page_id{INVALID_PAGE_ID}; // 4B — 所在页面 ID
    uint16_t slot_id{0};               // 2B — 在页面中的 slot 编号
    uint16_t reserved_{0};             // 2B — 保留对齐

    // ============================================================
    // Hint Bits 操作
    // ============================================================
    //
    // 【面试知识点 — Hint Bits 优化】
    //
    // 问题：每次可见性判断都要查 CLOG（磁盘 I/O）太慢
    // 解决：第一次查 CLOG 后，将结果缓存到元组头部的 flags 中
    //       后续判断直接读 flags，无需再查 CLOG
    //
    // PostgreSQL 的做法：
    //   if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED)) {
    //       if (TransactionIdDidCommit(xmin)) {
    //           tuple->t_infomask |= HEAP_XMIN_COMMITTED;  // 设置 hint bit
    //           SetBufferCommitInfoNeedsSave(buffer);       // 标记页面需要写回
    //       }
    //   }
    //
    // 注意：设置 hint bit 会弄脏页面，但这是"良性脏"，
    //       即使丢失也不影响正确性（下次再查 CLOG 即可）。
    //

    bool IsXminCommitted() const { return flags & TUPLE_XMIN_COMMITTED; }
    bool IsXminAborted() const { return flags & TUPLE_XMIN_ABORTED; }
    bool IsXmaxCommitted() const { return flags & TUPLE_XMAX_COMMITTED; }
    bool IsXmaxAborted() const { return flags & TUPLE_XMAX_ABORTED; }
    bool IsDeleted() const { return flags & TUPLE_DELETED; }
    bool IsUpdated() const { return flags & TUPLE_UPDATED; }

    void SetXminCommitted() { flags |= TUPLE_XMIN_COMMITTED; }
    void SetXminAborted() { flags |= TUPLE_XMIN_ABORTED; }
    void SetXmaxCommitted() { flags |= TUPLE_XMAX_COMMITTED; }
    void SetXmaxAborted() { flags |= TUPLE_XMAX_ABORTED; }
    void SetDeleted() { flags |= TUPLE_DELETED; }
    void SetUpdated() { flags |= TUPLE_UPDATED; }

    // 清除 hint bits（事务状态变更时需要）
    void ClearXminHints() { flags &= ~(TUPLE_XMIN_COMMITTED | TUPLE_XMIN_ABORTED); }
    void ClearXmaxHints() { flags &= ~(TUPLE_XMAX_COMMITTED | TUPLE_XMAX_ABORTED); }
};

// TupleHeader 大小（编译期检查）
static constexpr size_t TUPLE_HEADER_SIZE = sizeof(TupleHeader);

// ============================================================
// SlotEntry — 页面内的 Slot 目录项
// ============================================================
//
// 【面试知识点 — 页面内元组管理】
//
// PostgreSQL 使用 ItemIdData（Line Pointer）来管理页面内的元组：
//   struct ItemIdData {
//       unsigned lp_off:15;   // 元组在页面内的偏移
//       unsigned lp_flags:2;  // 状态（unused/normal/redirect/dead）
//       unsigned lp_len:15;   // 元组长度
//   };
//
// Slot 目录从页面头部向后增长，元组数据从页面尾部向前增长，
// 中间是空闲空间。这种设计允许元组在页面内移动（compaction）
// 而不影响外部引用（通过 slot_id 间接寻址）。
//
struct SlotEntry {
    uint16_t offset{0};    // 元组在页面用户数据区的偏移
    uint16_t length{0};    // 元组总长度（TupleHeader + 用户数据）
    bool     in_use{false}; // 是否在使用中
    uint8_t  padding_{0};  // 对齐填充
};

static constexpr size_t SLOT_ENTRY_SIZE = sizeof(SlotEntry);

// ============================================================
// SlotDirectory — 页面内的 Slot 目录管理
// ============================================================
//
// 页面用户数据区布局：
//
// +------------------------------------------+
// | SlotDirectory Header (4B)                |  ← slot_count + free_space_offset
// +------------------------------------------+
// | SlotEntry[0] | SlotEntry[1] | ...        |  ← Slot 目录（从前向后增长）
// +------------------------------------------+
// |           Free Space                      |
// +------------------------------------------+
// | ... | Tuple[1] | Tuple[0]                |  ← 元组数据（从后向前增长）
// +------------------------------------------+
//
struct SlotDirectoryHeader {
    uint16_t slot_count{0};           // 当前 slot 数量
    uint16_t free_space_offset{0};    // 空闲空间的起始偏移（相对于用户数据区）
};

static constexpr size_t SLOT_DIR_HEADER_SIZE = sizeof(SlotDirectoryHeader);

// ============================================================
// TuplePage — 页面级元组操作工具类
// ============================================================
//
// 提供在页面上操作元组的静态方法。
// 所有操作直接读写页面的 data_[]，修改会随页面刷盘自动持久化。
//
// 【设计原则】
//   这是一个无状态的工具类，不持有任何数据。
//   所有状态都在 Page 的 data_[] 中，通过指针操作。
//   调用者负责加锁（Page::WLock/RLock）。
//
class TuplePage {
public:
    // ============================================================
    // 初始化页面为元组页
    // ============================================================
    // 设置 SlotDirectoryHeader，将 free_space_offset 指向用户数据区末尾
    //
    static void InitPage(Page* page) {
        char* user_data = page->GetUserData();
        auto* header = reinterpret_cast<SlotDirectoryHeader*>(user_data);
        header->slot_count = 0;
        // 空闲空间从 slot 目录之后开始，元组从用户数据区末尾向前增长
        header->free_space_offset = static_cast<uint16_t>(PAGE_USER_DATA_SIZE);
    }

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
    static int InsertTuple(Page* page, txn_id_t xmin,
                           const char* data, uint16_t data_len);

    // ============================================================
    // TODO: 你来实现 — 标记删除元组（设置 xmax）
    // ============================================================
    //
    // 实现步骤：
    //   1. 通过 slot_id 找到 SlotEntry
    //   2. 通过 SlotEntry.offset 定位 TupleHeader
    //   3. 设置 TupleHeader.xmax = xmax
    //   4. 设置 TUPLE_DELETED 标志
    //
    // 注意：这里只是标记删除（设置 xmax），不是物理删除。
    //       物理删除由 VACUUM 完成。
    //
    static bool MarkDelete(Page* page, uint16_t slot_id, txn_id_t xmax);

    // ============================================================
    // TODO: 你来实现 — 读取元组头部
    // ============================================================
    //
    // 通过 slot_id 定位元组，返回 TupleHeader 的拷贝。
    // 用于可见性判断（读取 xmin/xmax/flags）。
    //
    static std::optional<TupleHeader> GetTupleHeader(const Page* page, uint16_t slot_id);
    static const TupleHeader* GetTupleHeaderInternal(const Page* page, uint16_t slot_id);

    // ============================================================
    // TODO: 你来实现 — 读取元组用户数据
    // ============================================================
    //
    // 通过 slot_id 定位元组，将用户数据拷贝到 out_data。
    // 返回实际数据长度，失败返回 0。
    //
    static uint16_t GetTupleData(const Page* page, uint16_t slot_id,
                                 char* out_data, uint16_t max_len);

    // ============================================================
    // TODO: 你来实现 — 更新元组的 Hint Bits
    // ============================================================
    //
    // 当可见性判断查询了 CLOG 后，将结果缓存到 TupleHeader.flags 中。
    // 这会弄脏页面，但是"良性脏"——丢失不影响正确性。
    //
    // 参数：
    //   page    - 目标页面（调用者需持有写锁）
    //   slot_id - 元组的 slot 编号
    //   flags   - 要设置的 hint bits（OR 操作）
    //
    static bool UpdateHintBits(Page* page, uint16_t slot_id, uint16_t flags);

    // ============================================================
    // 获取页面中的元组数量
    // ============================================================
    static uint16_t GetTupleCount(const Page* page) {
        const char* user_data = page->GetUserData();
        const auto* header = reinterpret_cast<const SlotDirectoryHeader*>(user_data);
        return header->slot_count;
    }

    // ============================================================
    // 获取页面剩余空闲空间
    // ============================================================
    static size_t GetFreeSpace(const Page* page) {
        const char* user_data = page->GetUserData();
        const auto* header = reinterpret_cast<const SlotDirectoryHeader*>(user_data);
        size_t slot_dir_end = SLOT_DIR_HEADER_SIZE + header->slot_count * SLOT_ENTRY_SIZE;
        if (header->free_space_offset <= slot_dir_end) {
            return 0;
        }
        return header->free_space_offset - slot_dir_end;
    }

private:
    // 获取指定 slot 的 SlotEntry 指针
    static SlotEntry* GetSlotEntry(char* user_data, uint16_t slot_id) {
        return reinterpret_cast<SlotEntry*>(
            user_data + SLOT_DIR_HEADER_SIZE + slot_id * SLOT_ENTRY_SIZE);
    }

    static const SlotEntry* GetSlotEntry(const char* user_data, uint16_t slot_id) {
        return reinterpret_cast<const SlotEntry*>(
            user_data + SLOT_DIR_HEADER_SIZE + slot_id * SLOT_ENTRY_SIZE);
    }
};

} // namespace minidb
