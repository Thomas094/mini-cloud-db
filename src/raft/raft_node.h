#pragma once

#include "raft/raft_log.h"
#include "raft/raft_rpc.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// 前向声明
class RaftCluster;

// ============================================================
// Raft Node - Raft 协议核心节点
// ============================================================
//
// 【面试核心知识点 - Raft 节点状态】
//
// 每个 Raft 节点维护以下持久化状态（需要写入磁盘）：
//   - currentTerm：当前任期号（单调递增）
//   - votedFor：当前任期投票给了谁（null 表示未投票）
//   - log[]：日志条目
//
// 每个节点维护以下易失性状态（内存中）：
//   - commitIndex：已知被提交的最高日志索引
//   - lastApplied：已应用到状态机的最高日志索引
//
// Leader 额外维护（选举后初始化）：
//   - nextIndex[]：对每个 Follower，下一条要发送的日志索引
//   - matchIndex[]：对每个 Follower，已知已复制的最高日志索引
//
// ╔══════════════════════════════════════════════════════════╗
// ║              Raft 节点状态转换                            ║
// ║                                                          ║
// ║  启动 → Follower                                        ║
// ║           │                                              ║
// ║           │ 选举超时                                     ║
// ║           ▼                                              ║
// ║        Candidate ──── 获得多数票 ────→ Leader            ║
// ║           │                              │               ║
// ║           │ 发现更高 term                │ 发现更高 term  ║
// ║           ▼                              ▼               ║
// ║        Follower ◄────────────────── Follower             ║
// ╚══════════════════════════════════════════════════════════╝
//

class RaftNode {
public:
    // 构造函数
    // id: 本节点ID
    // peer_ids: 集群中其他节点的ID列表
    explicit RaftNode(node_id_t id, const std::vector<node_id_t>& peer_ids);
    ~RaftNode() = default;

    // ============================================================
    // TODO: 你来实现 - 处理 RequestVote RPC
    // ============================================================
    //
    // 当收到候选人的投票请求时：
    //
    // 实现步骤（参考 Raft 论文 Figure 2）：
    //   1. 如果 args.term < currentTerm_ → 拒绝（reply.term = currentTerm_）
    //   2. 如果 args.term > currentTerm_ → 更新 currentTerm_，转为 Follower
    //   3. 检查是否可以投票：
    //      - votedFor_ 为空（未投票）或已经投给了该候选人
    //      - 候选人的日志至少和自己一样新（选举限制）
    //   4. "日志至少一样新"的判断：
    //      - 先比较最后一条日志的 term，term 大的更新
    //      - term 相同则比较 index，index 大的更新
    //
    // 【面试重点 - 选举限制（Election Restriction）】
    //   这是 Raft 安全性的关键保证！
    //   只有拥有所有已提交日志的候选人才能当选 Leader。
    //   因为已提交的日志一定存在于多数节点上，
    //   而候选人需要多数票才能当选，
    //   所以至少有一个投票者拥有所有已提交日志，
    //   如果候选人的日志不够新，这个投票者不会投票给它。
    //
    RequestVoteReply HandleRequestVote(const RequestVoteArgs& args);

    // ============================================================
    // TODO: 你来实现 - 处理 AppendEntries RPC
    // ============================================================
    //
    // 当收到 Leader 的 AppendEntries 请求时：
    //
    // 实现步骤（参考 Raft 论文 Figure 2）：
    //   1. 如果 args.term < currentTerm_ → 拒绝
    //   2. 如果 args.term >= currentTerm_ → 承认 Leader，重置选举超时
    //   3. 一致性检查：
    //      检查 prevLogIndex 处的日志 term 是否等于 prevLogTerm
    //      如果不匹配 → 拒绝（返回冲突信息帮助 Leader 快速回退）
    //   4. 追加新日志条目（处理冲突）
    //   5. 更新 commitIndex：
    //      if args.leaderCommit > commitIndex_:
    //        commitIndex_ = min(args.leaderCommit, 最后一条新日志的 index)
    //   6. 应用已提交但未应用的日志到状态机
    //
    // 【面试高频题 - 日志不一致的场景】
    //
    //   场景：Leader 崩溃后，新 Leader 的日志可能与 Follower 不一致
    //
    //   Leader (term=3):  [1:1] [2:1] [3:2] [4:3] [5:3]
    //   Follower A:       [1:1] [2:1] [3:2]              ← 缺少日志
    //   Follower B:       [1:1] [2:1] [3:2] [4:2] [5:2]  ← 有冲突日志
    //
    //   Leader 通过 AppendEntries 的一致性检查逐步修复：
    //   对 Follower B: prevLogIndex=4, prevLogTerm=3 → 不匹配
    //                  prevLogIndex=3, prevLogTerm=2 → 匹配！
    //                  → 删除 index 4,5 → 追加 Leader 的 index 4,5
    //
    AppendEntriesReply HandleAppendEntries(const AppendEntriesArgs& args);

