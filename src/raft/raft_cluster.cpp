#include "raft/raft_cluster.h"
#include <algorithm>
#include <iostream>

namespace minidb {

// ============================================================
// 创建集群
// ============================================================
RaftCluster::RaftCluster(int node_count) {
    // 创建节点 ID 列表（从 1 开始）
    std::vector<node_id_t> all_ids;
    for (int i = 1; i <= node_count; i++) {
        all_ids.push_back(static_cast<node_id_t>(i));
    }

    // 创建每个节点
    for (node_id_t id : all_ids) {
        // 构造 peer 列表（除了自己以外的所有节点）
        std::vector<node_id_t> peers;
        for (node_id_t other : all_ids) {
            if (other != id) {
                peers.push_back(other);
            }
        }

        auto node = std::make_shared<RaftNode>(id, peers);
        node->SetCluster(this);
        nodes_[id] = node;
    }
}

// ============================================================
// RPC 模拟 - RequestVote
// ============================================================
RequestVoteReply RaftCluster::SendRequestVote(node_id_t target,
                                               const RequestVoteArgs& args) {
    RequestVoteReply reply;
    reply.term = 0;
    reply.vote_granted = false;

    // 检查目标节点是否存活
    if (crashed_nodes_.count(target)) {
        return reply;
    }

    // 检查网络是否连通
    if (!IsConnected(args.candidate_id, target)) {
        return reply;
    }

    // 调用目标节点的处理函数
    auto it = nodes_.find(target);
    if (it != nodes_.end()) {
        reply = it->second->HandleRequestVote(args);
    }

    return reply;
}

// ============================================================
// RPC 模拟 - AppendEntries
// ============================================================
AppendEntriesReply RaftCluster::SendAppendEntries(node_id_t target,
                                                   const AppendEntriesArgs& args) {
    AppendEntriesReply reply;
    reply.term = 0;
    reply.success = false;
    reply.conflict_index = 0;
    reply.conflict_term = 0;

    // 检查目标节点是否存活
    if (crashed_nodes_.count(target)) {
        return reply;
    }

    // 检查网络是否连通
    if (!IsConnected(args.leader_id, target)) {
        return reply;
    }

    // 调用目标节点的处理函数
    auto it = nodes_.find(target);
    if (it != nodes_.end()) {
        reply = it->second->HandleAppendEntries(args);
    }

    return reply;
}

// ============================================================
// 时钟驱动
// ============================================================
void RaftCluster::TickAll(int ticks) {
    for (int t = 0; t < ticks; t++) {
        for (auto& [id, node] : nodes_) {
            if (!crashed_nodes_.count(id)) {
                node->Tick();
            }
        }
    }
}

void RaftCluster::TickNode(node_id_t id, int ticks) {
    auto it = nodes_.find(id);
    if (it != nodes_.end() && !crashed_nodes_.count(id)) {
        for (int t = 0; t < ticks; t++) {
            it->second->Tick();
        }
    }
}

// ============================================================
// 网络分区模拟
// ============================================================
void RaftCluster::Disconnect(node_id_t a, node_id_t b) {
    disconnected_pairs_.insert(MakePairKey(a, b));
}

void RaftCluster::Reconnect(node_id_t a, node_id_t b) {
    disconnected_pairs_.erase(MakePairKey(a, b));
}

void RaftCluster::IsolateNode(node_id_t id) {
    for (auto& [other_id, _] : nodes_) {
        if (other_id != id) {
            Disconnect(id, other_id);
        }
    }
}

void RaftCluster::RecoverNode(node_id_t id) {
    for (auto& [other_id, _] : nodes_) {
        if (other_id != id) {
            Reconnect(id, other_id);
        }
    }
}

// ============================================================
// 节点宕机/恢复
// ============================================================
void RaftCluster::CrashNode(node_id_t id) {
    crashed_nodes_.insert(id);
}

void RaftCluster::RestartNode(node_id_t id) {
    crashed_nodes_.erase(id);
}

// ============================================================
// 查询集群状态
// ============================================================
node_id_t RaftCluster::GetLeader() const {
    node_id_t leader = 0;
    term_t max_term = 0;

    for (auto& [id, node] : nodes_) {
        if (!crashed_nodes_.count(id) && node->IsLeader()) {
            if (node->GetCurrentTerm() > max_term) {
                max_term = node->GetCurrentTerm();
                leader = id;
            }
        }
    }
    return leader;
}

std::shared_ptr<RaftNode> RaftCluster::GetNode(node_id_t id) const {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        return it->second;
    }
    return nullptr;
}

bool RaftCluster::IsConnected(node_id_t a, node_id_t b) const {
    return disconnected_pairs_.find(MakePairKey(a, b)) == disconnected_pairs_.end();
}

bool RaftCluster::IsAlive(node_id_t id) const {
    return crashed_nodes_.find(id) == crashed_nodes_.end();
}

void RaftCluster::PrintStatus() const {
    std::cout << "\n========== 集群状态 ==========\n";
    for (auto& [id, node] : nodes_) {
        bool alive = !crashed_nodes_.count(id);
        std::cout << "  Node " << id
                  << " | 角色: " << NodeRoleToString(node->GetRole())
                  << " | 任期: " << node->GetCurrentTerm()
                  << " | 日志长度: " << node->GetLog().Size()
                  << " | commitIndex: " << node->GetCommitIndex()
                  << " | lastApplied: " << node->GetLastApplied()
                  << " | " << (alive ? "存活" : "宕机")
                  << "\n";
    }
    std::cout << "================================\n\n";
}

// ============================================================
// 客户端操作
// ============================================================
bool RaftCluster::ClientPropose(const std::string& command) {
    node_id_t leader = GetLeader();
    if (leader == 0) {
        return false;
    }
    return nodes_[leader]->Propose(command);
}

bool RaftCluster::CheckConsistency() const {
    std::vector<std::string> reference;
    bool has_reference = false;

    for (auto& [id, node] : nodes_) {
        if (crashed_nodes_.count(id)) continue;

        auto commands = node->GetAppliedCommands();
        if (!has_reference) {
            reference = commands;
            has_reference = true;
        } else {
            // 检查已应用的命令前缀是否一致
            size_t min_len = std::min(reference.size(), commands.size());
            for (size_t i = 0; i < min_len; i++) {
                if (reference[i] != commands[i]) {
                    std::cerr << "不一致！Node " << id
                              << " 在 index " << i
                              << " 处命令不同: " << commands[i]
                              << " vs " << reference[i] << "\n";
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace minidb
