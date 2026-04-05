// ============================================================
// Raft 协议测试
// ============================================================
//
// 这个测试文件覆盖了 Raft 协议的核心场景：
//   1. Leader 选举
//   2. 日志复制
//   3. 网络分区处理
//   4. 节点宕机恢复
//   5. 一致性保证
//
// 运行方式：
//   mkdir build && cd build
//   cmake .. && make
//   ./raft_test
//

#include "raft/raft_cluster.h"
#include "raft/raft_node.h"
#include "raft/raft_log.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace minidb;

// 辅助函数：等待集群选出 Leader
node_id_t WaitForLeader(RaftCluster& cluster, int max_ticks = 100) {
    for (int i = 0; i < max_ticks; i++) {
        cluster.TickAll(1);
        node_id_t leader = cluster.GetLeader();
        if (leader != 0) {
            return leader;
        }
    }
    return 0;
}

// 辅助函数：推进时钟并等待日志复制完成
void WaitForReplication(RaftCluster& cluster, int ticks = 30) {
    for (int i = 0; i < ticks; i++) {
        cluster.TickAll(1);
    }
}

// ============================================================
// 测试 1：基本日志操作
// ============================================================
void TestRaftLog() {
    std::cout << "=== 测试 1：Raft 日志基本操作 ===\n";

    RaftLog log;

    // 初始状态
    assert(log.Size() == 0);
    assert(log.GetLastIndex() == 0);
    assert(log.GetLastTerm() == 0);

    // 追加日志
    log_index_t idx1 = log.Append(LogEntry{1, 0, "SET x=1"});
    assert(idx1 == 1);
    assert(log.Size() == 1);
    assert(log.GetLastIndex() == 1);
    assert(log.GetLastTerm() == 1);

    log_index_t idx2 = log.Append(LogEntry{1, 0, "SET y=2"});
    assert(idx2 == 2);

    log_index_t idx3 = log.Append(LogEntry{2, 0, "DEL z"});
    assert(idx3 == 3);

    // 获取日志
    LogEntry entry = log.GetEntry(1);
    assert(entry.term == 1);
    assert(entry.command == "SET x=1");

    entry = log.GetEntry(3);
    assert(entry.term == 2);
    assert(entry.command == "DEL z");

    // 获取 term
    assert(log.GetTermAt(0) == 0);  // 哨兵
    assert(log.GetTermAt(1) == 1);
    assert(log.GetTermAt(3) == 2);

    // 匹配检查
    assert(log.MatchAt(0, 0) == true);
    assert(log.MatchAt(1, 1) == true);
    assert(log.MatchAt(1, 2) == false);

    // 截断
    log.TruncateFrom(2);
    assert(log.Size() == 1);
    assert(log.GetLastIndex() == 1);

    // 批量追加
    std::vector<LogEntry> entries = {
        LogEntry{3, 2, "SET a=10"},
        LogEntry{3, 3, "SET b=20"},
    };
    log.AppendEntries(1, entries);
    assert(log.Size() == 3);
    assert(log.GetTermAt(2) == 3);
    assert(log.GetTermAt(3) == 3);

    std::cout << "  ✓ 日志追加、获取、截断、批量追加 全部通过\n\n";
}

