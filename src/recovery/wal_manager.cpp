#include "recovery/wal_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "common/types.h"
#include "recovery/log_record.h"
#include "storage/page.h"
// PAGE_HEADER_SIZE 定义在 page.h 中，用于计算用户数据在 data_ 中的实际偏移

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <map>
#include <set>

namespace minidb {

WALManager::WALManager(const std::string& wal_file) : wal_file_(wal_file) {
    wal_fd_ = open(wal_file.c_str(), O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (wal_fd_ < 0) {
        throw std::runtime_error("WALManager: 无法打开WAL文件 " + wal_file);
    }
}

WALManager::~WALManager() {
    if (buffer_offset_ > 0) {
        Flush();
    }
    if (wal_fd_ >= 0) {
        close(wal_fd_);
    }
}

// ============================================================
// 内部无锁版本的 Flush，调用者必须已持有 wal_mutex_
// ============================================================
void WALManager::FlushInternal() {
    if (buffer_offset_ == 0) {
        return;  // 缓冲区为空，无需刷盘
    }

    // 1. 将缓冲区写入文件
    size_t remaining = buffer_offset_;
    size_t offset = 0;
    while (remaining > 0) {
        ssize_t written = write(wal_fd_, wal_buffer_ + offset, remaining);
        if (written < 0) {
            throw std::runtime_error("Flush: 写入WAL文件失败");
        }
        offset += written;
        remaining -= written;
    }

    // 2. 强制刷盘 —— 这是持久性的关键！
    fsync(wal_fd_);

    // 3. 清空缓冲区
    buffer_offset_ = 0;

    // 4. 更新已刷盘LSN
    flushed_lsn_.store(current_lsn_.load());
}

// ============================================================
// 追加日志记录
// ============================================================
lsn_t WALManager::AppendLog(LogRecord& record) {
    std::lock_guard<std::mutex> lock(wal_mutex_);

    // 1. 分配 LSN
    lsn_t lsn = current_lsn_.fetch_add(1) + 1;
    record.lsn_ = lsn;

    // 2. 检查缓冲区空间是否足够
    if (buffer_offset_ + record.size_ > WAL_BUFFER_SIZE) {
        FlushInternal();  // 调用无锁版本，避免死锁
    }

    // 3. 将 record 序列化到缓冲区
    memcpy(wal_buffer_ + buffer_offset_, &record, record.size_);
    buffer_offset_ += record.size_;

    return lsn;
}

// ============================================================
// 刷新WAL到磁盘（外部接口，自行加锁）
// ============================================================
void WALManager::Flush() {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    FlushInternal();
    //
    // 【性能思考】
    //   fsync 是非常昂贵的操作（可能几毫秒）。
    //   Group Commit 的核心思想：
    //     - 收集一批事务的日志
    //     - 一次 fsync 搞定
    //     - 摊薄每个事务的 fsync 成本
}

// ============================================================
// 崩溃恢复（Redo + Undo）
// 参数 bpm: 外部传入的 BufferPoolManager，用于获取和修改页面
// ============================================================
//
// 【内存优化】
//   旧实现：将所有日志记录存入 map<lsn, LogRecord>，内存占用 = 记录数 × ~8KB
//   新实现：只保留活跃事务（未提交/未回滚）的日志记录
//           已提交事务的日志在 Redo 后立即从内存中清除
//           已回滚事务的日志在 Undo 后立即清除
//
//   效果：假设 10,000 条日志中只有 5 个活跃事务各 10 条日志，
//         内存从 ~80MB 降到 ~400KB（降低 99.5%）
//
void WALManager::Recover(BufferPoolManager* bpm) {
    if (bpm == nullptr) {
        throw std::runtime_error("Recover: BufferPoolManager 不能为空");
    }

    // 1. 将文件指针移到开头
    lseek(wal_fd_, 0, SEEK_SET);

    // 2. 数据结构
    LogRecord record;
    // 【关键优化】只保留活跃事务的日志，按事务 ID 分组
    // key: txn_id → value: 该事务的所有日志记录（按 LSN 排序）
    std::map<txn_id_t, std::map<lsn_t, LogRecord>> active_txn_records;
    std::map<txn_id_t, lsn_t> active_txn_last_lsn; // 活跃事务的最后一条 LSN

    // ========== Phase 1: Redo（前向扫描，重放所有记录） ==========
    while (read(wal_fd_, &record, sizeof(record)) == sizeof(record)) {
        txn_id_t txn_id = record.txn_id_;

        switch (record.type_) {
            case LogRecordType::INSERT:
            case LogRecordType::UPDATE: {
                // Redo: 将 new_data 应用到对应页面
                Page *page = bpm->FetchPage(record.page_id_);
                if (page != nullptr && page->GetLSN() < record.lsn_) {
                    memcpy(page->GetUserData() + record.offset_, record.new_data_,
                           record.new_data_len_);
                    page->SetLSN(record.lsn_);
                }
                if (page != nullptr) {
                    bpm->UnpinPage(record.page_id_, true);
                }
                // 保留到活跃事务记录中（可能需要 Undo）
                if (txn_id != INVALID_TXN_ID) {
                    active_txn_records[txn_id][record.lsn_] = record;
                    active_txn_last_lsn[txn_id] = record.lsn_;
                }
                break;
            }
            case LogRecordType::DELETE: {
                Page *page = bpm->FetchPage(record.page_id_);
                if (page != nullptr && page->GetLSN() < record.lsn_) {
                    if (record.new_data_len_ > 0) {
                        memcpy(page->GetUserData() + record.offset_, record.new_data_,
                               record.new_data_len_);
                    } else {
                        memset(page->GetUserData() + record.offset_, 0, record.old_data_len_);
                    }
                    page->SetLSN(record.lsn_);
                }
                if (page != nullptr) {
                    bpm->UnpinPage(record.page_id_, true);
                }
                // 保留到活跃事务记录中
                if (txn_id != INVALID_TXN_ID) {
                    active_txn_records[txn_id][record.lsn_] = record;
                    active_txn_last_lsn[txn_id] = record.lsn_;
                }
                break;
            }
            case LogRecordType::TXN_BEGIN: {
                // 记录事务开始（可能后续需要 Undo）
                if (txn_id != INVALID_TXN_ID) {
                    active_txn_last_lsn[txn_id] = record.lsn_;
                }
                break;
            }
            case LogRecordType::TXN_COMMIT: {
                // 事务已提交 → 其日志不再需要 Undo，立即释放内存
                active_txn_records.erase(txn_id);
                active_txn_last_lsn.erase(txn_id);
                break;
            }
            case LogRecordType::TXN_ABORT: {
                // 显式 Abort：沿 prev_lsn 链回溯，Undo 该事务的所有修改
                auto it = active_txn_records.find(txn_id);
                if (it != active_txn_records.end()) {
                    // 从最新到最旧遍历该事务的日志（reverse order）
                    auto& txn_logs = it->second;
                    for (auto rit = txn_logs.rbegin(); rit != txn_logs.rend(); ++rit) {
                        auto& r = rit->second;
                        if (r.type_ == LogRecordType::INSERT || r.type_ == LogRecordType::UPDATE ||
                            r.type_ == LogRecordType::DELETE) {
                            Page *page = bpm->FetchPage(r.page_id_);
                            if (page != nullptr) {
                                memcpy(page->GetUserData() + r.offset_, r.old_data_, r.old_data_len_);
                                page->SetLSN(record.lsn_);
                                bpm->UnpinPage(r.page_id_, true);
                            }
                        }
                    }
                    // Undo 完成，释放该事务的日志内存
                    active_txn_records.erase(it);
                }
                active_txn_last_lsn.erase(txn_id);
                break;
            }
            default:
                break;
        }
    }

    // ========== Phase 2: Undo（回滚崩溃时未提交的事务） ==========
    // active_txn_records 中剩余的事务既没有 COMMIT 也没有 ABORT，
    // 说明崩溃时它们正在运行，需要回滚。
    for (auto& [txn_id, txn_logs] : active_txn_records) {
        // 从最新到最旧遍历（reverse order），确保正确的 Undo 顺序
        for (auto rit = txn_logs.rbegin(); rit != txn_logs.rend(); ++rit) {
            auto& r = rit->second;
            if (r.type_ == LogRecordType::INSERT || r.type_ == LogRecordType::UPDATE ||
                r.type_ == LogRecordType::DELETE) {
                Page *page = bpm->FetchPage(r.page_id_);
                if (page != nullptr) {
                    memcpy(page->GetUserData() + r.offset_, r.old_data_, r.old_data_len_);
                    bpm->UnpinPage(r.page_id_, true);
                }
            }
        }
    }
    // Phase 2 结束后 active_txn_records 自动析构，释放所有剩余内存
}

// ============================================================
// WAL 文件截断（Checkpoint 后调用）
// ============================================================
//
// 【为什么需要截断？】
//   WAL 文件是追加写入的，会无限增长。
//   当 Checkpoint 完成后（所有脏页已刷盘），checkpoint_lsn 之前的日志
//   不再需要用于恢复，可以安全删除。
//
// 【PostgreSQL 的做法】
//   PG 不截断文件，而是将旧的 WAL segment 文件回收（重命名）或删除。
//   WAL 被分成固定大小的 segment 文件（默认 16MB），如：
//     000000010000000000000001
//     000000010000000000000002
//   Checkpoint 后，旧 segment 被 pg_archivecleanup 清理。
//
// 【我们的简化实现】
//   由于只有一个 WAL 文件，采用"重写"策略：
//   1. 读取 checkpoint_lsn 之后的日志记录
//   2. 将它们写入新文件
//   3. 用新文件替换旧文件
//
void WALManager::Truncate(lsn_t checkpoint_lsn) {
    std::lock_guard<std::mutex> lock(wal_mutex_);

    // 1. 先将缓冲区中的数据刷盘，确保所有日志都在文件中
    FlushInternal();

    // 2. 从文件开头读取所有记录，保留 checkpoint_lsn 之后的
    lseek(wal_fd_, 0, SEEK_SET);

    std::vector<LogRecord> remaining_records;
    LogRecord record;
    while (read(wal_fd_, &record, sizeof(record)) == sizeof(record)) {
        if (record.lsn_ > checkpoint_lsn) {
            remaining_records.push_back(record);
        }
    }

    // 3. 截断文件并重写保留的记录
    //    先关闭旧文件，以截断模式重新打开
    close(wal_fd_);
    wal_fd_ = open(wal_file_.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (wal_fd_ < 0) {
        throw std::runtime_error("Truncate: 无法重新打开WAL文件 " + wal_file_);
    }

    // 4. 将保留的记录写回文件
    for (auto& rec : remaining_records) {
        size_t remaining = rec.size_;
        size_t offset = 0;
        const char* data = reinterpret_cast<const char*>(&rec);
        while (remaining > 0) {
            ssize_t written = write(wal_fd_, data + offset, remaining);
            if (written < 0) {
                throw std::runtime_error("Truncate: 写入WAL文件失败");
            }
            offset += written;
            remaining -= written;
        }
    }

    // 5. 刷盘确保持久化
    fsync(wal_fd_);

    // 6. 重新以追加模式打开（后续 AppendLog 需要 O_APPEND）
    close(wal_fd_);
    wal_fd_ = open(wal_file_.c_str(), O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
    if (wal_fd_ < 0) {
        throw std::runtime_error("Truncate: 无法以追加模式重新打开WAL文件 " + wal_file_);
    }
}

} // namespace minidb
