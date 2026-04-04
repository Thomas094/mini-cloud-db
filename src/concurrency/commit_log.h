#pragma once

#include "common/types.h"
#include "common/config.h"
#include <string>
#include <mutex>
#include <atomic>

namespace minidb {

// 前向声明
class DiskManager;

// ============================================================
// CommitLog (CLOG) — 事务状态持久化
// ============================================================
//
// 【面试核心知识点 — CLOG / pg_xact】
//
// MVCC 可见性判断需要知道 xmin/xmax 对应的事务是否已提交。
// 这个信息必须持久化，否则崩溃后无法判断可见性。
//
// PostgreSQL 的实现（pg_xact/ 目录）：
//   - 每个事务占 2 bit，记录状态：
//       00 = IN_PROGRESS（进行中）
//       01 = COMMITTED（已提交）
//       10 = ABORTED（已回滚）
//       11 = SUB_COMMITTED（子事务已提交）
//   - 一个 8KB 页面可存 32,768 个事务的状态
//   - 通过 Shared Buffer 缓存热点页面，按需读取
//   - 文件路径：$PGDATA/pg_xact/0000, 0001, ...
//
// 【为什么不直接用 WAL？】
//   WAL 记录了 TXN_COMMIT/TXN_ABORT，但恢复后 WAL 可能被截断。
//   CLOG 是独立的持久化结构，始终可用。
//   而且 CLOG 查询是 O(1)（直接按 txn_id 计算偏移），
//   而扫描 WAL 是 O(n)。
//
// 【Hint Bits 优化】
//   频繁查 CLOG 仍有开销（可能触发磁盘 I/O）。
//   PostgreSQL 在第一次查询后，将结果缓存到元组头部的 t_infomask 中：
//     HEAP_XMIN_COMMITTED / HEAP_XMIN_ABORTED
//   后续判断直接读 hint bit，无需再查 CLOG。
//

// CLOG 中每个事务的状态（2 bit）
enum class CLogTxnStatus : uint8_t {
    IN_PROGRESS  = 0b00,
    COMMITTED    = 0b01,
    ABORTED      = 0b10,
    SUB_COMMITTED = 0b11,  // 子事务（暂不使用）
};

class CommitLog {
public:
    // ============================================================
    // 构造函数
    // ============================================================
    // clog_file: CLOG 文件路径
    //
    explicit CommitLog(const std::string& clog_file);
    ~CommitLog();

    // 禁止拷贝
    CommitLog(const CommitLog&) = delete;
    CommitLog& operator=(const CommitLog&) = delete;

    // ============================================================
    // TODO: 你来实现 — 设置事务状态
    // ============================================================
    //
    // 实现步骤：
    //   1. 计算 txn_id 在文件中的字节偏移：byte_offset = txn_id / 4
    //      （每个字节存 4 个事务，每个事务 2 bit）
    //   2. 计算 bit 偏移：bit_offset = (txn_id % 4) * 2
    //   3. 读取该字节
    //   4. 清除旧的 2 bit，写入新状态
    //   5. 写回文件
    //
    // 【PostgreSQL 对应】
    //   TransactionIdSetTreeStatus() in src/backend/access/transam/clog.c
    //
    void SetTransactionStatus(txn_id_t txn_id, CLogTxnStatus status);

    // ============================================================
    // TODO: 你来实现 — 查询事务状态
    // ============================================================
    //
    // 实现步骤：
    //   1. 计算字节偏移和 bit 偏移（同上）
    //   2. 读取该字节
    //   3. 提取对应的 2 bit
    //   4. 返回状态
    //
    // 【PostgreSQL 对应】
    //   TransactionIdGetStatus() in src/backend/access/transam/clog.c
    //
    CLogTxnStatus GetTransactionStatus(txn_id_t txn_id) const;

    // ============================================================
    // TODO: 你来实现 — 刷盘
    // ============================================================
    //
    // 将 CLOG 缓冲区中的脏页写入磁盘并 fsync。
    // 事务提交时必须确保 CLOG 已持久化。
    //
    void Flush();

    // ============================================================
    // 恢复时从 WAL 重建 CLOG
    // ============================================================
    //
    // 如果 CLOG 文件损坏或丢失，可以从 WAL 中的
    // TXN_COMMIT/TXN_ABORT 记录重建。
    //
    // 【PostgreSQL 对应】
    //   pg_resetwal 工具可以重建 pg_xact
    //
    void RebuildFromWAL(/* WALManager* wal */);

private:
    std::string clog_file_;
    int clog_fd_{-1};

    // ============================================================
    // CLOG 缓冲区
    // ============================================================
    //
    // 【设计说明】
    //   简化实现：使用固定大小的内存缓冲区。
    //   每个字节存 4 个事务状态（2 bit/事务），
    //   CLOG_BUFFER_SIZE = 8KB 可存 32,768 个事务。
    //
    //   生产环境（PostgreSQL）使用 SLRU（Simple LRU）缓冲池：
    //   - 多个 8KB 页面组成的缓冲池
    //   - LRU 淘汰策略
    //   - 支持数百万事务
    //
    static constexpr size_t CLOG_BUFFER_SIZE = PAGE_SIZE;  // 8KB = 32K 事务
    static constexpr size_t MAX_TRANSACTIONS = CLOG_BUFFER_SIZE * 4;

    char buffer_[CLOG_BUFFER_SIZE]{};  // CLOG 缓冲区
    bool is_dirty_{false};             // 缓冲区是否有未刷盘的修改

    mutable std::mutex clog_mutex_;    // 保护并发访问

    // 内部辅助：计算 txn_id 在缓冲区中的位置
    static size_t GetByteOffset(txn_id_t txn_id) { return txn_id / 4; }
    static uint8_t GetBitOffset(txn_id_t txn_id) { return (txn_id % 4) * 2; }
};

} // namespace minidb
