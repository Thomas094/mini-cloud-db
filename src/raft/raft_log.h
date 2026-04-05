#pragma once

#include "raft/raft_rpc.h"
#include <mutex>
#include <vector>

namespace minidb {

// ============================================================
// Raft Log - 日志管理
// ============================================================
//
// 【面试核心知识点 - Raft 日志】
//
// Raft 日志是有序的日志条目序列，每个条目包含：
//   - 任期号（term）：标识该条目在哪个任期被创建
//   - 索引（index）：在日志中的位置，从 1 开始
//   - 命令（command）：要应用到状态机的操作
//
// ╔══════════════════════════════════════════════════════════╗
// ║                  日志结构示意                             ║
// ║                                                          ║
// ║  index:  1     2     3     4     5     6     7           ║
// ║  term:  [1]   [1]   [1]   [2]   [3]   [3]   [3]        ║
// ║  cmd:  SET   SET   DEL   SET   SET   DEL   SET          ║
// ║         x=1   y=2   z     a=3   b=4   c     d=5        ║
// ║                                                          ║
// ║  committed: ──────────────────────►                      ║
// ║                              ▲                           ║
// ║                         commitIndex = 5                  ║
// ║                                                          ║
// ║  applied:  ────────────►                                 ║
// ║                    ▲                                     ║
// ║              lastApplied = 3                             ║
// ╚══════════════════════════════════════════════════════════╝
//
// 两个关键属性（Log Matching Property）：
//   1. 如果两个日志在相同 index 处有相同 term，
//      则它们存储相同的命令。
//   2. 如果两个日志在相同 index 处有相同 term，
//      则从头到该 index 的所有日志都相同。
//

class RaftLog {
public:
    RaftLog();
    ~RaftLog() = default;

    // ============================================================
    // TODO: 你来实现 - 追加日志条目
    // ============================================================
    // 在日志末尾追加一个新条目
    //
    // 实现步骤：
    //   1. 设置 entry 的 index = 当前日志长度 + 1
    //   2. 将 entry 追加到 entries_ 向量末尾
    //   3. 返回新条目的 index
    //
    log_index_t Append(LogEntry entry);

    // ============================================================
    // TODO: 你来实现 - 追加多个日志条目（用于 AppendEntries RPC）
    // ============================================================
    // Leader 发来的日志条目，需要处理冲突
    //
    // 实现步骤：
    //   1. 从 prev_log_index + 1 开始，逐个比较
    //   2. 如果发现冲突（同一 index 但 term 不同）：
    //      - 删除该 index 及之后的所有条目
    //      - 追加新条目
    //   3. 如果没有冲突，只追加新的条目
    //
    // 【关键】不要删除已经匹配的条目！
    //   Raft 论文 Figure 2 明确指出：
    //   "If an existing entry conflicts with a new one
    //    (same index but different terms), delete the
    //    existing entry and all that follow it"
    //
    void AppendEntries(log_index_t prev_log_index,
                       const std::vector<LogEntry>& entries);

    // ============================================================
    // TODO: 你来实现 - 获取指定索引的日志条目
    // ============================================================
    // 返回 index 处的日志条目
    // 如果 index 超出范围，返回空的 LogEntry
    //
    LogEntry GetEntry(log_index_t index) const;

    // ============================================================
    // TODO: 你来实现 - 获取从 start_index 开始的所有日志条目
    // ============================================================
    // 用于 Leader 构造 AppendEntries 请求
    //
    std::vector<LogEntry> GetEntriesFrom(log_index_t start_index) const;

    // ============================================================
    // TODO: 你来实现 - 获取指定索引处的任期号
    // ============================================================
    // 如果 index == 0 或超出范围，返回 0
    //
    term_t GetTermAt(log_index_t index) const;

    // 获取最后一条日志的索引
    log_index_t GetLastIndex() const;

    // 获取最后一条日志的任期号
    term_t GetLastTerm() const;

    // 获取日志条目数量
    size_t Size() const;

    // 检查在 index 处是否有 term 匹配的日志
    // 用于 AppendEntries 的一致性检查
    bool MatchAt(log_index_t index, term_t term) const;

    // ============================================================
    // TODO: 你来实现 - 截断日志
    // ============================================================
    // 删除 from_index 及之后的所有日志条目
    // 用于处理日志冲突
    //
    void TruncateFrom(log_index_t from_index);

private:
    // 日志条目存储（index 从 1 开始，entries_[0] 是哨兵）
    std::vector<LogEntry> entries_;

    mutable std::mutex log_mutex_;
};

} // namespace minidb
