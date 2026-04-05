#include "raft/raft_log.h"
#include <algorithm>

namespace minidb {

// 构造函数：插入一个哨兵条目（index=0），简化边界处理
RaftLog::RaftLog() {
    // 哨兵条目：index=0, term=0
    // 这样 GetTermAt(0) 返回 0，MatchAt(0, 0) 返回 true
    // 避免了大量的边界条件判断
    entries_.emplace_back(LogEntry{0, 0, ""});
}

// ============================================================
// TODO: 你来实现 - 追加日志条目
// ============================================================
//
// 提示：
//   - entries_[0] 是哨兵，实际日志从 entries_[1] 开始
//   - 新条目的 index = entries_.size()（因为哨兵占了 [0]）
//   - 记得加锁保证线程安全
//
log_index_t RaftLog::Append(LogEntry entry) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    entry.index = static_cast<log_index_t>(entries_.size());
    entries_.push_back(std::move(entry));
    return entries_.back().index;
}

// ============================================================
// TODO: 你来实现 - 追加多个日志条目（AppendEntries RPC 使用）
// ============================================================
//
// 这是 Raft 日志复制的核心逻辑之一。
//
// 算法：
//   for each entry in entries:
//     idx = prev_log_index + 1 + i
//     if idx <= GetLastIndex() && GetTermAt(idx) != entry.term:
//       TruncateFrom(idx)  // 删除冲突及之后的日志
//     if idx > GetLastIndex():
//       Append(entry)      // 追加新条目
//
// 【易错点】
//   不要无脑删除 prev_log_index 之后的所有日志再追加！
//   因为 Follower 可能已经有一些正确的日志，
//   只需要删除冲突的部分。
//
void RaftLog::AppendEntries(log_index_t prev_log_index,
                            const std::vector<LogEntry>& entries) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    for (size_t i = 0; i < entries.size(); ++i) {
        log_index_t idx = prev_log_index + 1 + static_cast<log_index_t>(i);

        if (idx < static_cast<log_index_t>(entries_.size())) {
            // 该位置已有日志，检查是否冲突
            if (entries_[idx].term != entries[i].term) {
                // 冲突！删除该位置及之后的所有日志
                entries_.resize(idx);
                // 追加新条目
                LogEntry new_entry = entries[i];
                new_entry.index = idx;
                entries_.push_back(std::move(new_entry));
            }
            // 如果 term 相同，不做任何操作（已经匹配）
        } else {
            // 该位置没有日志，直接追加
            LogEntry new_entry = entries[i];
            new_entry.index = idx;
            entries_.push_back(std::move(new_entry));
        }
    }
}

// ============================================================
// TODO: 你来实现 - 获取指定索引的日志条目
// ============================================================
LogEntry RaftLog::GetEntry(log_index_t index) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (index == 0 || index >= static_cast<log_index_t>(entries_.size())) {
        return LogEntry{};
    }
    return entries_[index];
}

// ============================================================
// TODO: 你来实现 - 获取从 start_index 开始的所有日志条目
// ============================================================
std::vector<LogEntry> RaftLog::GetEntriesFrom(log_index_t start_index) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::vector<LogEntry> result;
    if (start_index >= static_cast<log_index_t>(entries_.size())) {
        return result;
    }
    for (log_index_t i = start_index; i < static_cast<log_index_t>(entries_.size()); ++i) {
        result.push_back(entries_[i]);
    }
    return result;
}

// ============================================================
// TODO: 你来实现 - 获取指定索引处的任期号
// ============================================================
term_t RaftLog::GetTermAt(log_index_t index) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (index >= static_cast<log_index_t>(entries_.size())) {
        return 0;
    }
    return entries_[index].term;
}

log_index_t RaftLog::GetLastIndex() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    // entries_[0] 是哨兵，所以最后一条日志的 index = size - 1
    return static_cast<log_index_t>(entries_.size()) - 1;
}

term_t RaftLog::GetLastTerm() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return entries_.back().term;
}

size_t RaftLog::Size() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    // 减去哨兵
    return entries_.size() - 1;
}

bool RaftLog::MatchAt(log_index_t index, term_t term) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (index >= static_cast<log_index_t>(entries_.size())) {
        return false;
    }
    // index == 0 时，entries_[0].term == 0，与 term == 0 匹配
    return entries_[index].term == term;
}

// ============================================================
// TODO: 你来实现 - 截断日志
// ============================================================
//
// 提示：直接 resize entries_ 到 from_index 即可
//
void RaftLog::TruncateFrom(log_index_t from_index) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (from_index < static_cast<log_index_t>(entries_.size()) && from_index > 0) {
        entries_.resize(from_index);
    }
}

} // namespace minidb