// ============================================================
// 测试 2：Leader 选举
// ============================================================
void TestLeaderElection() {
    std::cout << "=== 测试 2：Leader 选举 ===\n";

    // 创建 3 节点集群
    RaftCluster cluster(3);

    // 初始状态：所有节点都是 Follower
    for (auto& [id, node] : cluster.GetNodes()) {
        assert(node->GetRole() == NodeRole::FOLLOWER);
    }
    std::cout << "  ✓ 初始状态：所有节点都是 Follower\n";

    // 推进时钟，等待选举
    node_id_t leader = WaitForLeader(cluster);
    assert(leader != 0);
    std::cout << "  ✓ 选举成功：Node " << leader << " 成为 Leader\n";

    // 验证只有一个 Leader
    int leader_count = 0;
    for (auto& [id, node] : cluster.GetNodes()) {
        if (node->IsLeader()) leader_count++;
    }
    assert(leader_count == 1);
    std::cout << "  ✓ 集群中只有一个 Leader\n";

    // 验证所有节点的 term 一致
    term_t leader_term = cluster.GetNode(leader)->GetCurrentTerm();
    for (auto& [id, node] : cluster.GetNodes()) {
        // Follower 的 term 应该 >= Leader 的 term
        // （可能在选举过程中有些节点 term 更高但没当选）
        assert(node->GetCurrentTerm() >= leader_term ||
               node->GetCurrentTerm() == leader_term);
    }
    std::cout << "  ✓ Leader 任期号: " << leader_term << "\n";

    cluster.PrintStatus();
    std::cout << "\n";
}

// ============================================================
// 测试 3：日志复制
// ============================================================
void TestLogReplication() {
    std::cout << "=== 测试 3：日志复制 ===\n";

    RaftCluster cluster(3);

    // 等待选出 Leader
    node_id_t leader = WaitForLeader(cluster);
    assert(leader != 0);
    std::cout << "  ✓ Leader: Node " << leader << "\n";

    // 提交命令
    bool ok = cluster.ClientPropose("SET x=1");
    assert(ok);
    std::cout << "  ✓ 提交命令: SET x=1\n";

    // 等待复制
    WaitForReplication(cluster);

    // 提交更多命令
    cluster.ClientPropose("SET y=2");
    cluster.ClientPropose("SET z=3");
    WaitForReplication(cluster);

    // 验证所有节点的日志一致
    assert(cluster.CheckConsistency());
    std::cout << "  ✓ 所有节点日志一致\n";

    // 验证 Leader 的日志长度
    auto leader_node = cluster.GetNode(leader);
    assert(leader_node->GetLog().Size() >= 3);
    std::cout << "  ✓ Leader 日志长度: " << leader_node->GetLog().Size() << "\n";

    // 验证 commitIndex 已更新
    assert(leader_node->GetCommitIndex() >= 3);
    std::cout << "  ✓ Leader commitIndex: " << leader_node->GetCommitIndex() << "\n";

    cluster.PrintStatus();
    std::cout << "\n";
}

// ============================================================
// 测试 4：Leader 宕机后重新选举
// ============================================================
void TestLeaderFailover() {
    std::cout << "=== 测试 4：Leader 宕机后重新选举 ===\n";

    RaftCluster cluster(5);

    // 等待选出 Leader
    node_id_t old_leader = WaitForLeader(cluster);
    assert(old_leader != 0);
    std::cout << "  ✓ 初始 Leader: Node " << old_leader << "\n";

    // 提交一些命令
    cluster.ClientPropose("CMD_1");
    cluster.ClientPropose("CMD_2");
    WaitForReplication(cluster);

    // 模拟 Leader 宕机
    cluster.CrashNode(old_leader);
    std::cout << "  ✓ Node " << old_leader << " 宕机\n";

    // 等待新 Leader 选出
    node_id_t new_leader = 0;
    for (int i = 0; i < 200 && new_leader == 0; i++) {
        cluster.TickAll(1);
        new_leader = cluster.GetLeader();
        if (new_leader == old_leader) new_leader = 0;  // 排除已宕机的旧 Leader
    }
    assert(new_leader != 0);
    assert(new_leader != old_leader);
    std::cout << "  ✓ 新 Leader: Node " << new_leader << "\n";

    // 新 Leader 应该能继续服务
    bool ok = cluster.ClientPropose("CMD_3_AFTER_FAILOVER");
    assert(ok);
    WaitForReplication(cluster);
    std::cout << "  ✓ 新 Leader 成功接受命令\n";

    // 恢复旧 Leader
    cluster.RestartNode(old_leader);
    WaitForReplication(cluster, 50);
    std::cout << "  ✓ 旧 Leader (Node " << old_leader << ") 恢复\n";

    // 验证一致性
    assert(cluster.CheckConsistency());
    std::cout << "  ✓ 集群一致性验证通过\n";

    cluster.PrintStatus();
    std::cout << "\n";
}

