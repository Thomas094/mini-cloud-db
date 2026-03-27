#include "recovery/wal_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

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
// TODO: 你来实现
// ============================================================
lsn_t WALManager::AppendLog(LogRecord& record) {
    // std::lock_guard<std::mutex> lock(wal_mutex_);
    //
    // 实现提示：
    //
    // 1. 分配 LSN
    //    lsn_t lsn = current_lsn_.fetch_add(1) + 1;
    //    record.lsn_ = lsn;
    //
    // 2. 检查缓冲区空间是否足够
    //    if (buffer_offset_ + record.size_ > WAL_BUFFER_SIZE) {
    //        Flush();  // 缓冲区满了，先刷盘
    //    }
    //
    // 3. 将 record 序列化到缓冲区
    //    memcpy(wal_buffer_ + buffer_offset_, &record, record.size_);
    //    buffer_offset_ += record.size_;
    //
    // 4. return lsn;
    //
    // 【思考】为什么 current_lsn_ 用 atomic？
    //   因为在 Group Commit 优化中，多个线程可能并发分配 LSN。
    //   但实际写入缓冲区仍需要互斥（或使用更精细的并发控制）。

    throw std::runtime_error("AppendLog: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void WALManager::Flush() {
    // std::lock_guard<std::mutex> lock(wal_mutex_);
    //
    // 1. 将缓冲区写入文件
    //    ssize_t written = write(wal_fd_, wal_buffer_, buffer_offset_);
    //    if (written < 0) throw ...;
    //
    // 2. 强制刷盘 —— 这是持久性的关键！
    //    fsync(wal_fd_);
    //
    // 3. 清空缓冲区
    //    buffer_offset_ = 0;
    //
    // 4. 更新已刷盘LSN
    //    flushed_lsn_.store(current_lsn_.load());
    //
    // 【性能思考】
    //   fsync 是非常昂贵的操作（可能几毫秒）。
    //   Group Commit 的核心思想：
    //     - 收集一批事务的日志
    //     - 一次 fsync 搞定
    //     - 摊薄每个事务的 fsync 成本

    throw std::runtime_error("Flush: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void WALManager::Recover() {
    // 简化版恢复流程：
    //
    // 1. 将文件指针移到开头
    //    lseek(wal_fd_, 0, SEEK_SET);
    //
    // 2. 循环读取日志记录
    //    LogRecord record;
    //    while (read(wal_fd_, &record, sizeof(record)) == sizeof(record)) {
    //        switch (record.type_) {
    //            case LogRecordType::INSERT:
    //            case LogRecordType::UPDATE:
    //                // Redo: 将 new_data 应用到对应页面
    //                // 需要通过 BufferPoolManager 获取页面
    //                break;
    //            case LogRecordType::TXN_COMMIT:
    //                // 标记事务为已提交
    //                break;
    //            case LogRecordType::TXN_ABORT:
    //                // 需要 Undo 该事务的所有修改
    //                break;
    //        }
    //    }
    //
    // 【进阶挑战】实现完整的 ARIES 恢复
    //   需要维护 ATT (Active Transaction Table) 和 DPT (Dirty Page Table)

    throw std::runtime_error("Recover: 尚未实现");
}

} // namespace minidb
