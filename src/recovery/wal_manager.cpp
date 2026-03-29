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
void WALManager::Recover(BufferPoolManager* bpm) {
    if (bpm == nullptr) {
        throw std::runtime_error("Recover: BufferPoolManager 不能为空");
    }

    // 1. 将文件指针移到开头
    lseek(wal_fd_, 0, SEEK_SET);

    // 2. 循环读取日志记录
    LogRecord record;
    std::map<lsn_t, LogRecord> txn_records;       // 所有日志记录（用于 Undo 链回溯）
    std::set<txn_id_t> committed_txns;             // 已提交的事务集合
    std::set<txn_id_t> aborted_txns;               // 已回滚的事务集合
    std::map<txn_id_t, lsn_t> active_txn_last_lsn; // 活跃事务的最后一条 LSN

    // ========== Phase 1: Redo（前向扫描，重放所有记录） ==========
    while (read(wal_fd_, &record, sizeof(record)) == sizeof(record)) {
        txn_records[record.lsn_] = record;

        // 记录每个事务的最后一条 LSN（用于后续 Undo 未提交事务）
        if (record.txn_id_ != INVALID_TXN_ID) {
            active_txn_last_lsn[record.txn_id_] = record.lsn_;
        }

        switch (record.type_) {
            case LogRecordType::INSERT:
            case LogRecordType::UPDATE: {
                // Redo: 将 new_data 应用到对应页面
                Page *page = bpm->FetchPage(record.page_id_);
                if (page != nullptr && page->GetLSN() < record.lsn_) {
                    memcpy(page->GetUserData() + record.offset_, record.new_data_,
                           record.new_data_len_);
                    page->SetLSN(record.lsn_);  // 更新页面 LSN，防止重复 Redo（写入 PageHeader，随页面持久化）
                }
                if (page != nullptr) {
                    bpm->UnpinPage(record.page_id_, true);
                }
                break;
            }
            case LogRecordType::DELETE: {
                // Redo DELETE: 将 new_data（空数据或标记）应用到对应页面
                Page *page = bpm->FetchPage(record.page_id_);
                if (page != nullptr && page->GetLSN() < record.lsn_) {
                    if (record.new_data_len_ > 0) {
                        memcpy(page->GetUserData() + record.offset_, record.new_data_,
                               record.new_data_len_);
                    } else {
                        // 无 new_data 时，清零对应区域（使用 old_data_len_ 作为清除长度）
                        memset(page->GetUserData() + record.offset_, 0, record.old_data_len_);
                    }
                    page->SetLSN(record.lsn_);
                }
                if (page != nullptr) {
                    bpm->UnpinPage(record.page_id_, true);
                }
                break;
            }
            case LogRecordType::TXN_COMMIT:
                // 标记事务为已提交
                committed_txns.insert(record.txn_id_);
                active_txn_last_lsn.erase(record.txn_id_);
                break;
            case LogRecordType::TXN_ABORT: {
                // 显式 Abort：沿 prev_lsn 链回溯，Undo 该事务的所有修改
                aborted_txns.insert(record.txn_id_);
                active_txn_last_lsn.erase(record.txn_id_);
                lsn_t lsn = record.prev_lsn_;
                while (lsn != INVALID_LSN && txn_records.find(lsn) != txn_records.end()) {
                    auto& r = txn_records[lsn];
                    if (r.type_ == LogRecordType::INSERT || r.type_ == LogRecordType::UPDATE ||
                        r.type_ == LogRecordType::DELETE) {
                        Page *page = bpm->FetchPage(r.page_id_);
                        if (page != nullptr) {
                            memcpy(page->GetUserData() + r.offset_, r.old_data_, r.old_data_len_);
                            page->SetLSN(record.lsn_);  // Undo 后更新 LSN
                            bpm->UnpinPage(r.page_id_, true);
                        }
                    }
                    lsn = r.prev_lsn_;
                }
                break;
            }
            default:
                // TXN_BEGIN, CHECKPOINT 等暂不处理
                break;
        }
    }

    // ========== Phase 2: Undo（回滚崩溃时未提交的事务） ==========
    // active_txn_last_lsn 中剩余的事务既没有 COMMIT 也没有 ABORT，
    // 说明崩溃时它们正在运行，需要回滚。
    for (auto& [txn_id, last_lsn] : active_txn_last_lsn) {
        lsn_t lsn = last_lsn;
        while (lsn != INVALID_LSN && txn_records.find(lsn) != txn_records.end()) {
            auto& r = txn_records[lsn];
            if (r.type_ == LogRecordType::INSERT || r.type_ == LogRecordType::UPDATE ||
                r.type_ == LogRecordType::DELETE) {
                Page *page = bpm->FetchPage(r.page_id_);
                if (page != nullptr) {
                    memcpy(page->GetUserData() + r.offset_, r.old_data_, r.old_data_len_);
                    bpm->UnpinPage(r.page_id_, true);
                }
            }
            lsn = r.prev_lsn_;
        }
    }
}

} // namespace minidb