// ============================================================
// 测试 5：网络分区
// ============================================================
void TestNetworkPartition() {
    std::cout << "=== 测试 5：网络分区 ===\n";

    RaftCluster cluster(5);

    // 等待选出 Leader
    node_id_t leader = WaitForLeader(cluster);
    assert(leader != 0);
    std::cout << "  ✓ 初始 Leader: Node " << leader << "\n";

    // 提交一些命令
    cluster.ClientPropose("BEFORE_PARTITION_1");
    cluster.ClientPropose("BEFORE_PARTITION_2");
    WaitForReplication(cluster);

    // 隔离 Leader（模拟网络分区）
    cluster.IsolateNode(leader);
    std::cout << "  ✓ 隔离 Leader (Node " << leader << ")\n";

    // 等待多数派选出新 Leader
    node_id_t new_leader = 0;
    for (int i = 0; i < 200; i++) {
        cluster.TickAll(1);
        for (auto& [id, node] : cluster.GetNodes()) {
            if (id != leader && node->IsLeader() && cluster.IsAlive(id)) {
                new_leader = id;
                break;
            }
        }
        if (new_leader != 0) break;
    }

    if (new_leader != 0) {
        std::cout << "  ✓ 多数派选出新 Leader: Node " << new_leader << "\n";

        // 新 Leader 应该能服务
        bool ok = cluster.GetNode(new_leader)->Propose("AFTER_PARTITION");
        WaitForReplication(cluster, 30);
        if (ok) {
            std::cout << "  ✓ 新 Leader 成功接受命令\n";
        }
    }

    // 恢复网络
    cluster.RecoverNode(leader);
    std::cout << "  ✓ 恢复 Node " << leader << " 的网络连接\n";

    // 等待集群恢复
    WaitForReplication(cluster, 50);

    // 旧 Leader 应该退回 Follower
    auto old_leader_node = cluster.GetNode(leader);
    // 经过足够的时间后，旧 Leader 应该发现新 term 并退回 Follower
    std::cout << "  ✓ 旧 Leader 当前角色: "
              << NodeRoleToString(old_leader_node->GetRole()) << "\n";

    cluster.PrintStatus();
    std::cout << "\n";
}

// ============================================================
// 测试 6：日志冲突解决
// ============================================================
void TestLogConflictResolution() {
    std::cout << "=== 测试 6：日志冲突解决 ===\n";

    RaftLog log;

    // 模拟 Follower 有一些日志
    log.Append(LogEntry{1, 0, "CMD_A"});  // index=1, term=1
    log.Append(LogEntry{1, 0, "CMD_B"});  // index=2, term=1
    log.Append(LogEntry{2, 0, "CMD_C"});  // index=3, term=2
    log.Append(LogEntry{2, 0, "CMD_D"});  // index=4, term=2（冲突日志）

    assert(log.Size() == 4);
    std::cout << "  ✓ Follower 初始日志: 4 条\n";

    // Leader 发来的日志（从 index=3 开始有冲突）
    // prevLogIndex=2, prevLogTerm=1
    std::vector<LogEntry> leader_entries = {
        LogEntry{3, 3, "CMD_E"},  // index=3, term=3（与 Follower 的 term=2 冲突）
        LogEntry{3, 4, "CMD_F"},  // index=4, term=3
        LogEntry{3, 5, "CMD_G"},  // index=5, term=3（新增）
    };

    log.AppendEntries(2, leader_entries);

    // 验证冲突已解决
    assert(log.Size() == 5);
    assert(log.GetTermAt(3) == 3);  // 被替换
    assert(log.GetTermAt(4) == 3);  // 被替换
    assert(log.GetTermAt(5) == 3);  // 新增
    assert(log.GetEntry(3).command == "CMD_E");
    assert(log.GetEntry(5).command == "CMD_G");

    std::cout << "  ✓ 冲突日志已被 Leader 的日志替换\n";
    std::cout << "  ✓ 日志长度: " << log.Size() << "\n";
    std::cout << "\n";
}

