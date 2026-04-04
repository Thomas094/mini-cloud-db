#include "concurrency/commit_log.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>
#include <sys/types.h>
#include <stdexcept>
#include <cstring>

namespace minidb {

CommitLog::CommitLog(const std::string& clog_file) : clog_file_(clog_file) {
    // 打开或创建 CLOG 文件
    clog_fd_ = open(clog_file.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (clog_fd_ < 0) {
        throw std::runtime_error("CommitLog: 无法打开 CLOG 文件 " + clog_file);
    }

    // 尝试从文件加载已有数据
    ssize_t bytes_read = pread(clog_fd_, buffer_, CLOG_BUFFER_SIZE, 0);
    if (bytes_read < 0) {
        throw std::runtime_error("CommitLog: 读取 CLOG 文件失败");
    }
    // 如果文件比缓冲区小，剩余部分保持为 0（IN_PROGRESS）
}

CommitLog::~CommitLog() {
    // 析构时刷盘，确保不丢失状态
    if (is_dirty_) {
        try {
            Flush();
        } catch (...) {
            // 析构函数中不抛异常
        }
    }
    if (clog_fd_ >= 0) {
        close(clog_fd_);
    }
}

// ============================================================
// TODO: 你来实现 — 设置事务状态
// ============================================================
//
// 实现提示：
//   1. 加锁保护并发访问
//   2. 检查 txn_id 是否在有效范围内（< MAX_TRANSACTIONS）
//   3. 计算字节偏移和 bit 偏移
//   4. 读取缓冲区中的字节
//   5. 清除旧的 2 bit：byte &= ~(0x03 << bit_offset)
//   6. 写入新状态：byte |= (static_cast<uint8_t>(status) << bit_offset)
//   7. 写回缓冲区
//   8. 标记缓冲区为脏
//
// 【PostgreSQL 对应代码】
//   TransactionIdSetTreeStatus() → 设置 CLOG 页面中的 2 bit
//   然后标记 SLRU 页面为脏，等待 checkpoint 或显式 flush
//
void CommitLog::SetTransactionStatus(txn_id_t txn_id, CLogTxnStatus status) {
    if (txn_id >= MAX_TRANSACTIONS) {
        throw std::out_of_range("CommitLog: 事务 ID 超出范围");
    }
    size_t byte_offset = txn_id / 4;
    size_t bit_offset = (txn_id % 4) * 2;
    std::lock_guard<std::mutex> lock(clog_mutex_);
    char& byte = buffer_[byte_offset];
    byte &= ~(0x03 << bit_offset);
    byte |= (static_cast<uint8_t>(status) << bit_offset);
    is_dirty_ = true;
}

// ============================================================
// TODO: 你来实现 — 查询事务状态
// ============================================================
//
// 实现提示：
//   1. 加锁保护并发访问
//   2. 检查 txn_id 范围
//   3. 计算字节偏移和 bit 偏移
//   4. 读取缓冲区中的字节
//   5. 提取 2 bit：(byte >> bit_offset) & 0x03
//   6. 转换为 CLogTxnStatus 返回
//
// 【PostgreSQL 对应代码】
//   TransactionIdGetStatus() → 从 CLOG 页面读取 2 bit
//   如果页面不在缓冲区中，触发 SLRU 读取（可能有磁盘 I/O）
//
CLogTxnStatus CommitLog::GetTransactionStatus(txn_id_t txn_id) const {
    // TODO: 你来实现
    if (txn_id >= MAX_TRANSACTIONS) {
        throw std::out_of_range("CommitLog: 事务 ID 超出范围");
    }
    size_t byte_offset = txn_id / 4;
    size_t bit_offset = (txn_id % 4) * 2;
    std::lock_guard<std::mutex> lock(clog_mutex_);
    const char byte = buffer_[byte_offset];
    return static_cast<CLogTxnStatus>((byte >> bit_offset) & 0x03);
}

// ============================================================
// TODO: 你来实现 — 刷盘
// ============================================================
//
// 实现提示：
//   1. 加锁
//   2. 如果缓冲区不脏，直接返回
//   3. pwrite() 将整个缓冲区写入文件偏移 0
//   4. fsync() 确保持久化
//   5. 清除脏标记
//
// 【关键时机】
//   事务提交时的持久化顺序：
//     ① WAL 中写入 TXN_COMMIT 记录
//     ② WAL Flush（fsync）
//     ③ CLOG 中设置 COMMITTED
//     ④ CLOG Flush（fsync）
//   如果在 ③④ 之间崩溃，恢复时可以从 WAL 重建 CLOG。
//
void CommitLog::Flush() {
    std::lock_guard<std::mutex> lock(clog_mutex_);
    if (!is_dirty_) {
        return;
    }
    ssize_t bytes_written = 0;
    while (bytes_written < CLOG_BUFFER_SIZE) {
        ssize_t written = pwrite(clog_fd_, buffer_ + bytes_written, CLOG_BUFFER_SIZE - bytes_written, bytes_written);
        if (written < 0) {
            throw std::runtime_error("CommitLog: 写入 CLOG 文件失败" + std::string(strerror(errno)));
        }
        bytes_written += written;
    }
    if (int ret = fsync(clog_fd_)) {
        throw std::runtime_error("CommitLog: 同步 CLOG 文件失败" + std::string(strerror(errno)));
    }
    is_dirty_ = false;
}

// ============================================================
// 从 WAL 重建 CLOG（恢复用）
// ============================================================
void CommitLog::RebuildFromWAL(/* WALManager* wal */) {
    // 预留接口：扫描 WAL 中的 TXN_COMMIT/TXN_ABORT 记录，
    // 重建 CLOG 缓冲区。
    //
    // 实现思路：
    //   1. 清空缓冲区（所有事务状态重置为 IN_PROGRESS）
    //   2. 前向扫描 WAL：
    //      - 遇到 TXN_COMMIT → SetTransactionStatus(txn_id, COMMITTED)
    //      - 遇到 TXN_ABORT  → SetTransactionStatus(txn_id, ABORTED)
    //   3. Flush() 持久化
    //
    // 暂不实现，等 WALManager 和 CommitLog 集成后再补充。
}

} // namespace minidb
