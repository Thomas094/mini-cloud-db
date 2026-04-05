#include "raft/raft_node.h"
#include "raft/raft_cluster.h"
#include <algorithm>
#include <cassert>

namespace minidb {

// ============================================================
// 构造函数
// ============================================================
RaftNode::RaftNode(node_id_t id, const std::vector<node_id_t>& peer_ids)
    : id_(id)
    , peer_ids_(peer_ids)
    , rng_(std::random_device{}() + id)  // 用 id 做种子偏移，确保不同节点有不同的随机序列
{
    ResetElectionTimeout();
}

// ============================================================
// 处理 RequestVote RPC
// ============================================================
RequestVoteReply RaftNode::HandleRequestVote(const RequestVoteArgs& args) {
    std::lock_guard<std::mutex> lock(node_mutex_);

    RequestVoteReply reply;
    reply.term = current_term_;
    reply.vote_granted = false;

    // 规则 1：如果候选人的 term 比自己小，直接拒绝
    if (args.term < current_term_) {
        return reply;
    }

    // 规则 2：如果候选人的 term 比自己大，更新 term，转为 Follower
    if (args.term > current_term_) {
        current_term_ = args.term;
        role_ = NodeRole::FOLLOWER;
        voted_for_ = 0;
        has_voted_ = false;
        leader_id_ = 0;
    }

    reply.term = current_term_;

    // 规则 3：检查是否可以投票
    // 条件 a：还没投票，或者已经投给了该候选人
    bool can_vote = (!has_voted_ || voted_for_ == args.candidate_id);

    // 条件 b：候选人的日志至少和自己一样新（选举限制）
    bool log_ok = IsLogUpToDate(args.last_log_index, args.last_log_term);

    if (can_vote && log_ok) {
        voted_for_ = args.candidate_id;
        has_voted_ = true;
        reply.vote_granted = true;
        // 投票后重置选举超时，防止自己也发起选举
        ResetElectionTimeout();
    }

    return reply;
}

// ============================================================
// 处理 AppendEntries RPC
// ============================================================
AppendEntriesReply RaftNode::HandleAppendEntries(const AppendEntriesArgs& args) {
    std::lock_guard<std::mutex> lock(node_mutex_);

    AppendEntriesReply reply;
    reply.term = current_term_;
    reply.success = false;
    reply.conflict_index = 0;
    reply.conflict_term = 0;

    // 规则 1：如果 Leader 的 term 比自己小，拒绝
    if (args.term < current_term_) {
        return reply;
    }

    // 收到合法的 AppendEntries，承认 Leader
    if (args.term >= current_term_) {
        current_term_ = args.term;
        role_ = NodeRole::FOLLOWER;
        leader_id_ = args.leader_id;
        voted_for_ = 0;
        has_voted_ = false;
    }

    // 重置选举超时（收到心跳了）
    ResetElectionTimeout();

    reply.term = current_term_;

    // 规则 2：一致性检查
    // 检查 prevLogIndex 处的日志是否匹配
    if (args.prev_log_index > 0) {
        if (args.prev_log_index > log_.GetLastIndex()) {
            // Follower 的日志太短
            reply.conflict_index = log_.GetLastIndex() + 1;
            reply.conflict_term = 0;
            return reply;
        }

        term_t prev_term = log_.GetTermAt(args.prev_log_index);
        if (prev_term != args.prev_log_term) {
            // term 不匹配，返回冲突信息
            reply.conflict_term = prev_term;
            // 找到该 term 的第一条日志，帮助 Leader 快速回退
            log_index_t idx = args.prev_log_index;
            while (idx > 1 && log_.GetTermAt(idx - 1) == prev_term) {
                idx--;
            }
            reply.conflict_index = idx;
            return reply;
        }
    }

    // 规则 3：追加新日志条目
    if (!args.entries.empty()) {
        log_.AppendEntries(args.prev_log_index, args.entries);
    }

    // 规则 4：更新 commitIndex
    if (args.leader_commit > commit_index_) {
        log_index_t last_new_index = args.prev_log_index +
                                     static_cast<log_index_t>(args.entries.size());
        commit_index_ = std::min(args.leader_commit, last_new_index);
    }

    // 应用已提交的日志
    while (last_applied_ < commit_index_) {
        last_applied_++;
        LogEntry entry = log_.GetEntry(last_applied_);
        if (!entry.command.empty()) {
            applied_commands_.push_back(entry.command);
            if (apply_callback_) {
                apply_callback_(entry);
            }
        }
    }

    reply.success = true;
    return reply;
}

// ============================================================
// 发起选举
// ============================================================
void RaftNode::StartElection() {
    // 注意：调用者已持有锁或在 Tick 中调用

    // 1. 转为 Candidate
    role_ = NodeRole::CANDIDATE;

    // 2. 自增任期号
    current_term_++;

    // 3. 投票给自己
    voted_for_ = id_;
    has_voted_ = true;

    // 4. 重置选举超时
    ResetElectionTimeout();

    // 5. 统计票数（自己投了一票）
    int votes_received = 1;
    int total_nodes = static_cast<int>(peer_ids_.size()) + 1;
    int majority = total_nodes / 2 + 1;

    // 保存当前状态用于 RPC
    term_t election_term = current_term_;
    log_index_t last_log_index = log_.GetLastIndex();
    term_t last_log_term = log_.GetLastTerm();

    // 6. 向所有 peer 发送 RequestVote
    if (cluster_ == nullptr) return;

    for (node_id_t peer_id : peer_ids_) {
        RequestVoteArgs args;
        args.term = election_term;
        args.candidate_id = id_;
        args.last_log_index = last_log_index;
        args.last_log_term = last_log_term;

        // 通过集群模拟器发送 RPC
        RequestVoteReply reply = cluster_->SendRequestVote(peer_id, args);

        // 检查是否还是 Candidate（可能在处理其他 RPC 时已经变了）
        if (role_ != NodeRole::CANDIDATE || current_term_ != election_term) {
            return;
        }

        // 发现更高的 term → 退回 Follower
        if (reply.term > current_term_) {
            BecomeFollower(reply.term);
            return;
        }

        if (reply.vote_granted) {
            votes_received++;
            if (votes_received >= majority) {
                // 获得多数票，成为 Leader！
                BecomeLeader();
                return;
            }
        }
    }
}

// ============================================================
// 成为 Leader
// ============================================================
void RaftNode::BecomeLeader() {
    role_ = NodeRole::LEADER;
    leader_id_ = id_;

    // 初始化 nextIndex 和 matchIndex
    log_index_t last_index = log_.GetLastIndex();
    for (node_id_t peer_id : peer_ids_) {
        next_index_[peer_id] = last_index + 1;
        match_index_[peer_id] = 0;
    }

    // 立即发送一轮心跳，宣告自己是 Leader
    SendHeartbeats();
}

// ============================================================
// 发送心跳/日志复制
// ============================================================
void RaftNode::SendHeartbeats() {
    if (role_ != NodeRole::LEADER || cluster_ == nullptr) return;

    for (node_id_t peer_id : peer_ids_) {
        // 构造 AppendEntries 请求
        AppendEntriesArgs args;
        args.term = current_term_;
        args.leader_id = id_;
        args.leader_commit = commit_index_;

        log_index_t next_idx = next_index_[peer_id];
        args.prev_log_index = next_idx - 1;
        args.prev_log_term = log_.GetTermAt(args.prev_log_index);
        args.entries = log_.GetEntriesFrom(next_idx);

        // 发送 RPC
        AppendEntriesReply reply = cluster_->SendAppendEntries(peer_id, args);

        // 检查是否还是 Leader
        if (role_ != NodeRole::LEADER) return;

        // 发现更高的 term → 退回 Follower
        if (reply.term > current_term_) {
            BecomeFollower(reply.term);
            return;
        }

        if (reply.success) {
            // 更新 nextIndex 和 matchIndex
            if (!args.entries.empty()) {
                next_index_[peer_id] = args.entries.back().index + 1;
                match_index_[peer_id] = args.entries.back().index;
            }
        } else {
            // 日志不一致，回退 nextIndex
            if (reply.conflict_term > 0) {
                // 优化：使用冲突信息快速回退
                // 在 Leader 日志中查找 conflict_term 的最后一条
                log_index_t new_next = reply.conflict_index;
                for (log_index_t i = log_.GetLastIndex(); i > 0; i--) {
                    if (log_.GetTermAt(i) == reply.conflict_term) {
                        new_next = i + 1;
                        break;
                    }
                }
                next_index_[peer_id] = new_next;
            } else {
                next_index_[peer_id] = reply.conflict_index;
            }
            // 确保 nextIndex 至少为 1
            if (next_index_[peer_id] < 1) {
                next_index_[peer_id] = 1;
            }
        }
    }

    // 检查是否有新的日志可以提交
    UpdateCommitIndex();
}

// ============================================================
// 客户端提交命令
// ============================================================
bool RaftNode::Propose(const std::string& command) {
    std::lock_guard<std::mutex> lock(node_mutex_);

    if (role_ != NodeRole::LEADER) {
        return false;
    }

    // 创建新的日志条目
    LogEntry entry;
    entry.term = current_term_;
    entry.command = command;

    // 追加到本地日志
    log_index_t index = log_.Append(entry);

    // 更新自己的 matchIndex
    // （Leader 自己的日志当然是匹配的）

    // 立即触发一轮日志复制
    SendHeartbeats();

    return true;
}

// ============================================================
// 更新 commitIndex（Leader 专用）
// ============================================================
void RaftNode::UpdateCommitIndex() {
    if (role_ != NodeRole::LEADER) return;

    // 从后往前检查，找到最大的可提交 index
    for (log_index_t n = log_.GetLastIndex(); n > commit_index_; n--) {
        // 只能提交当前任期的日志
        if (log_.GetTermAt(n) != current_term_) {
            continue;
        }

        // 统计有多少节点已经复制了该日志
        int replicated = 1;  // Leader 自己
        for (node_id_t peer_id : peer_ids_) {
            if (match_index_.count(peer_id) && match_index_[peer_id] >= n) {
                replicated++;
            }
        }

        int total_nodes = static_cast<int>(peer_ids_.size()) + 1;
        if (replicated > total_nodes / 2) {
            // 多数节点已复制，可以提交
            commit_index_ = n;

            // 应用已提交的日志
            ApplyCommitted();
            break;
        }
    }
}

// ============================================================
// 应用已提交的日志到状态机
// ============================================================
void RaftNode::ApplyCommitted() {
    while (last_applied_ < commit_index_) {
        last_applied_++;
        LogEntry entry = log_.GetEntry(last_applied_);
        if (!entry.command.empty()) {
            ApplyToStateMachine(entry);
        }
    }
}

// ============================================================
// Tick - 时钟驱动
// ============================================================
//
// 【设计说明】
// Raft 使用逻辑时钟而非物理时钟，这样更容易测试和调试。
// 每次 Tick 代表一个时间单位。
//
// 超时设置（典型值）：
//   - 心跳间隔：50-100ms（这里用 3 个 tick）
//   - 选举超时：150-300ms（这里用 10-20 个 tick，随机化）
//
// 【面试要点】
//   选举超时必须远大于心跳间隔！
//   否则 Follower 会频繁发起不必要的选举。
//   典型关系：broadcastTime << electionTimeout << MTBF
//   (broadcastTime: 网络往返时间, MTBF: 平均故障间隔)
//
void RaftNode::Tick() {
    std::lock_guard<std::mutex> lock(node_mutex_);

    if (role_ == NodeRole::LEADER) {
        // Leader：检查心跳超时
        heartbeat_elapsed_++;
        if (heartbeat_elapsed_ >= heartbeat_timeout_) {
            heartbeat_elapsed_ = 0;
            SendHeartbeats();
        }
    } else {
        // Follower / Candidate：检查选举超时
        election_elapsed_++;
        if (election_elapsed_ >= election_timeout_) {
            election_elapsed_ = 0;
            StartElection();
        }
    }
}

// ============================================================
// 私有辅助方法
// ============================================================

void RaftNode::BecomeFollower(term_t term) {
    role_ = NodeRole::FOLLOWER;
    current_term_ = term;
    voted_for_ = 0;
    has_voted_ = false;
    leader_id_ = 0;
    ResetElectionTimeout();
}

void RaftNode::ResetElectionTimeout() {
    // 随机化选举超时：10-20 个 tick
    std::uniform_int_distribution<int> dist(10, 20);
    election_timeout_ = dist(rng_);
    election_elapsed_ = 0;
}

bool RaftNode::IsLogUpToDate(log_index_t last_log_index, term_t last_log_term) const {
    term_t my_last_term = log_.GetLastTerm();
    log_index_t my_last_index = log_.GetLastIndex();

    // 先比较最后一条日志的 term
    if (last_log_term != my_last_term) {
        return last_log_term > my_last_term;
    }
    // term 相同则比较 index
    return last_log_index >= my_last_index;
}

void RaftNode::ApplyToStateMachine(const LogEntry& entry) {
    applied_commands_.push_back(entry.command);
    if (apply_callback_) {
        apply_callback_(entry);
    }
}

} // namespace minidb