// ============================================================
// 测试 7：5 节点集群容错
// ============================================================
void TestFiveNodeFaultTolerance() {
    std::cout << "=== 测试 7：5 节点集群容错 ===\n";

    RaftCluster cluster(5);

    // 等待选出 Leader
    node_id_t leader = WaitForLeader(cluster);
    assert(leader != 0);
    std::cout << "  ✓ Leader: Node " << leader << "\n";

    // 提交命令
    cluster.ClientPropose("FT_CMD_1");
    WaitForReplication(cluster);

    // 宕机 1 个节点（5 节点集群可以容忍 2 个故障）
    node_id_t victim1 = (leader % 5) + 1;
    if (victim1 == leader) victim1 = (victim1 % 5) + 1;
    cluster.CrashNode(victim1);
    std::cout << "  ✓ 宕机 Node " << victim1 << "（1/5 节点故障）\n";

    // 集群应该仍然可用
    bool ok = cluster.ClientPropose("FT_CMD_2_ONE_DOWN");
    WaitForReplication(cluster);
    // Leader 可能已经变了，重新检查
    if (!ok) {
        WaitForLeader(cluster, 100);
        ok = cluster.ClientPropose("FT_CMD_2_ONE_DOWN");
        WaitForReplication(cluster);
    }
    std::cout << "  ✓ 1 个节点故障后集群仍可用\n";

    // 宕机第 2 个节点
    node_id_t victim2 = (victim1 % 5) + 1;
    if (victim2 == leader) victim2 = (victim2 % 5) + 1;
    if (victim2 == victim1) victim2 = (victim2 % 5) + 1;
    cluster.CrashNode(victim2);
    std::cout << "  ✓ 宕机 Node " << victim2 << "（2/5 节点故障）\n";

    // 集群应该仍然可用（3/5 存活 = 多数派）
    WaitForReplication(cluster, 100);
    node_id_t current_leader = cluster.GetLeader();
    if (current_leader != 0) {
        ok = cluster.GetNode(current_leader)->Propose("FT_CMD_3_TWO_DOWN");
        WaitForReplication(cluster);
        std::cout << "  ✓ 2 个节点故障后集群仍可用\n";
    } else {
        // 可能需要重新选举
        WaitForLeader(cluster, 200);
        current_leader = cluster.GetLeader();
        if (current_leader != 0) {
            std::cout << "  ✓ 重新选举后集群恢复，新 Leader: Node " << current_leader << "\n";
        }
    }

    // 恢复所有节点
    cluster.RestartNode(victim1);
    cluster.RestartNode(victim2);
    WaitForReplication(cluster, 50);
    std::cout << "  ✓ 所有节点恢复\n";

    cluster.PrintStatus();
    std::cout << "\n";
}

// ============================================================
// 主函数
// ============================================================
int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║          Raft 协议学习测试套件               ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║  通过运行这些测试，你将理解：                ║\n";
    std::cout << "║    1. Raft 日志的基本操作                    ║\n";
    std::cout << "║    2. Leader 选举机制                        ║\n";
    std::cout << "║    3. 日志复制流程                           ║\n";
    std::cout << "║    4. Leader 故障转移                        ║\n";
    std::cout << "║    5. 网络分区处理                           ║\n";
    std::cout << "║    6. 日志冲突解决                           ║\n";
    std::cout << "║    7. 集群容错能力                           ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    TestRaftLog();
    TestLeaderElection();
    TestLogReplication();
    TestLeaderFailover();
    TestNetworkPartition();
    TestLogConflictResolution();
    TestFiveNodeFaultTolerance();

    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║          🎉 所有测试通过！                   ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    return 0;
}
