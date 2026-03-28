#pragma once

#include "common/types.h"
#include "common/config.h"
#include <cstring>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>

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

    // 页面ID（原子变量，允许上层无锁安全读取）
    inline page_id_t GetPageId() const { return page_id_.load(); }
    inline void SetPageId(page_id_t page_id) { page_id_.store(page_id); }

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

    // ============================================================
    // 页面级读写锁
    // ============================================================
    // 读锁（共享）：多个线程可以同时读取页面数据
    //   使用场景：FetchPage 返回后，上层读取 data_
    inline void RLock() { page_latch_.lock_shared(); }
    inline void RUnlock() { page_latch_.unlock_shared(); }

    // 写锁（独占）：只有一个线程可以修改页面数据
    //   使用场景：FlushPage 写回磁盘、FetchPage 淘汰旧页面并加载新数据
    inline void WLock() { page_latch_.lock(); }
    inline void WUnlock() { page_latch_.unlock(); }
    std::shared_mutex& GetPageLatch() { return page_latch_; }

    // ============================================================
    // Loading 状态（类似 PostgreSQL 的 BM_IO_IN_PROGRESS）
    // ============================================================
    // 当一个帧正在从磁盘加载数据时，标记为 loading 状态。
    // 其他线程通过 page_table_ 命中该帧后，需要等待 loading 完成，
    // 避免读到不完整/旧的数据。
    inline bool IsLoading() const { return is_loading_; }
    inline void SetLoading(bool loading) { is_loading_ = loading; }

    // 通知所有等待 loading 完成的线程（调用者需持有 BPM latch_）
    inline void NotifyReady() { loading_cv_.notify_all(); }

    // 等待 loading 完成（调用者需持有 BPM latch_ 的 unique_lock）
    // condition_variable_any::wait 会自动释放 BPM latch_，
    // 让其他线程可以继续操作，被唤醒后重新获取锁。
    void WaitUntilReady(std::unique_lock<std::mutex>& bpm_latch) {
        loading_cv_.wait(bpm_latch, [this]() { return !is_loading_; });
    }

    // 重置页面
    void Reset() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_.store(INVALID_PAGE_ID);
        lsn_ = INVALID_LSN;
        pin_count_.store(0);
        is_dirty_ = false;
        is_loading_ = false;
    }

private:
    char data_[PAGE_SIZE]{};           // 页面数据（固定8KB）
    std::atomic<page_id_t> page_id_{INVALID_PAGE_ID};
    lsn_t lsn_{INVALID_LSN};
    std::atomic<int> pin_count_{0};
    bool is_dirty_{false};
    bool is_loading_{false};           // 是否正在加载中（类似 PG 的 BM_IO_IN_PROGRESS）
    std::shared_mutex page_latch_;     // 页面级读写锁
    std::condition_variable_any loading_cv_;  // loading 完成的通知
};

} // namespace minidb
