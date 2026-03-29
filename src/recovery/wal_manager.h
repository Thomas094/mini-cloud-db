#pragma once

#include "common/types.h"
#include "common/config.h"
#include "recovery/log_record.h"
#include <string>
#include <mutex>
#include <atomic>
#include <vector>

namespace minidb {

// 前向声明，Recover() 需要使用 BufferPoolManager
class BufferPoolManager;

// ============================================================
// WALManager - WAL 日志管理器
// ============================================================
//
// 【面试核心知识点】
//
// WAL 的写入流程：
//   1. 上层模块（如执行器）构造 LogRecord
//   2. 将 LogRecord 追加到 WAL Buffer
//   3. 分配 LSN（单调递增）
//   4. 事务提交时 → flush WAL Buffer 到磁盘（fsync）
//
// 恢复流程（ARIES 算法的简化版）：
//   Phase 1 - Redo：从 checkpoint 开始，前向扫描 WAL，重放所有记录
//   Phase 2 - Undo：反向扫描，回滚所有未提交事务
//
// Aurora 的不同之处：
//   - 计算节点不写数据页，只写 WAL
//   - WAL 发送到 6 个存储副本（Quorum: 4/6 写成功即可）
//   - 存储节点异步回放 WAL 重建页面
//
// Neon 的不同之处：
//   - WAL 先到 Safekeeper（保证持久性，类似 Paxos）
//   - Pageserver 从 Safekeeper 消费 WAL
//   - Pageserver 维护页面的历史版本（按 LSN 索引）
//
class WALManager {
public:
    explicit WALManager(const std::string& wal_file);
    ~WALManager();

    // ============================================================
    // TODO: 你来实现 - 追加日志记录
    // ============================================================
    // 将日志记录写入WAL缓冲区，返回分配的LSN
    //
    // 实现步骤：
    //   1. 加锁
    //   2. 分配新的 LSN: current_lsn_++
    //   3. 设置 record 的 lsn
    //   4. 将 record 序列化追加到 wal_buffer_
    //      - 如果 buffer 满了，先 Flush
    //   5. 返回分配的 LSN
    //
    // 【性能优化思考】
    //   这里用单个 mutex 是最简单的实现。
    //   PostgreSQL 使用 WALInsertLock（一组锁）来允许并发插入：
    //     - 多个事务可以同时往 WAL buffer 的不同位置写入
    //     - 但 flush 时需要保证顺序
    //   这个优化叫 "Group Commit"，是数据库面试的经典题目。
    //
    lsn_t AppendLog(LogRecord& record);

    // ============================================================
    // TODO: 你来实现 - 刷新WAL到磁盘
    // ============================================================
    // 将WAL缓冲区中所有内容刷新到磁盘文件
    //
    // 实现步骤：
    //   1. 将 wal_buffer_ 中的数据 write() 到 wal_fd_
    //   2. 调用 fsync(wal_fd_) 保证持久化
    //   3. 清空 wal_buffer_
    //   4. 更新 flushed_lsn_
    //
    // 【关键】这是保证事务持久性的最后一步！
    //   事务 COMMIT 时必须等 Flush 完成才能返回成功。
    //   这也是为什么"组提交"（Group Commit）这么重要——
    //   多个事务的日志可以合并在一次 fsync 中刷盘。
    //
    void Flush();

    // ============================================================
    // TODO: 你来实现 - 崩溃恢复（Redo）
    // ============================================================
    // 从WAL文件中读取所有日志记录并回放
    //
    // 实现提示（简化版，不实现完整的ARIES）：
    //   1. 打开 WAL 文件
    //   2. 循环读取 LogRecord
    //   3. 对于每条 INSERT/UPDATE/DELETE 记录：
    //      - 获取对应页面
    //      - 如果 page.lsn < record.lsn → 需要 redo
    //      - 将 record.new_data 应用到页面的 record.offset 位置
    //   4. 对于 TXN_ABORT 记录：
    //      - 沿着 prev_lsn 链回溯，undo 该事务的所有修改
    //
    // 【面试加分项】了解 ARIES 的三阶段：
    //   1. Analysis：确定哪些事务需要 redo/undo
    //   2. Redo：前向扫描，重放所有操作
    //   3. Undo：反向扫描，回滚未提交事务
    //
    void Recover(BufferPoolManager* bpm);

    // ============================================================
    // WAL 文件截断（Checkpoint 后调用）
    // ============================================================
    // 删除 checkpoint_lsn 及之前的所有日志记录，释放磁盘空间。
    //
    // 调用时机：
    //   Checkpoint 完成后（所有脏页已刷盘），调用此方法清理旧日志。
    //   checkpoint_lsn 之前的日志不再需要用于恢复。
    //
    // 【PostgreSQL 对应】
    //   PG 将 WAL 分成 16MB 的 segment 文件，Checkpoint 后
    //   通过 pg_archivecleanup 或自动回收旧 segment。
    //
    void Truncate(lsn_t checkpoint_lsn);

    // 获取当前LSN
    lsn_t GetCurrentLSN() const { return current_lsn_.load(); }

    // 获取已刷盘的LSN
    lsn_t GetFlushedLSN() const { return flushed_lsn_.load(); }

private:
    std::string wal_file_;
    int wal_fd_{-1};                   // WAL 文件描述符

    std::atomic<lsn_t> current_lsn_{0};   // 当前分配到的LSN
    std::atomic<lsn_t> flushed_lsn_{0};   // 已刷盘的最大LSN

    // WAL 缓冲区（简化实现，用vector）
    // 生产环境会用 mmap 或 共享内存 + 环形缓冲区
    char wal_buffer_[WAL_BUFFER_SIZE]{};
    size_t buffer_offset_{0};          // 当前缓冲区写入位置

    std::mutex wal_mutex_;

    // 内部无锁版本的 Flush，供 AppendLog 在已持有锁时调用，避免死锁
    void FlushInternal();
};

} // namespace minidb
