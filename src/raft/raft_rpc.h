#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

// ============================================================
// Raft 协议 - RPC 消息定义
// ============================================================
//
// 【面试核心知识点 - Raft 协议概述】
//
// Raft 是一种分布式一致性协议，由 Diego Ongaro 在 2014 年提出。
// 相比 Paxos，Raft 更容易理解和实现。
//
// ╔══════════════════════════════════════════════════════════╗
// ║                  Raft 协议三大核心                        ║
// ║                                                          ║
// ║  1. Leader Election（领导者选举）                         ║
// ║     - 集群中只有一个 Leader                               ║
// ║     - Leader 挂了，其他节点发起选举                        ║
// ║     - 获得多数票的候选人成为新 Leader                      ║
// ║                                                          ║
// ║  2. Log Replication（日志复制）                           ║
// ║     - 客户端请求只发给 Leader                             ║
// ║     - Leader 将日志复制到所有 Follower                    ║
// ║     - 多数节点确认后，日志被提交                           ║
// ║                                                          ║
// ║  3. Safety（安全性保证）                                  ║
// ║     - 选举限制：只有日志足够新的节点才能当选               ║
// ║     - 已提交的日志不会丢失                                ║
// ╚══════════════════════════════════════════════════════════╝
//
// Raft 只需要两种 RPC：
//   1. RequestVote    - 候选人请求投票
//   2. AppendEntries  - Leader 复制日志 / 心跳
//

// 节点ID类型
using node_id_t = uint32_t;

// 任期号类型（Raft 的核心概念之一）
using term_t = uint64_t;

// 日志索引类型
using log_index_t = uint64_t;

// ============================================================
// 日志条目 (Log Entry)
// ============================================================
//
// 每个日志条目包含：
//   - term：创建该条目时的任期号
//   - index：在日志中的位置（从1开始）
//   - command：要执行的命令（状态机命令）
//
// 【面试要点】
//   日志条目一旦被提交（committed），就保证不会丢失。
//   "提交"的定义：Leader 将该条目复制到了多数节点。
//
struct LogEntry {
    term_t term = 0;           // 该条目被创建时的任期号
    log_index_t index = 0;     // 日志索引（从1开始）
    std::string command;       // 状态机命令

    LogEntry() = default;
    LogEntry(term_t t, log_index_t idx, const std::string& cmd)
        : term(t), index(idx), command(cmd) {}
};

// ============================================================
// RequestVote RPC - 请求投票
// ============================================================
//
// 候选人在发起选举时，向其他节点发送 RequestVote 请求。
//
// 【选举流程】
//   1. Follower 超时未收到心跳 → 转为 Candidate
//   2. Candidate 自增 currentTerm，投票给自己
//   3. 向所有其他节点发送 RequestVote
//   4. 收到多数票 → 成为 Leader
//   5. 收到更高 term 的消息 → 退回 Follower
//   6. 超时未获得多数票 → 重新选举
//
// 【面试高频题】
//   Q: 为什么需要随机化选举超时？
//   A: 防止多个节点同时发起选举导致"分票"（split vote），
//      随机超时让某个节点大概率先超时并赢得选举。
//

struct RequestVoteArgs {
    term_t term;                  // 候选人的任期号
    node_id_t candidate_id;       // 请求投票的候选人ID
    log_index_t last_log_index;   // 候选人最后一条日志的索引
    term_t last_log_term;         // 候选人最后一条日志的任期号
};

struct RequestVoteReply {
    term_t term;           // 当前任期号（让候选人更新自己）
    bool vote_granted;     // true = 投票给该候选人
};

// ============================================================
// AppendEntries RPC - 追加日志 / 心跳
// ============================================================
//
// Leader 使用 AppendEntries 实现两个功能：
//   1. 日志复制：将新的日志条目发送给 Follower
//   2. 心跳：entries 为空时就是心跳，维持 Leader 地位
//
// 【日志复制流程】
//
//   Client → Leader: "SET x = 1"
//     │
//     ▼
//   Leader: 追加到本地日志 [term=3, index=5, cmd="SET x=1"]
//     │
//     ├──→ Follower A: AppendEntries(entries=[...])
//     ├──→ Follower B: AppendEntries(entries=[...])
//     └──→ Follower C: AppendEntries(entries=[...])
//           │
//           ▼
//   收到多数确认 → Leader 提交该日志 → 应用到状态机
//     │
//     ▼
//   下一次 AppendEntries 中 leaderCommit 更新
//     → Follower 也提交并应用
//
// 【一致性检查 - 面试重点】
//   prevLogIndex 和 prevLogTerm 用于一致性检查：
//   Follower 检查自己在 prevLogIndex 位置的日志 term 是否等于 prevLogTerm。
//   如果不匹配 → 返回 false → Leader 回退 nextIndex 重试。
//   这保证了 Log Matching Property（日志匹配特性）：
//     如果两个日志在某个 index 处 term 相同，
//     那么从头到该 index 的所有日志都相同。
//

struct AppendEntriesArgs {
    term_t term;                    // Leader 的任期号
    node_id_t leader_id;            // Leader 的ID（让 Follower 知道谁是 Leader）
    log_index_t prev_log_index;     // 紧接新条目之前的日志索引
    term_t prev_log_term;           // prevLogIndex 处日志的任期号
    std::vector<LogEntry> entries;  // 要追加的日志条目（心跳时为空）
    log_index_t leader_commit;      // Leader 已提交的最高日志索引
};

struct AppendEntriesReply {
    term_t term;       // 当前任期号
    bool success;      // true = Follower 成功追加

    // 优化：快速回退（论文 §5.3 的优化）
    // 当 Follower 拒绝时，返回冲突信息帮助 Leader 快速定位
    log_index_t conflict_index;  // 冲突日志的起始索引
    term_t conflict_term;        // 冲突日志的任期号
};

// ============================================================
// 节点角色
// ============================================================
//
// Raft 中每个节点在任意时刻处于三种角色之一：
//
//   ┌──────────┐  超时   ┌───────────┐  获得多数票  ┌────────┐
//   │ Follower │ ──────→ │ Candidate │ ──────────→ │ Leader │
//   └──────────┘         └───────────┘              └────────┘
//        ▲                     │                        │
//        │    发现更高 term     │   发现更高 term         │
//        └─────────────────────┘                        │
//        └──────────────────────────────────────────────┘
//
enum class NodeRole : uint8_t {
    FOLLOWER = 0,
    CANDIDATE,
    LEADER,
};

inline const char* NodeRoleToString(NodeRole role) {
    switch (role) {
        case NodeRole::FOLLOWER:  return "Follower";
        case NodeRole::CANDIDATE: return "Candidate";
        case NodeRole::LEADER:    return "Leader";
        default:                  return "Unknown";
    }
}

} // namespace minidb