    // ============================================================
    // TODO: 你来实现 - 发起选举
    // ============================================================
    //
    // 当选举超时触发时调用：
    //
    // 实现步骤：
    //   1. 转为 Candidate
    //   2. 自增 currentTerm_
    //   3. 投票给自己（votedFor_ = id_）
    //   4. 重置选举超时
    //   5. 向所有 peer 发送 RequestVote RPC
    //   6. 统计票数：
    //      - 获得多数票 → 成为 Leader（调用 BecomeLeader）
    //      - 发现更高 term → 退回 Follower
    //      - 超时 → 重新选举
    //
    void StartElection();

    // ============================================================
    // TODO: 你来实现 - 成为 Leader 后的初始化
    // ============================================================
    //
    // 实现步骤：
    //   1. 设置角色为 LEADER
    //   2. 初始化 nextIndex[]：对每个 Follower 设为 lastLogIndex + 1
    //   3. 初始化 matchIndex[]：对每个 Follower 设为 0
    //   4. 立即发送一轮心跳（空的 AppendEntries）
    //
    // 【面试要点】
    //   为什么 nextIndex 初始化为 lastLogIndex + 1？
    //   因为 Leader 乐观地假设 Follower 的日志和自己一样新。
    //   如果不是，AppendEntries 会失败，Leader 会递减 nextIndex 重试。
    //
    void BecomeLeader();

    // ============================================================
    // TODO: 你来实现 - Leader 发送心跳/日志
    // ============================================================
    //
    // Leader 定期调用，向所有 Follower 发送 AppendEntries：
    //
    // 对每个 Follower：
    //   1. 构造 AppendEntriesArgs：
    //      - prevLogIndex = nextIndex[peer] - 1
    //      - prevLogTerm = log.GetTermAt(prevLogIndex)
    //      - entries = log.GetEntriesFrom(nextIndex[peer])
    //      - leaderCommit = commitIndex_
    //   2. 发送 RPC 并处理响应：
    //      - 如果成功：更新 nextIndex 和 matchIndex
    //      - 如果失败：递减 nextIndex 重试
    //        （优化：使用 conflictIndex 快速回退）
    //   3. 检查是否有新的日志可以提交（UpdateCommitIndex）
    //
    void SendHeartbeats();

    // ============================================================
    // TODO: 你来实现 - 客户端提交命令
    // ============================================================
    //
    // 只有 Leader 才能接受客户端命令：
    //   1. 如果不是 Leader → 返回 false
    //   2. 创建新的日志条目（term = currentTerm_）
    //   3. 追加到本地日志
    //   4. 立即触发一轮 AppendEntries 复制到 Follower
    //   5. 返回 true（注意：此时日志还未提交）
    //
    // 【面试要点】
    //   客户端命令的提交是异步的！
    //   Propose 返回 true 只表示 Leader 接受了命令，
    //   真正提交要等多数节点确认后才会更新 commitIndex。
    //
    bool Propose(const std::string& command);

    // ============================================================
    // TODO: 你来实现 - 更新 commitIndex
    // ============================================================
    //
    // Leader 专用：检查是否有新的日志可以提交
    //
    // 算法（Raft 论文 Figure 2）：
    //   对于 N = commitIndex+1 到 lastLogIndex：
    //     如果 matchIndex[i] >= N 的节点数 > 集群半数
    //     且 log[N].term == currentTerm_
    //     → 更新 commitIndex = N
    //
    // 【关键】为什么要检查 log[N].term == currentTerm_？
    //   这是 Raft 安全性的重要保证！
    //   Leader 不能提交之前任期的日志条目（即使已复制到多数节点）。
    //   只能通过提交当前任期的日志来间接提交之前的日志。
    //
    //   反例（论文 Figure 8）：
    //     如果允许提交旧 term 的日志，可能导致已提交的日志被覆盖！
    //
    void UpdateCommitIndex();

