#pragma once

#include "raft/raft_node.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <iostream>

namespace minidb {

// ============================================================
// Raft Cluster - 集群模拟器
// ============================================================
//
// 【设计说明】
//
// 这是一个单进程内的 Raft 集群模拟器，用于学习和测试。
// 真实的 Raft 实现中，节点之间通过网络通信（gRPC/TCP）。
// 这里我们用直接函数调用来模拟 RPC，省去网络层的复杂性。
//
// 模拟器支持：
//   1. 创建多节点集群
//   2. 模拟网络分区（断开/恢复节点间的连接）
//   3. 模拟节点宕机和恢复
//   4. 驱动时钟推进（Tick）
//   5. 观察集群状态变化
//
// ╔══════════════════════════════════════════════════════════╗
// ║                  集群模拟架构                             ║
// ║                                                          ║
// ║   ┌─────────┐    RPC     ┌─────────┐    RPC             ║
// ║   │ Node 1  │ ◄────────► │ Node 2  │ ◄──────►           ║
// ║   │(Leader) │            │(Follower)│         ┌────────┐ ║
// ║   └─────────┘            └─────────┘         │ Node 3 │ ║
// ║        ▲                      ▲              │(Follow.)│ ║
// ║        │         RPC          │              └────────┘ ║
// ║        └──────────────────────┘                   ▲      ║
// ║                                                   │      ║
// ║   RaftCluster: 管理所有节点，模拟网络，驱动时钟     ║
// ╚══════════════════════════════════════════════════════════╝
//

class RaftCluster {
public:
    // ============================================================
    // 创建集群
    // ============================================================
    // node_count: 集群节点数量（通常为奇数：3, 5, 7）
    //
    // 【面试要点】
    //   为什么 Raft 集群通常是奇数个节点？
    //   因为 Raft 需要多数派（majority）才能工作。
    //   3 个节点可以容忍 1 个故障（majority = 2）
    //   5 个节点可以容忍 2 个故障（majority = 3）
    //   4 个节点也只能容忍 1 个故障（majority = 3），
    //   和 3 个节点的容错能力一样，但多了一个节点的开销。
    //
    explicit RaftCluster(int node_count);
    ~RaftCluster() = default;

    // ============================================================
    // RPC 模拟
    // ============================================================
    // 这些方法模拟网络 RPC 调用
    // 如果目标节点不可达（宕机或网络分区），返回拒绝响应
    //
    RequestVoteReply SendRequestVote(node_id_t target, const RequestVoteArgs& args);
    AppendEntriesReply SendAppendEntries(node_id_t target, const AppendEntriesArgs& args);

    // ============================================================
    // 时钟驱动
    // ============================================================
    // 推进所有节点的逻辑时钟
    // ticks: 推进的 tick 数
    //
    void TickAll(int ticks = 1);

    // 只推进指定节点的时钟
    void TickNode(node_id_t id, int ticks = 1);

    // ============================================================
    // 网络分区模拟
    // ============================================================
    //
    // 【面试高频题 - 网络分区（Network Partition）】
    //
    // 网络分区是分布式系统最棘手的故障之一。
    // Raft 如何处理网络分区？
    //
    // 场景：5 节点集群 [A, B, C, D, E]，A 是 Leader
    //   分区 1: [A, B]     ← 少数派，A 仍认为自己是 Leader
    //   分区 2: [C, D, E]  ← 多数派，会选出新 Leader
    //
    //   分区 1 中：
    //     - A 发送 AppendEntries 给 C,D,E 都失败
    //     - A 无法提交新日志（得不到多数确认）
    //     - 客户端写入 A 的数据不会被提交
    //
    //   分区 2 中：
    //     - C/D/E 选举超时，发起选举
    //     - 某个节点（如 C）获得 D,E 的投票，成为新 Leader
    //     - 新 Leader 可以正常服务（3/5 = 多数）
    //
    //   分区恢复后：
    //     - A 发现 C 的 term 更高 → A 退回 Follower
    //     - A,B 的未提交日志被 C 的日志覆盖
    //     - 集群恢复一致
    //

    // 断开两个节点之间的连接
    void Disconnect(node_id_t a, node_id_t b);

    // 恢复两个节点之间的连接
    void Reconnect(node_id_t a, node_id_t b);

    // 隔离一个节点（断开它与所有其他节点的连接）
    void IsolateNode(node_id_t id);

    // 恢复一个节点的所有连接
    void RecoverNode(node_id_t id);

    // ============================================================
    // 节点宕机/恢复模拟
    // ============================================================

    // 模拟节点宕机（停止处理任何消息）
    void CrashNode(node_id_t id);

    // 模拟节点恢复（重新开始处理消息）
    void RestartNode(node_id_t id);

    // ============================================================
    // 查询集群状态
    // ============================================================

    // 获取当前 Leader（如果有的话）
    // 返回 Leader 的 ID，如果没有 Leader 返回 0
    node_id_t GetLeader() const;

    // 获取指定节点
    std::shared_ptr<RaftNode> GetNode(node_id_t id) const;

    // 获取所有节点
    const std::unordered_map<node_id_t, std::shared_ptr<RaftNode>>& GetNodes() const {
        return nodes_;
    }

    // 检查两个节点之间是否连通
    bool IsConnected(node_id_t a, node_id_t b) const;

    // 检查节点是否存活
    bool IsAlive(node_id_t id) const;

    // 打印集群状态（调试用）
    void PrintStatus() const;

    // ============================================================
    // 客户端操作
    // ============================================================

    // 向 Leader 提交命令
    // 自动找到 Leader 并提交
    // 返回 true 表示提交成功（Leader 接受了命令）
    bool ClientPropose(const std::string& command);

    // 检查所有存活节点的已应用命令是否一致
    bool CheckConsistency() const;

private:
    // 节点集合
    std::unordered_map<node_id_t, std::shared_ptr<RaftNode>> nodes_;

    // 网络连通性（存储断开的连接对）
    std::unordered_set<uint64_t> disconnected_pairs_;

    // 宕机的节点
    std::unordered_set<node_id_t> crashed_nodes_;

    // 生成连接对的 key
    uint64_t MakePairKey(node_id_t a, node_id_t b) const {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }
};

} // namespace minidb
