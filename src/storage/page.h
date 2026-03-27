#pragma once

#include "common/types.h"
#include "common/config.h"
#include <cstring>
#include <atomic>

namespace minidb {

// ============================================================
// Page - 数据库页面的内存表示
// ============================================================
//
// 【面试核心知识点】
// 数据库以"页（Page）"为单位管理磁盘上的数据。
// PostgreSQL 中对应的是 BufferPage / PageHeaderData 结构。
// 一个页面的经典布局：
//
// +-------------------+
// | Page Header       |  ← 包含 LSN、校验和等元数据
// +-------------------+
// | Line Pointers     |  ← 指向页面内各元组的偏移（Item Pointer Array）
// | (从前往后增长)     |
// +-------------------+
// | Free Space        |  ← 空闲空间
// +-------------------+
// | Tuples/Data       |  ← 实际数据（从后往前增长）
// | (从后往前增长)     |
// +-------------------+
// | Special Space     |  ← B+Tree 等索引的特殊数据
// +-------------------+
//
class Page {
public:
    Page() { Reset(); }

    // 获取页面原始数据指针
    inline char* GetData() { return data_; }
    inline const char* GetData() const { return data_; }

    // 页面ID
    inline page_id_t GetPageId() const { return page_id_; }
    inline void SetPageId(page_id_t page_id) { page_id_ = page_id; }

    // LSN —— 这个页面最后一次被修改时的WAL日志位置
    // 恢复时：if (page_lsn < wal_record_lsn) → 需要redo
    inline lsn_t GetLSN() const { return lsn_; }
    inline void SetLSN(lsn_t lsn) { lsn_ = lsn; }

    // Pin计数：表示有多少线程正在使用这个页面
    // Pin > 0 时不能被 Buffer Pool 淘汰
    inline int GetPinCount() const { return pin_count_.load(); }
    inline void IncrementPinCount() { pin_count_.fetch_add(1); }
    inline void DecrementPinCount() { pin_count_.fetch_sub(1); }

    // 脏页标记：页面被修改后标记为脏，需要刷回磁盘
    inline bool IsDirty() const { return is_dirty_; }
    inline void SetDirty(bool dirty) { is_dirty_ = dirty; }

    // 重置页面
    void Reset() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        lsn_ = INVALID_LSN;
        pin_count_.store(0);
        is_dirty_ = false;
    }

private:
    char data_[PAGE_SIZE]{};           // 页面数据（固定8KB）
    page_id_t page_id_{INVALID_PAGE_ID};
    lsn_t lsn_{INVALID_LSN};
    std::atomic<int> pin_count_{0};
    bool is_dirty_{false};
};

} // namespace minidb