    // ============================================================
    // TODO: 你来实现 - 应用已提交的日志到状态机
    // ============================================================
    //
    // 将 lastApplied_ + 1 到 commitIndex_ 之间的日志应用到状态机
    //
    // 实现步骤：
    //   while lastApplied_ < commitIndex_:
    //     lastApplied_++
    //     entry = log_.GetEntry(lastApplied_)
    //     ApplyToStateMachine(entry)  // 调用回调函数
    //
    void ApplyCommitted();

    // 设置状态机回调（当日志被应用时调用）
    void SetApplyCallback(std::function<void(const LogEntry&)> callback) {
        apply_callback_ = std::move(callback);
    }

    // 设置集群引用（用于发送 RPC）
    void SetCluster(RaftCluster* cluster) { cluster_ = cluster; }

    // ============================================================
    // Tick - 时钟驱动
    // ============================================================
    // Raft 使用逻辑时钟驱动超时：
    //   - Follower/Candidate：选举超时 → 发起选举
    //   - Leader：心跳超时 → 发送心跳
    //
    void Tick();

    // Getter 方法
    node_id_t GetId() const { return id_; }
    NodeRole GetRole() const { return role_; }
    term_t GetCurrentTerm() const { return current_term_; }
    log_index_t GetCommitIndex() const { return commit_index_; }
    log_index_t GetLastApplied() const { return last_applied_; }
    const RaftLog& GetLog() const { return log_; }
    node_id_t GetLeaderId() const { return leader_id_; }

    // 获取已应用到状态机的命令列表（用于测试验证）
    std::vector<std::string> GetAppliedCommands() const {
        std::lock_guard<std::mutex> lock(node_mutex_);
        return applied_commands_;
    }

    // 是否为 Leader
    bool IsLeader() const { return role_ == NodeRole::LEADER; }

private:
    // 转为 Follower
    void BecomeFollower(term_t term);

    // 重置选举超时（随机化）
    void ResetElectionTimeout();

    // 检查候选人日志是否至少和自己一样新
    bool IsLogUpToDate(log_index_t last_log_index, term_t last_log_term) const;

    // 应用单条日志到状态机
    void ApplyToStateMachine(const LogEntry& entry);

    // ---- 节点基本信息 ----
    node_id_t id_;                          // 本节点ID
    std::vector<node_id_t> peer_ids_;       // 其他节点ID列表

    // ---- 持久化状态（生产环境需要写磁盘） ----
    term_t current_term_ = 0;               // 当前任期号
    node_id_t voted_for_ = 0;              // 当前任期投票给了谁（0=未投票）
    bool has_voted_ = false;                // 是否已投票
    RaftLog log_;                           // 日志

    // ---- 易失性状态 ----
    log_index_t commit_index_ = 0;          // 已提交的最高日志索引
    log_index_t last_applied_ = 0;          // 已应用到状态机的最高日志索引
    NodeRole role_ = NodeRole::FOLLOWER;    // 当前角色
    node_id_t leader_id_ = 0;              // 当前已知的 Leader ID

    // ---- Leader 专用状态 ----
    std::unordered_map<node_id_t, log_index_t> next_index_;   // 下一条要发送的日志索引
    std::unordered_map<node_id_t, log_index_t> match_index_;  // 已知已复制的最高日志索引

    // ---- 超时控制 ----
    int election_timeout_ = 0;              // 选举超时（tick 数）
    int election_elapsed_ = 0;              // 自上次重置以来经过的 tick 数
    int heartbeat_timeout_ = 3;             // 心跳间隔（tick 数）
    int heartbeat_elapsed_ = 0;             // 自上次心跳以来经过的 tick 数

    // ---- 回调和引用 ----
    std::function<void(const LogEntry&)> apply_callback_;
    RaftCluster* cluster_ = nullptr;

    // ---- 状态机（简化版：记录已应用的命令） ----
    std::vector<std::string> applied_commands_;

    // ---- 随机数生成器 ----
    std::mt19937 rng_;

    // ---- 互斥锁 ----
    mutable std::mutex node_mutex_;
};

} // namespace minidb
